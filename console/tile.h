#ifndef TILE_H
#define TILE_H
#include <unistd.h>
#include <cstdint>
#include <memory>
#include <vector>

#include "ioctl.h"
#include "tlb.h"

// Abstract Base Class for all Tiles
class Tile
{
    int fd;
    
    // Using unique_ptr here should ensure that all these windows are
    // cleaned up when L2CPU goes out of scope
    std::vector<std::unique_ptr<TlbWindow2M>> persistent_2M_tlb_windows;
    
protected:
    xy_t coordinates;
    
    virtual void set_coordinates() = 0;

public:
    Tile();

    xy_t get_coordinates();

    uint8_t* get_persistent_2M_tlb_window(uint64_t addr);

    void write32(uint64_t addr, uint32_t value);
    
    uint32_t read32(uint64_t addr);

    void write(uint64_t addr, std::vector<uint8_t>);

    std::vector<uint8_t> read(uint64_t addr, size_t size);

    ~Tile() noexcept;

};
#endif