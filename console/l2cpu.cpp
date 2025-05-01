// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <iostream>
#include <algorithm>
#include <array>
#include <memory>
#include <random>
#include <map>
#include <vector>

#include "l2cpu.h"

/* 
I believe that these values should never need changing
*/
std::map<int, xy_t> l2cpu_tile_mapping {
    {0, xy_t{x: 8, y: 3}},
    {1, xy_t{x: 8, y: 9}},
    {2, xy_t{x: 8, y: 5}},
    {3, xy_t{x: 8, y: 7}},
};

/*
But not sure about these values
The user should probably have a way to override these values 
if they're booting the L2CPU with a remote memory tile

However these values are only used in the "relative" version of read32/write32
so maybe it doesn't really matter. A poweruser can use the "absolute" versions of those functions
*/ 
std::map<int, uint64_t> l2cpu_starting_address_mapping {
    {0, 0x4000'3000'0000ULL},
    {1, 0x4000'3000'0000ULL},
    {2, 0x4000'3000'0000ULL},
    {3, 0x4000'b000'0000ULL},
};


L2CPU::L2CPU(int idx)
    : idx(idx)
{
    starting_address = l2cpu_starting_address_mapping[idx];
    set_coordinates();
}

void L2CPU::set_coordinates(){
    coordinates = l2cpu_tile_mapping[idx];
}

/*
For read32 and write32, we offer an additional function
read32_offset/write32_offset accept a relative address value that is offset from the L2CPU's starting address
*/

void L2CPU::write32_offset(uint64_t addr, uint32_t value) {
    Tile::write32(starting_address + addr, value);
}

uint32_t L2CPU::read32_offset(uint64_t addr) {
    return Tile::read32(starting_address + addr);
}

/*
This function similarly has variant that accepts relative addresses
*/
uint8_t* L2CPU::get_persistent_2M_tlb_window_offset(uint64_t addr){
    return Tile::get_persistent_2M_tlb_window(starting_address + addr);
}
