#include <sys/mman.h>
#include <sys/ioctl.h>
#include <cassert>
#include "tlb.h"

TlbHandle::TlbHandle(int fd, size_t size, const tenstorrent_noc_tlb_config &config)
    : fd(fd)
    , tlb_size(size)
{
    tenstorrent_allocate_tlb allocate_tlb{};
    allocate_tlb.in.size = size;
    if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &allocate_tlb) != 0){
        std::cerr<<"Failed to allocate TLB";
        exit(1);
    }

    tlb_id = allocate_tlb.out.id;

    tenstorrent_configure_tlb configure_tlb{};
    configure_tlb.in.id = tlb_id;
    configure_tlb.in.config = config;
    if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0){
        tenstorrent_free_tlb free_tlb{};
        free_tlb.in.id = tlb_id;
        ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
        std::cerr<<"Failed to configure TLB";
        exit(1);
    }

    void *mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, allocate_tlb.out.mmap_offset_uc);
    if (mem == MAP_FAILED) {
        tenstorrent_free_tlb free_tlb{};
        free_tlb.in.id = tlb_id;
        ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
        std::cerr<<"Failed to map TLB";
        exit(1);
    }

    tlb_base = reinterpret_cast<uint8_t *>(mem);
}

uint8_t* TlbHandle::data() { return tlb_base; }
size_t TlbHandle::size() const { return tlb_size; }

TlbHandle::~TlbHandle() noexcept
{
    tenstorrent_free_tlb free_tlb{};
    free_tlb.in.id = tlb_id;

    munmap(tlb_base, tlb_size);
    ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
}


TlbWindow2M::TlbWindow2M(int fd, uint16_t x, uint16_t y, uint64_t addr)
: offset(addr & WINDOW_MASK)
{
    tenstorrent_noc_tlb_config config{
        .addr = addr & ~WINDOW_MASK,
        .x_end = x,
        .y_end = y,
    };

    window = std::make_unique<TlbHandle>(fd, WINDOW_SIZE, config);
}


void TlbWindow2M::write32(uint64_t addr, uint32_t value){
    assert(offset + addr + 4 <= WINDOW_SIZE);
    assert(((offset + addr) % 4) == 0);
    void *ptr = window->data() + offset + addr;
    *reinterpret_cast<volatile uint32_t *>(ptr) = value;
}

uint32_t TlbWindow2M::read32(uint64_t addr){
    assert(offset + addr + 4 <= WINDOW_SIZE);
    assert(((offset + addr) % 4) == 0);
    void *ptr = window->data() + offset + addr;
    return *reinterpret_cast<volatile uint32_t *>(ptr);
}

uint8_t* TlbWindow2M::get_window(){
    return window.get()->data() + offset;
}

