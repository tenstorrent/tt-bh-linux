#include <fcntl.h>
#include "tile.h"

Tile::Tile()
{
    fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
}

xy_t Tile::get_coordinates(){
    return coordinates;
}

/*
For read32 and write32, we offer 2 sets of functions
read32/write32 accept an absolute address value

These functions create a "temporary" TlbWindow to do accomplish the task they want to do
This TlbWindow should get cleaned up immediately
*/

void Tile::write32(uint64_t addr, uint32_t value) {
    TlbWindow2M temporary_window(fd, coordinates.x, coordinates.y, addr);
    temporary_window.write32(0, value);
}

uint32_t Tile::read32(uint64_t addr) {
    TlbWindow2M temporary_window(fd, coordinates.x, coordinates.y, addr);
    return temporary_window.read32(0);
}

void Tile::write(uint64_t addr, std::vector<uint8_t> value) {
    TlbWindow4G temporary_window(fd, coordinates.x, coordinates.y, addr);
    temporary_window.write(0, value);
}

std::vector<uint8_t> Tile::read(uint64_t addr, size_t size) {
    TlbWindow4G temporary_window(fd, coordinates.x, coordinates.y, addr);
    return temporary_window.read(0, size);
}

/*
While read32/write32 are enough for many one off operations, we need some way to "persistently" map a memory location to a struct
This function creates a TlbWindow on the heap that gets cleaned up when the L2CPU goes out of scope
*/
uint8_t* Tile::get_persistent_2M_tlb_window(uint64_t addr){
    persistent_2M_tlb_windows.push_back(std::unique_ptr<TlbWindow2M>(new TlbWindow2M(fd, coordinates.x, coordinates.y, addr)));
    return persistent_2M_tlb_windows.back()->get_window();
}

Tile::~Tile() noexcept
{
    close(fd);
}


