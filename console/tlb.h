// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <iostream>
#include <algorithm>
#include <array>
#include <memory>
#include <random>
#include <map>

#include "ioctl.h"

static constexpr size_t TWO_MEG = 1 << 21;
static constexpr size_t FOUR_GIG = 1ULL << 32;

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

template <size_t WINDOW_SIZE>
class TlbWindow
{
    static constexpr size_t WINDOW_MASK = WINDOW_SIZE - 1;
    static_assert((WINDOW_SIZE & WINDOW_MASK) == 0, "WINDOW_SIZE must be a power of 2");

    const uint64_t offset;  // Within the window, to reach target address.
    std::unique_ptr<TlbHandle> window;
    
public:
    TlbWindow(int fd, uint16_t x, uint16_t y, uint64_t addr);

    void write32(uint64_t addr, uint32_t value);
    
    uint32_t read32(uint64_t addr);

    uint8_t* get_window();

    uint64_t get_offset();

};

template <size_t WINDOW_SIZE>
TlbWindow<WINDOW_SIZE>::TlbWindow(int fd, uint16_t x, uint16_t y, uint64_t addr)
: offset(addr & WINDOW_MASK)
{
    std::cout<<"Addr 0x"<<std::hex<<addr<<"\n";
    tenstorrent_noc_tlb_config config{
        .addr = addr & ~WINDOW_MASK,
        .x_end = x,
        .y_end = y,
    };

    window = std::make_unique<TlbHandle>(fd, WINDOW_SIZE, config);
}

template <size_t WINDOW_SIZE>
void TlbWindow<WINDOW_SIZE>::write32(uint64_t addr, uint32_t value) 
{
    // if (addr & 3)
    //     THROW_TEST_FAILURE("Misaligned write");

    void *ptr = window->data() + offset + addr;
    *reinterpret_cast<volatile uint32_t *>(ptr) = value;
}

template <size_t WINDOW_SIZE>
uint32_t TlbWindow<WINDOW_SIZE>::read32(uint64_t addr)
{
    // if (addr & 3)
    //     THROW_TEST_FAILURE("Misaligned read");

    void *ptr = window->data() + offset + addr;
    return *reinterpret_cast<volatile uint32_t *>(ptr);
}

template <size_t WINDOW_SIZE>
uint8_t* TlbWindow<WINDOW_SIZE>::get_window(){
    return window.get()->data() + offset;
}

template <size_t WINDOW_SIZE>
uint64_t TlbWindow<WINDOW_SIZE>::get_offset(){
    return offset;
}

using TlbWindow2M = TlbWindow<TWO_MEG>;
using TlbWindow4G = TlbWindow<FOUR_GIG>;

