#include <unistd.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

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

    void write(uint64_t addr, const std::vector<uint8_t> value); // Write bytes

    std::vector<uint8_t> read(uint64_t addr, size_t size); // Read bytes

    uint8_t* get_window();

    uint64_t get_offset();

};

template <size_t WINDOW_SIZE>
TlbWindow<WINDOW_SIZE>::TlbWindow(int fd, uint16_t x, uint16_t y, uint64_t addr)
: offset(addr & WINDOW_MASK)
{
    std::cout<<"Addr 0x"<<std::hex<<addr<<"\n"<<std::dec;
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
    std::vector<uint8_t> vec(4);
    std::memcpy(vec.data(), reinterpret_cast<uint8_t*>(value), 4);
    write(addr, vec);
}

template <size_t WINDOW_SIZE>
uint32_t TlbWindow<WINDOW_SIZE>::read32(uint64_t addr)
{
    uint32_t result;
    std::vector<uint8_t> vec = read(addr, 4);
    std::memcpy(reinterpret_cast<uint8_t*>(&result), vec.data(), 4);
    return result;
}

template <size_t WINDOW_SIZE>
uint8_t* TlbWindow<WINDOW_SIZE>::get_window(){
    return window.get()->data() + offset;
}

template <size_t WINDOW_SIZE>
uint64_t TlbWindow<WINDOW_SIZE>::get_offset(){
    return offset;
}

template <size_t WINDOW_SIZE>
void TlbWindow<WINDOW_SIZE>::write(uint64_t addr, const std::vector<uint8_t> value){
    assert(offset + addr + value.size() <= WINDOW_SIZE);
    uint8_t *ptr = window.get()->data() + offset + addr;
    const uint8_t *src = reinterpret_cast<const uint8_t*>(value.data());
    std::memcpy(ptr, src, value.size());
}

template <size_t WINDOW_SIZE>
std::vector<uint8_t> TlbWindow<WINDOW_SIZE>::read(uint64_t addr, size_t size){
    assert(offset + addr + size <= WINDOW_SIZE);
    std::vector<uint8_t> result(size);
    uint8_t *ptr = window.get()->data() + offset + addr;
    uint8_t *dst = reinterpret_cast<uint8_t*>(result.data());
    std::memcpy(dst, ptr, size);
    return result;
}

using TlbWindow2M = TlbWindow<TWO_MEG>;
using TlbWindow4G = TlbWindow<FOUR_GIG>;

