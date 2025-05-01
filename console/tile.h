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

    ~Tile() noexcept;

};
