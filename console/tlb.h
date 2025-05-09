#ifndef TLB_H
#define TLB_H
#include <unistd.h>
#include <iostream>
#include <memory>

#include "ioctl.h"

struct xy_t
{
    uint16_t x;
    uint16_t y;
};


class TlbHandle
{
    int fd;
    int tlb_id;
    uint8_t *tlb_base;
    size_t tlb_size;

public:
    TlbHandle(int fd, size_t size, const tenstorrent_noc_tlb_config &config);

    uint8_t* data();
    size_t size() const;

    ~TlbHandle() noexcept;

};

class TlbWindow2M
{
    static constexpr size_t WINDOW_SIZE = 1 << 21;
    static constexpr size_t WINDOW_MASK = WINDOW_SIZE - 1;
    static_assert((WINDOW_SIZE & WINDOW_MASK) == 0, "WINDOW_SIZE must be a power of 2");

    const uint64_t offset;  // Within the window, to reach target address.
    std::unique_ptr<TlbHandle> window;

public:
    TlbWindow2M(int fd, uint16_t x, uint16_t y, uint64_t addr);

    void write32(uint64_t addr, uint32_t value);

    uint32_t read32(uint64_t addr);

    uint8_t* get_window();
};
#endif
