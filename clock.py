# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import time
import ctypes
from pyluwen import PciChip

PLL4_BASE = 0x80020500 # PLL4 is for L2CPU
PLL_CNTL_1 = 0x4
PLL_CNTL_5 = 0x14

solutions = {
    # freq: [ fbdiv, [postdiv0, postdiv1, postdiv2, [postdiv]3] ]
    200: [128, [15, 15, 15, 15]],
    1750: [140, [1, 1, 1, 1]] 
}

class PLLCNTL5(ctypes.LittleEndianStructure):
    _fields_ = [
        ("postdiv", 4*ctypes.c_uint8),
    ]

    def step(self, chip, target, field):
        one_step = max(min(target - self.postdiv[field], 1), -1)
        while self.postdiv[field] != target:
            self.postdiv[field] += one_step
            chip.axi_write(PLL4_BASE+PLL_CNTL_5, bytearray(self))
            time.sleep(1e-9)

class PLLCNTL1(ctypes.LittleEndianStructure):
    _fields_ = [
        ("refdiv", ctypes.c_uint8),
        ("postdiv", ctypes.c_uint8),
        ("fbdiv", ctypes.c_uint16),
    ]

    def step_fbdiv(self, chip, target):
        one_step = max(min(target - self.fbdiv, 1), -1)
        while self.fbdiv != target:
            self.fbdiv += one_step
            chip.axi_write(PLL4_BASE+PLL_CNTL_1, bytearray(self))
            time.sleep(1e-9)


def set_l2cpu_pll(chip, mhz):
    sol_fbdiv = solutions[mhz][0]
    sol_postdivs = solutions[mhz][1]

    initial_post_dividers = bytearray(4)
    chip.axi_read(PLL4_BASE+PLL_CNTL_5, initial_post_dividers)
    initial_post_dividers = PLLCNTL5.from_buffer(initial_post_dividers)

    initial_fbdiv = bytearray(4)
    chip.axi_read(PLL4_BASE+PLL_CNTL_1, initial_fbdiv)
    initial_fbdiv = PLLCNTL1.from_buffer(initial_fbdiv)

    increasing_postdivs = [ (postdiv_index, target) for postdiv_index, target in enumerate(sol_postdivs) if target > initial_post_dividers.postdiv[postdiv_index] ]
    decreasing_postdivs = [ (postdiv_index, target) for postdiv_index, target in enumerate(sol_postdivs) if target < initial_post_dividers.postdiv[postdiv_index] ]

    for postdiv_index, target in increasing_postdivs:
        initial_post_dividers.step(chip, target, postdiv_index)
    
    initial_fbdiv.step_fbdiv(chip, sol_fbdiv)

    for postdiv_index, target in decreasing_postdivs:
        initial_post_dividers.step(chip, target, postdiv_index)

def main(l2_cpu, mhz):
    chip = PciChip(0)
    print("Setting clock to ", mhz)

    initial_post_dividers = bytearray(4)
    chip.axi_read(PLL4_BASE+PLL_CNTL_5, initial_post_dividers)
    initial_post_dividers = PLLCNTL5.from_buffer(initial_post_dividers)
    # print(list(initial_post_dividers.postdiv))

    initial_fbdiv = bytearray(4)
    chip.axi_read(PLL4_BASE+PLL_CNTL_1, initial_fbdiv)
    initial_fbdiv = PLLCNTL1.from_buffer(initial_fbdiv)
    # print(initial_fbdiv.fbdiv)
    
    set_l2cpu_pll(chip, mhz)

    initial_post_dividers = bytearray(4)
    chip.axi_read(PLL4_BASE+PLL_CNTL_5, initial_post_dividers)
    initial_post_dividers = PLLCNTL5.from_buffer(initial_post_dividers)
    # print(list(initial_post_dividers.postdiv))

    initial_fbdiv = bytearray(4)
    chip.axi_read(PLL4_BASE+PLL_CNTL_1, initial_fbdiv)
    initial_fbdiv = PLLCNTL1.from_buffer(initial_fbdiv)
    # print(initial_fbdiv.fbdiv)
    
if __name__ == "__main__":
    # l2_cpu actually seems unused. Maybe remove?
    l2_cpu = int(sys.argv[1])
    mhz = int(sys.argv[2])
    main(l2_cpu, mhz)
