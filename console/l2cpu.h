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
#include "tile.h"

class L2CPU : public Tile
{
    int idx;
    uint64_t starting_address;

    void set_coordinates() override;

public:
    L2CPU(int idx);

    uint8_t* get_persistent_2M_tlb_window_offset(uint64_t addr);
    
    void write32_offset(uint64_t addr, uint32_t value);

    uint32_t read32_offset(uint64_t addr);
};
