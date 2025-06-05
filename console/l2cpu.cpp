// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0

#include <fcntl.h>
#include <cassert>
#include <sys/mman.h>
#include "l2cpu.h"

/*
The blackhole chip consists of different tiles (tensix, gddr, pcie, ethernet, l2cpu) that are addressible using their location on the NOC.

A L2CPU tile represents the Sifive X280 Quad core riscv64 CPU along with it's attached peripherals (interrupt controllers, etc..)

There are 4 such L2CPU tiles in blackhole This entry represents the location of each of these L2CPUs on the NOC.
*/
const std::map<int, xy_t> l2cpu_tile_mapping {
    {0, xy_t{x: 8, y: 3}},
    {1, xy_t{x: 8, y: 9}},
    {2, xy_t{x: 8, y: 5}},
    {3, xy_t{x: 8, y: 7}},
};

/*
This represents the starting address of the DRAM for each L2CPU
*/
const std::map<int, uint64_t> l2cpu_starting_address_mapping {
    {0, 0x4000'3000'0000ULL},
    {1, 0x4000'3000'0000ULL},
    {2, 0x4000'3000'0000ULL},
    {3, 0x4000'b000'0000ULL},
};


/*
This represents the size of the memory available to each L2CPU
*/
const std::map<int, uint64_t> l2cpu_memory_size_mapping {
    {0, 0x1'0000'0000ULL},
    {1, 0x1'0000'0000ULL},
    {2, 0x8000'0000ULL},
    {3, 0x8000'0000ULL},
};

L2CPU::L2CPU(int idx)
    : idx(idx)
{
    assert(idx >=0 && idx < 4);
    fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
    starting_address = l2cpu_starting_address_mapping.at(idx);
    coordinates = l2cpu_tile_mapping.at(idx);
    memory_size = l2cpu_memory_size_mapping.at(idx);

    memory = reinterpret_cast<uint8_t*>(mmap(nullptr, (2ULL<<32), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

    first = std::make_unique<TlbWindow4G>(fd, coordinates.x, coordinates.y, 0x4000'0000'0000ULL, memory);
    second = std::make_unique<TlbWindow4G>(fd, coordinates.x, coordinates.y, 0x4001'0000'0000ULL, memory+(1ULL<<32));
}

uint64_t L2CPU::get_starting_address(){
  return starting_address;
}

uint64_t L2CPU::get_memory_size(){
  return memory_size;
}

xy_t L2CPU::get_coordinates(){
    return coordinates;
}

/*
These functions create a "temporary" TlbWindow to do accomplish the task they want to do
This TlbWindow should get cleaned up immediately
*/

void L2CPU::write32(uint64_t addr, uint32_t value) {
    TlbWindow2M temporary_window(fd, coordinates.x, coordinates.y, addr);
    temporary_window.write32(0, value);
}

uint32_t L2CPU::read32(uint64_t addr) {
    TlbWindow2M temporary_window(fd, coordinates.x, coordinates.y, addr);
    return temporary_window.read32(0);
}

/*
While read32/write32 are enough for many one off operations, we need some way to "persistently" map a memory location to a struct
This function creates a TlbWindow on the heap that gets cleaned up when the L2CPU goes out of scope
*/
std::unique_ptr<TlbWindow2M> L2CPU::get_persistent_2M_tlb_window(uint64_t addr){
    return std::unique_ptr<TlbWindow2M>(new TlbWindow2M(fd, coordinates.x, coordinates.y, addr));
}

// Returns the starting address of the L2CPU's memory
uint8_t* L2CPU::get_memory_ptr(){
    return memory+(starting_address-0x4000'0000'0000ULL);
}

L2CPU::~L2CPU() noexcept
{
    munmap(memory, 2ULL<<32);
    close(fd);
}


