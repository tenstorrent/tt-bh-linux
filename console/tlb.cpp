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



TlbHandle::TlbHandle(int fd, size_t size, const tenstorrent_noc_tlb_config &config)
    : fd(fd)
    , tlb_size(size)
{
    tenstorrent_allocate_tlb allocate_tlb{};
    allocate_tlb.in.size = size;
    if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &allocate_tlb) != 0)
        std::cerr<<"Failed to allocate TLB";

    tlb_id = allocate_tlb.out.id;

    tenstorrent_configure_tlb configure_tlb{};
    configure_tlb.in.id = tlb_id;
    configure_tlb.in.config = config;
    if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0)
        std::cerr<<"Failed to configure TLB";

    void *mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, allocate_tlb.out.mmap_offset_uc);
    if (mem == MAP_FAILED) {
        tenstorrent_free_tlb free_tlb{};
        free_tlb.in.id = tlb_id;
        ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
        std::cerr<<"Failed to map TLB";
    }

    tlb_base = reinterpret_cast<uint8_t *>(mem);
}

uint8_t* TlbHandle::data() { return tlb_base; }
size_t TlbHandle::size() const { return tlb_size; }

TlbHandle::~TlbHandle() noexcept
{
    tenstorrent_free_tlb free_tlb{};
    free_tlb.in.id = tlb_id;
    std::cout<<"Free TLB "<<tlb_id<<"\n";

    munmap(tlb_base, tlb_size);
    ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
}


