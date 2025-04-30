#include "blackhole_pcie.hpp"

#include "atomic.hpp"
#include "ioctl.h"
#include "logger.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <numeric>
#include <stdexcept>

#define IOCTL(fd, request, arg)                                                                                        \
    if (ioctl(fd, request, arg) < 0) {                                                                                 \
        throw std::runtime_error("ioctl failed");                                                                      \
    }

#define CHECK(cond)                                                                                                    \
    if (!(cond)) {                                                                                                     \
        std::cerr << "CHECK failed: " << #cond << std::endl;                                                           \
        std::abort();                                                                                                  \
    }

namespace tt {

// Blackhole has 2 MiB and 4 GiB TLB windows.
static const size_t BH_NUM_2M_TLBS = 202;
static const size_t BH_NUM_4G_TLBS = 8;

// There is a convention to map BAR0 (containing 2MiB windows) into WC and UC
static const size_t BH_NUM_2M_WC_TLBS = 188;
static const size_t BH_NUM_2M_UC_TLBS = 14 - 1; // HACK: reserve one for kernel

// These numbers represent indices into the TLB configuration registers.
// 4G indicies start where 2M indicies end.
static const size_t BH_2M_TLB_START = 0;
static const size_t BH_2M_TLB_WC_START = 0;
static const size_t BH_2M_TLB_UC_START = 188;
static const size_t BH_2M_TLB_END = 201;
static const size_t BH_4G_TLB_START = 202;
static const size_t BH_4G_TLB_END = 209;

namespace pcie {
/**
 * @brief 2M TLB configuration register layout.
 */
struct Tlb2M
{
    union
    {
        struct __attribute__((packed))
        {
            uint64_t address : 43;
            uint64_t x_end : 6;
            uint64_t y_end : 6;
            uint64_t x_start : 6;
            uint64_t y_start : 6;
            uint64_t noc : 2;
            uint64_t multicast : 1;
            uint64_t ordering : 2;
            uint64_t linked : 1;
            uint64_t use_static_vc : 1;
            uint64_t stream_header : 1;
            uint64_t static_vc : 3;
            uint64_t reserved : 18;
        };
        uint32_t data[3];
    };
};

/**
 * @brief 4G TLB configuration register layout.
 */
struct Tlb4G
{
    union
    {
        struct __attribute__((packed))
        {
            uint64_t address : 32;
            uint64_t x_end : 6;
            uint64_t y_end : 6;
            uint64_t x_start : 6;
            uint64_t y_start : 6;
            uint64_t noc : 2;
            uint64_t multicast : 1;
            uint64_t ordering : 2;
            uint64_t linked : 1;
            uint64_t use_static_vc : 1;
            uint64_t stream_header : 1;
            uint64_t static_vc : 3;
            uint64_t reserved : 29;
        };
        uint32_t data[3];
    };
};
} // namespace pcie

static PciDeviceInfo get_device_info(int fd)
{
    tenstorrent_get_device_info info{};

    info.in.output_size_bytes = sizeof(tenstorrent_get_device_info_out);

    IOCTL(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info);

    uint16_t bus = info.out.bus_dev_fn >> 8;
    uint16_t dev = (info.out.bus_dev_fn >> 3) & 0x1F;
    uint16_t fn = info.out.bus_dev_fn & 0x07;

    return PciDeviceInfo{info.out.vendor_id, info.out.device_id, info.out.pci_domain, bus, dev, fn};
}

static tenstorrent_mapping get_mapping(int fd, int id)
{
    static const size_t NUM_MAPPINGS = 8; // TODO(jms) magic 8
    struct
    {
        tenstorrent_query_mappings query_mappings{};
        tenstorrent_mapping mapping_array[NUM_MAPPINGS];
    } mappings;

    mappings.query_mappings.in.output_mapping_count = NUM_MAPPINGS;

    IOCTL(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings);

    for (size_t i = 0; i < NUM_MAPPINGS; i++) {
        if (mappings.mapping_array[i].mapping_id == id) {
            return mappings.mapping_array[i];
        }
    }

    throw std::runtime_error("Unknown mapping");
}

static uint8_t* map_bar0(int fd, size_t size)
{
    auto wc_resource = get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE0_WC);
    auto uc_resource = get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE0_UC);

    // There exists a convention that BAR0 is divided into write-combined (lower) and uncached (upper) mappings.
    auto wc_size = 188 << 21;
    auto uc_size = uc_resource.mapping_size - wc_size;
    auto wc_offset = 0;
    auto uc_offset = wc_size;

    uc_resource.mapping_base += wc_size;

    auto* bar0 = static_cast<uint8_t*>(mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

    CHECK(bar0 != MAP_FAILED);

    void* wc =
        mmap(bar0 + wc_offset, wc_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, wc_resource.mapping_base);
    void* uc =
        mmap(bar0 + uc_offset, uc_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, uc_resource.mapping_base);

    CHECK(wc != MAP_FAILED);
    CHECK(uc != MAP_FAILED);

    return bar0;
}

static uint8_t* map_bar2(int fd, size_t size)
{
    auto uc_resource = get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE1_UC); // BAR2 is index 1
    void* bar2 = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, uc_resource.mapping_base);

    CHECK(bar2 != MAP_FAILED);
    CHECK(size == uc_resource.mapping_size);

    return static_cast<uint8_t*>(bar2);
}

static uint8_t* map_bar4(int fd, size_t size)
{
    auto wc_resource = get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE2_WC); // BAR4 is index 2 (BAR2 is index 1)
    void* bar4 = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, wc_resource.mapping_base);

    CHECK(bar4 != MAP_FAILED);
    CHECK(size == wc_resource.mapping_size);

    return static_cast<uint8_t*>(bar4);
}

class BlackholeTLB : public TlbWindow
{
    std::function<void()> on_destruct;

public:
    BlackholeTLB(void* memory, size_t size, std::function<void()> release)
        : TlbWindow(reinterpret_cast<uint8_t*>(memory), size)
        , on_destruct(release)
    {
    }

    virtual ~BlackholeTLB() override
    {
        if (on_destruct) {
            on_destruct();
        }
    }

    virtual void write_block(uint64_t address, const void* buffer, size_t size) override
    {
        if (address + size > window_size) {
            fmt::print("address: {:#x}, size: {:#x}, window_size: {:#x}\n", address, size, window_size);
            throw std::out_of_range("Out of bounds access");
        }

        // TODO: is this safe?  What about alignment?
        std::memcpy(base + address, buffer, size);
    }

    virtual void read_block(uint64_t address, void* buffer, size_t size) override
    {
        if (address + size > window_size) {
            throw std::out_of_range("Out of bounds access");
        }

        // TODO: is this safe?  What about alignment?
        std::memcpy(buffer, base + address, size);
    }
};

BlackholePciDevice::BlackholePciDevice(const std::string& path)
    : fd(open(path.c_str(), O_RDWR | O_CLOEXEC))
    , info(get_device_info(fd))
    , bar0_size(1ULL << 29) // 512 MiB
    , bar2_size(1ULL << 20) //   1 MiB
    , bar4_size(1ULL << 35) //  32 GiB
    , bar0(map_bar0(fd, bar0_size))
    , bar2(map_bar2(fd, bar2_size))
    , bar4(map_bar4(fd, bar4_size))
    , free_tlb_indices_2M_WC(BH_NUM_2M_WC_TLBS, 0)
    , free_tlb_indices_2M_UC(BH_NUM_2M_UC_TLBS, 0)
    , free_tlb_indices_4G(BH_NUM_4G_TLBS, 0)
{
    std::iota(free_tlb_indices_2M_WC.begin(), free_tlb_indices_2M_WC.end(), BH_2M_TLB_WC_START);
    std::iota(free_tlb_indices_2M_UC.begin(), free_tlb_indices_2M_UC.end(), BH_2M_TLB_UC_START);
    std::iota(free_tlb_indices_4G.begin(), free_tlb_indices_4G.end(), BH_4G_TLB_START);
    std::reverse(free_tlb_indices_2M_WC.begin(), free_tlb_indices_2M_WC.end()); // HACK
}

BlackholePciDevice::~BlackholePciDevice()
{
    munmap(bar0, bar0_size);
    munmap(bar4, bar4_size);
    close(fd);
}

std::unique_ptr<TlbWindow> BlackholePciDevice::map_tlb_2M_WC(uint32_t x, uint32_t y, uint64_t address)
{
    std::scoped_lock lock(tlb_mutex);

    if (free_tlb_indices_2M_WC.empty()) {
        throw std::runtime_error("No free 2MiB WC TLB entries available");
    }

    const size_t tlb_index = free_tlb_indices_2M_WC.back();
    const size_t tlb_size = 1 << 21;
    const size_t tlb_mask = tlb_size - 1;
    const uint64_t local_offset = address & tlb_mask;
    const size_t apparent_size = tlb_size - local_offset;

    pcie::Tlb2M tlb_config{};
    tlb_config.address = address >> 21;
    tlb_config.x_end = x;
    tlb_config.y_end = y;

    write_tlb_config_2M(tlb_index, tlb_config);

    void* memory = bar0 + (tlb_size * tlb_index) + local_offset;
    auto release = [this, tlb_index]() {
        std::scoped_lock lock(tlb_mutex);
        free_tlb_indices_2M_WC.push_back(tlb_index);
    };

    free_tlb_indices_2M_WC.pop_back();

    return std::make_unique<BlackholeTLB>(memory, apparent_size, release);
}

// This is pasta, for WC vs UC.
std::unique_ptr<TlbWindow> BlackholePciDevice::map_tlb_2M_UC(uint32_t x, uint32_t y, uint64_t address)
{
    std::scoped_lock lock(tlb_mutex);

    if (free_tlb_indices_2M_UC.empty()) {
        throw std::runtime_error("No free 2MiB UC TLB entries available");
    }

    const size_t tlb_index = free_tlb_indices_2M_UC.back();
    const size_t tlb_size = 1 << 21;
    const size_t tlb_mask = tlb_size - 1;
    const uint64_t local_offset = address & tlb_mask;
    const size_t apparent_size = tlb_size - local_offset;

    pcie::Tlb2M tlb_config{};
    tlb_config.address = address >> 21;
    tlb_config.x_end = x;
    tlb_config.y_end = y;

    write_tlb_config_2M(tlb_index, tlb_config);

    void* memory = bar0 + (tlb_size * tlb_index) + local_offset;
    auto release = [this, tlb_index]() {
        std::scoped_lock lock(tlb_mutex);
        free_tlb_indices_2M_UC.push_back(tlb_index);
    };

    free_tlb_indices_2M_UC.pop_back();

    return std::make_unique<BlackholeTLB>(memory, apparent_size, release);
}

std::unique_ptr<TlbWindow> BlackholePciDevice::map_tlb_4G(uint32_t x, uint32_t y, uint64_t address)
{
    std::scoped_lock lock(tlb_mutex);

    if (free_tlb_indices_4G.empty()) {
        throw std::runtime_error("No free 4GiB TLB entries available");
    }

    const size_t tlb_size = 1ULL << 32;
    const size_t tlb_mask = tlb_size - 1;
    const size_t tlb_index = free_tlb_indices_4G.back();
    const uint64_t local_offset = address & tlb_mask;
    const size_t apparent_size = tlb_size - local_offset;

    pcie::Tlb4G tlb_config{};
    tlb_config.address = address >> 32;
    tlb_config.x_end = x;
    tlb_config.y_end = y;

    write_tlb_config_4G(tlb_index, tlb_config);

    void* memory = bar4 + (tlb_size * (tlb_index - BH_4G_TLB_START)) + local_offset;
    auto release = [this, tlb_index]() {
        std::scoped_lock lock(tlb_mutex);
        free_tlb_indices_4G.push_back(tlb_index);
    };

    free_tlb_indices_4G.pop_back();

    return std::make_unique<BlackholeTLB>(memory, apparent_size, release);
}

uint64_t BlackholePciDevice::map_for_dma(const void* buffer, size_t size)
{
    tenstorrent_pin_pages pin{};
    pin.in.output_size_bytes = sizeof(pin.out);
    pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
    pin.in.size = size;
    // pin.in.flags = TENSTORRENT_PIN_PAGES_INTO_IOMMU;

    // If this is failing on you, check that the buffer is page-aligned and
    // that the size is a multiple of the page size.  Also that IOMMU is on.
    IOCTL(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin);

    return pin.out.physical_address;
}

void BlackholePciDevice::configure_iatu_region(size_t region, uint64_t base, uint64_t target, size_t size)
{
    static constexpr uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1200;
    uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);

    if (bar2 == nullptr || bar2 == MAP_FAILED) {
        throw std::runtime_error("BAR2 not mapped");
    }

    auto write_iatu_reg = [this](uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(bar2 + offset) = value;
    };

    std::cout << "size is " << size << std::endl;

    uint64_t limit = (base + (size - 1));
    uint32_t limit_lo = (limit >> 0x00) & 0xffffffff;
    uint32_t limit_hi = (limit >> 0x20) & 0xffffffff;
    uint32_t base_lo = (base >> 0x00) & 0xffff'ffff;
    uint32_t base_hi = (base >> 0x20) & 0xffff'ffff;
    uint32_t target_lo = (target >> 0x00) & 0xffff'ffff;
    uint32_t target_hi = (target >> 0x20) & 0xffff'ffff;
    std::cout << "limit is " << limit << std::endl;
    std::cout << "limit_lo is " << limit_lo << std::endl;
    std::cout << "limit_hi is " << limit_hi << std::endl;

    uint32_t region_ctrl_1 = 1 << 13;  // INCREASE_REGION_SIZE
    uint32_t region_ctrl_2 = 1 << 31;  // REGION_EN
    uint32_t region_ctrl_3 = 0;

    write_iatu_reg(iatu_base + 0x00, region_ctrl_1);
    write_iatu_reg(iatu_base + 0x04, region_ctrl_2);
    write_iatu_reg(iatu_base + 0x08, base_lo);
    write_iatu_reg(iatu_base + 0x0c, base_hi);
    write_iatu_reg(iatu_base + 0x10, limit_lo);
    write_iatu_reg(iatu_base + 0x14, target_lo);
    write_iatu_reg(iatu_base + 0x18, target_hi);
    write_iatu_reg(iatu_base + 0x1c, region_ctrl_3);
    write_iatu_reg(iatu_base + 0x20, limit_hi);
}

void BlackholePciDevice::dump_iatu_region(size_t region)
{
    static constexpr uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1200;
    uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);

    if (bar2 == nullptr || bar2 == MAP_FAILED) {
        throw std::runtime_error("BAR2 not mapped");
    }

    auto read_iatu_reg = [this](uint64_t offset) {
        return *reinterpret_cast<volatile uint32_t *>(bar2 + offset);
    };

    std::cout << std::hex;
    std::cout << region << " region_ctrl_1:" << read_iatu_reg(iatu_base + 0x00) << std::endl;
    std::cout << region << " region_ctrl_2:" << read_iatu_reg(iatu_base + 0x04) << std::endl;
    std::cout << region << " base_lo:" << read_iatu_reg(iatu_base + 0x08) << std::endl;
    std::cout << region << " base_hi:" << read_iatu_reg(iatu_base + 0x0c) << std::endl;
    std::cout << region << " limit:" << read_iatu_reg(iatu_base + 0x10) << std::endl;
    std::cout << region << " target_lo:" << read_iatu_reg(iatu_base + 0x14) << std::endl;
    std::cout << region << " target_hi:" << read_iatu_reg(iatu_base + 0x18) << std::endl;
    std::cout << region << " region_ctrl_3:" << read_iatu_reg(iatu_base + 0x1c) << std::endl;
    std::cout << region << " limit_hi:" << read_iatu_reg(iatu_base + 0x20) << std::endl;
    std::cout << std::dec;
}

uint32_t* BlackholePciDevice::tlb_config_registers(size_t tlb_index)
{
    const size_t tlb_config_reg_offset = 0x1FC00000; // From bottom of BAR0
    const size_t tlb_config_size = 12;               // Bytes
    const size_t offset = tlb_config_reg_offset + (tlb_index * tlb_config_size);

    return reinterpret_cast<uint32_t*>(this->bar0 + offset);
}

void BlackholePciDevice::write_tlb_config_2M(size_t tlb_index, const pcie::Tlb2M& tlb_config)
{
    CHECK(tlb_index >= BH_2M_TLB_START);
    CHECK(tlb_index <= BH_2M_TLB_END);

    volatile uint32_t* dst = tlb_config_registers(tlb_index);

    mfence();
    dst[0] = tlb_config.data[0];
    dst[1] = tlb_config.data[1];
    dst[2] = tlb_config.data[2];

    mfence();
}

void BlackholePciDevice::write_tlb_config_4G(size_t tlb_index, const pcie::Tlb4G& tlb_config)
{
    CHECK(tlb_index >= BH_4G_TLB_START);
    CHECK(tlb_index <= BH_4G_TLB_END);

    volatile uint32_t* dst = tlb_config_registers(tlb_index);

    mfence();
    dst[0] = tlb_config.data[0];
    dst[1] = tlb_config.data[1];
    dst[2] = tlb_config.data[2];
    mfence();
}

pcie::Tlb2M BlackholePciDevice::read_tlb_config_2M(size_t tlb_index)
{
    CHECK(tlb_index >= BH_2M_TLB_START);
    CHECK(tlb_index <= BH_2M_TLB_END);

    volatile uint32_t* src = tlb_config_registers(tlb_index);
    pcie::Tlb2M tlb_config{};

    tlb_config.data[0] = src[0];
    tlb_config.data[1] = src[1];
    tlb_config.data[2] = src[2];

    return tlb_config;
}

pcie::Tlb4G BlackholePciDevice::read_tlb_config_4G(size_t tlb_index)
{
    CHECK(tlb_index >= BH_4G_TLB_START);
    CHECK(tlb_index <= BH_4G_TLB_END);

    volatile uint32_t* src = tlb_config_registers(tlb_index);
    pcie::Tlb4G tlb_config{};

    tlb_config.data[0] = src[0];
    tlb_config.data[1] = src[1];
    tlb_config.data[2] = src[2];

    return tlb_config;
}

} // namespace tt
