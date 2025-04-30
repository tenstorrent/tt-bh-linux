#pragma once

#include "atomic.hpp"
#include "blackhole_pcie.hpp"
#include "tlb_window.hpp"

#include <fmt/core.h>
#include <iostream>

namespace tt {

class BlackholePciDevice;

// L2CPU has TLB windows for NOC access in two flavors: 2 MiB and 128 GiB.
// The 128 GiB windows are weirdly broken when attempting to access PCIe core's
// address space corresponding to the MMIO (i.e. address space in which BARs are
// assigned) region of the device when the PCIe core is in in root port mode.
// The 2 MiB windows work fine.  One day I should figure out the story here.
namespace l2cpu {
struct Tlb2M
{
    union
    {
        struct __attribute__((packed))
        {
            uint64_t address : 43;
            uint64_t reserved0 : 21;
            uint64_t x_end : 6;
            uint64_t y_end : 6;
            uint64_t x_start : 6;
            uint64_t y_start : 6;
            uint64_t multicast_en : 1;
            uint64_t strict_order : 1;
            uint64_t posted : 1;
            uint64_t linked : 1;
            uint64_t static_en : 1;
            uint64_t stream_header : 1;
            uint64_t reserved1 : 1;
            uint64_t noc_selector : 1;
            uint64_t static_vc : 3;
            uint64_t strided : 8;
            uint64_t exclude_coord_x : 5;
            uint64_t exclude_coord_y : 4;
            uint64_t exclude_dir_x : 1;
            uint64_t exclude_dir_y : 1;
            uint64_t exclude_enable : 1;
            uint64_t exclude_routing_option : 1;
            uint64_t num_destinations : 8;
        };
        uint32_t data[4];
    };
};

struct Tlb128G
{
    union
    {
        struct __attribute__((packed))
        {
            uint64_t address : 27;
            uint64_t reserved0 : 5;
            uint64_t x_end : 6;
            uint64_t y_end : 6;
            uint64_t x_start : 6;
            uint64_t y_start : 6;
            uint64_t multicast_en : 1;
            uint64_t strict_order : 1;
            uint64_t posted : 1;
            uint64_t linked : 1;
            uint64_t static_en : 1;
            uint64_t stream_header : 1;
            uint64_t reserved1 : 1;
            uint64_t noc_selector : 1;
            uint64_t static_vc : 3;
            uint64_t strided : 8;
            uint64_t exclude_coord_x : 5;
            uint64_t exclude_coord_y : 4;
            uint64_t exclude_dir_x : 1;
            uint64_t exclude_dir_y : 1;
            uint64_t exclude_enable : 1;
            uint64_t exclude_routing_option : 1;
            uint64_t num_destinations : 8;
        };
        uint32_t data[3];
    };
};

typedef union {
    uint32_t value;
    struct {
        // Bit 0: Enable hardware prefetcher support for scalar loads
        uint32_t scalarLoadSupportEn : 1;

        // Bit 1: Reserved
        uint32_t reserved0 : 1;

        // Bits 7:2: Initial prefetch distance
        uint32_t initialDist : 6;

        // Bits 13:8: Maximum allowed prefetch distance
        uint32_t maxAllowedDist : 6;

        // Bits 19:14: Linear-to-exponential prefetch distance threshold
        uint32_t linToExpThrd : 6;

        // Bits 27:20: Reserved
        uint32_t reserved1 : 8;

        // Bit 28: Enable prefetches to cross-pages
        uint32_t crossPageEn : 1;

        // Bits 30:29: Threshold for forgiving loads with mismatching strides when L2 Prefetcher is in trained state
        uint32_t forgiveThrd : 2;

        // Bit 31: Reserved (Read-Only)
        uint32_t reserved2 : 1;
    } bits;
} PrefetcherCtrl0;

typedef union {
    uint32_t value;
    struct {
        // Bits [3:0]: Threshold fraction/16 of MSHRs to stop sending hits
        uint32_t qFullnessThrd : 4;

        // Bits [8:4]: Threshold number of cache tag hits for evicting prefetch entry
        uint32_t hitCacheThrd : 5;

        // Bits [12:9]: Threshold number of demand hits on hint MSHRs for increasing prefetch distance
        uint32_t hitMSHRThrd : 4;

        // Bits [18:13]: Size of the comparison window for address matching
        uint32_t window : 6;

        // Bit 19: Enable hardware prefetcher support for scalar stores
        uint32_t scalarStoreSupportEn : 1;

        // Bit 20: Enable hardware prefetcher support for vector loads
        uint32_t vectorLoadSupportEn : 1;

        // Bit 21: Enable hardware prefetcher support for vector stores
        uint32_t vectorStoreSupportEn : 1;

        // Bits [31:22]: Reserved
        uint32_t reserved : 10;
    } bits;
} PrefetcherCtrl1;

} // namespace l2cpu

/**
 * @brief One L2CPU core in Blackhole.
 *
 * L2CPU cores are on the NOC.  There are four in Blackhole.  Each contains four
 * X280 cores from SiFive.  L2CPU refers to the X280s plus the surrounding
 * "uncore" logic.
 *
 * The only L2CPU I've bothered with is the one at NOC0 (x=8, y=3).
 *
 * System port and Memory port are the same with one key difference: NOC access
 * to the memory port is coherent with X280 cache.  System port is not.
 *
 * TODO: I've just been changing the access address calculation back and forth
 * in the NOC TLB configuration functions.  If this were real code...
 */
class L2CPU
{
    // clang-format off
    static constexpr uint64_t PERIPHERAL_PORT   = 0x0000'0000'2000'0000ULL; // 256 MiB
    static constexpr uint64_t L3_ZERO_START     = 0x0000'0000'0A00'0000ULL; //   2 MiB
    static constexpr uint64_t L3_ZERO_END       = 0x0000'0000'0A20'0000ULL; //
    static constexpr uint64_t SYSTEM_PORT       = 0x0000'0000'3000'0000ULL; //  64 TiB
    static constexpr uint64_t MEMORY_PORT       = 0x0000'4000'3000'0000ULL; //  64 TiB
    static constexpr uint64_t L2CPU_REGISTERS   = 0xFFFF'F7FE'FFF0'0000ULL; // 512 KiB
    static constexpr uint64_t L2CPU_DMAC        = 0xFFFF'F7FE'FFF8'0000ULL;
    static constexpr uint64_t L2CPU_PREFETCH    = 0x02030000;
    // clang-format on

    BlackholePciDevice& device;
    const uint32_t our_noc0_x;
    const uint32_t our_noc0_y;

public:
    L2CPU(BlackholePciDevice& device, uint32_t noc0_x, uint32_t noc0_y)
        : device(device)
        , our_noc0_x(noc0_x)
        , our_noc0_y(noc0_y)
    {
    }

    uint32_t read32(uint64_t address)
    {
        auto tlb = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, address);
        return tlb->read32(0);
    }

    void write32(uint64_t address, uint32_t value)
    {
        auto tlb = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, address);
        tlb->write32(0, value);
    }


    // TODO: would be ideal to manage tlb_index internally instead of making the
    // caller have to pick one.
    uint64_t configure_noc_tlb_2M(size_t tlb_index, uint32_t noc_x, uint32_t noc_y, uint64_t address)
    {
        auto registers = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, L2CPU_REGISTERS);
        size_t tlb_config_offset = tlb_index * 0x10;

        const size_t tlb_size = 1ULL << 21; // 2 MiB
        const size_t tlb_mask = tlb_size - 1;
        const uint64_t local_offset = address & tlb_mask;
        const size_t apparent_size = tlb_size - local_offset;

        l2cpu::Tlb2M tlb{};
        tlb.address = address >> 21;
        tlb.x_end = noc_x;
        tlb.y_end = noc_y;
        tlb.strict_order = 1;

        mfence();
        registers->write32(tlb_config_offset + 0x0, tlb.data[0]);
        registers->write32(tlb_config_offset + 0x4, tlb.data[1]);
        registers->write32(tlb_config_offset + 0x8, tlb.data[2]);
        registers->write32(tlb_config_offset + 0xC, tlb.data[3]);
        mfence();

        // Where in X280 address space the window begins:
        return 0x0000'0020'3000'0000ULL + (0x200000 * tlb_index);
    }

    void print_noc_tlb_2M(size_t tlb_index)
    {
        auto registers = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, L2CPU_REGISTERS);
        size_t tlb_config_offset = tlb_index * 0x10;

        l2cpu::Tlb2M tlb{};
        tlb.data[0] = registers->read32(tlb_config_offset + 0x0);
        tlb.data[1] = registers->read32(tlb_config_offset + 0x4);
        tlb.data[2] = registers->read32(tlb_config_offset + 0x8);
        tlb.data[3] = registers->read32(tlb_config_offset + 0xC);
        uint64_t address = tlb.address << 21;
        uint64_t x = tlb.x_end;
        uint64_t y = tlb.y_end;
        fmt::print("{} addr: {:#x}, x: {}, y: {}\n", tlb_index, address, x, y);
    }

    void print_noc_tlb_128G(size_t tlb_index)
    {
        auto registers = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, L2CPU_REGISTERS);
        size_t tlb_config_offset = 0xE00 + (tlb_index * 0xC);

        l2cpu::Tlb128G tlb{};
        tlb.data[0] = registers->read32(tlb_config_offset + 0x0);
        tlb.data[1] = registers->read32(tlb_config_offset + 0x4);
        tlb.data[2] = registers->read32(tlb_config_offset + 0x8);
        uint64_t address = (size_t)tlb.address << 37ULL;
        uint64_t x = tlb.x_end;
        uint64_t y = tlb.y_end;
        fmt::print("{} addr: {:#x}, x: {}, y: {}\n", tlb_index, address, x, y);
    }

    uint64_t configure_noc_tlb_128G(size_t tlb_index, uint32_t noc_x, uint32_t noc_y, uint64_t address)
    {
        auto registers = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, L2CPU_REGISTERS);
        size_t tlb_config_offset = 0xE00 + (tlb_index * 0xC);

        const size_t tlb_size = 1ULL << 37; // 128 GiB
        const size_t tlb_mask = tlb_size - 1;
        const uint64_t local_offset = address & tlb_mask;
        const size_t apparent_size = tlb_size - local_offset;

        l2cpu::Tlb128G tlb{};
        tlb.address = address >> 37;
        tlb.x_end = noc_x;
        tlb.y_end = noc_y;

        // HACK!
        // tlb.strict_order = 1;
        // tlb.posted = 1;

        mfence();
        registers->write32(tlb_config_offset + 0x0, tlb.data[0]);
        registers->write32(tlb_config_offset + 0x4, tlb.data[1]);
        registers->write32(tlb_config_offset + 0x8, tlb.data[2]);
        mfence();

        // Where in X280 space does the window begin?  L2CPU Spec.docx gave me
        // numbers that did not work.
        //
        // Andrew says,
        //  RTL uses bit 43 to determine whether to use 2MB TLBs (bit 43=0) or 128GB TLBs (bit 43=1)
        //  the address is evaluated after passing ddr_noc_xbar which sends 0x2000000000+ to NOC
        //  So I think the first 128GB TLB is at system_port_address + noc_address + bit 43 = 0x82030000000
        //
        // Maybe that's not right?!
        // 0x0060_3000_0000

        // auto access_address = 0x82030000000 + (0x2000000000 * tlb_index) + local_offset;
        // auto access_address = ((1ULL << 43) | ((1ULL << 37) * (1 + tlb_index)) | SYSTEM_PORT) + local_offset;
        auto access_address = 0x80430000000ULL + ((1ULL << 37) * tlb_index) + local_offset;
        return access_address;
    }

    void configure_prefetcher(uint32_t prefetcher_ctrl0, uint32_t prefetcher_ctrl1)
    {
        std::vector<uint64_t> offsets = { 0x0000, 0x2000, 0x4000, 0x6000 };
        for (auto offset : offsets) {
            auto addr = L2CPU_PREFETCH + offset;
            auto val = read32(addr);

            write32(addr, prefetcher_ctrl0);
            fmt::print("Prefetcher at {:#x} configured: {:#x} -> {:#x}\n", addr, val, prefetcher_ctrl0);

            addr += 0x4;
            val = read32(addr);
            write32(addr, prefetcher_ctrl1);

            fmt::print("Prefetcher at {:#x} configured: {:#x} -> {:#x}\n", addr, val, prefetcher_ctrl1);

            l2cpu::PrefetcherCtrl0 ctrl0;
            l2cpu::PrefetcherCtrl1 ctrl1;
            ctrl0.value = prefetcher_ctrl0;
            ctrl1.value = prefetcher_ctrl1;

            // fmt::print("Prefetcher settings: scalarLoadSupportEn: {}, initialDist: {}, maxAllowedDist: {}, "
            //            "linToExpThrd: {}, crossPageEn: {}, forgiveThrd: {}\n",
            //            (int)ctrl0.bits.scalarLoadSupportEn, (int)ctrl0.bits.initialDist, (int)ctrl0.bits.maxAllowedDist,
            //            (int)ctrl0.bits.linToExpThrd, (int)ctrl0.bits.crossPageEn, (int)ctrl0.bits.forgiveThrd);

            fmt::println("scalarLoadSupportEn: {}", (int)ctrl0.bits.scalarLoadSupportEn);
            fmt::println("initialDist: {}", (int)ctrl0.bits.initialDist);
            fmt::println("maxAllowedDist: {}", (int)ctrl0.bits.maxAllowedDist);
            fmt::println("linToExpThrd: {}", (int)ctrl0.bits.linToExpThrd);
            fmt::println("crossPageEn: {}", (int)ctrl0.bits.crossPageEn);
            fmt::println("forgiveThrd: {}\n", (int)ctrl0.bits.forgiveThrd);
            fmt::println("qFullnessThrd: {}", (int)ctrl1.bits.qFullnessThrd);
            fmt::println("hitCacheThrd: {}", (int)ctrl1.bits.hitCacheThrd);
            fmt::println("hitMSHRThrd: {}", (int)ctrl1.bits.hitMSHRThrd);
            fmt::println("window: {}", (int)ctrl1.bits.window);
            fmt::println("scalarStoreSupportEn: {}", (int)ctrl1.bits.scalarStoreSupportEn);
            fmt::println("vectorLoadSupportEn: {}", (int)ctrl1.bits.vectorLoadSupportEn);
            fmt::println("vectorStoreSupportEn: {}\n", (int)ctrl1.bits.vectorStoreSupportEn);

        }
    }

    void configure_prefetcher_default()
    {
        configure_prefetcher(0x14a0c, 0xc45e);
    }

    void configure_prefetcher_recommended()
    {
        configure_prefetcher(0x15811, 0x38c84e);
    }
};

} // namespace tt