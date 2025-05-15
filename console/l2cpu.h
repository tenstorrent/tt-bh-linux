// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0

#ifndef L2CPU_H
#define L2CPU_H
#include <vector>
#include <memory>
#include <map>

#include "ioctl.h"
#include "tlb.h"

class L2CPU
{
    int fd;

    int idx;
    uint64_t starting_address, memory_size;

    xy_t coordinates;

public:
    L2CPU(int idx);

    uint64_t get_starting_address();
    uint64_t get_memory_size();

    xy_t get_coordinates();

    std::unique_ptr<TlbWindow2M> get_persistent_2M_tlb_window(uint64_t addr);

    void write32(uint64_t addr, uint32_t value);

    uint32_t read32(uint64_t addr);

    ~L2CPU() noexcept;

};
const extern std::map<int, xy_t> l2cpu_tile_mapping;
const extern std::map<int, uint64_t> l2cpu_starting_address_mapping, l2cpu_memory_size_mapping;
#endif
