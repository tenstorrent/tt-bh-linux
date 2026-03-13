#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import sys
import subprocess
from pyluwen import PciChip

l2cpu_gddr_enable_bit_mapping = {
    0: 5, 1: 6, 2: 7, 3: 7
}

def get_running_l2cpus():
    """Check which L2CPUs have running tt-bh-linux processes"""
    try:
        result = subprocess.run(['ps', 'aux'], capture_output=True, text=True)
        running = set()
        for line in result.stdout.splitlines():
            if 'tt-bh-linux' in line and '--l2cpu' in line:
                parts = line.split('--l2cpu')
                if len(parts) > 1:
                    l2cpu_num = parts[1].strip().split()[0]
                    running.add(int(l2cpu_num))
        return running
    except:
        return set()

def main():
    chip = PciChip(0)
    telemetry = chip.get_telemetry()
    enabled_l2cpu = telemetry.enabled_l2cpu
    enabled_gddr = telemetry.enabled_gddr
    running_l2cpus = get_running_l2cpus()

    print("L2CPU Status:")
    for l2cpu in range(4):
        l2cpu_enabled = (enabled_l2cpu >> l2cpu) & 1
        gddr_bit = l2cpu_gddr_enable_bit_mapping[l2cpu]
        gddr_enabled = (enabled_gddr >> gddr_bit) & 1
        is_available = l2cpu_enabled and gddr_enabled
        is_running = l2cpu in running_l2cpus

        if not is_available:
            status = "Harvested"
        elif is_running:
            status = "Running"
        else:
            status = "Available"

        print(f"  L2CPU {l2cpu}: {status}")

if __name__ == "__main__":
    main()
