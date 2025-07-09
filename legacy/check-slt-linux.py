#!/usr/bin/env python
# from /home/dfustini/slt-linux-release-20240607/

from argparse import ArgumentParser
import sys
import timeit
from l2c_utils import *
import re

from tenstorrent.chip import Chip


def test_fail(l2cpu):
    print(f"TEST FAILED, l2cpu {l2cpu}")
    sys.exit(1)

def parse_args():
    __cmd__ = "python check-slt-linux.py"
    example_usage = f"""
    {__cmd__} --l2cpu 0 .
    """

    parser = ArgumentParser(description=__doc__, epilog=example_usage)
    parser.add_argument("--l2cpu", type=int, default=0, help="Which L2CPU")

    parser.add_argument("--interface", type=str, default="pci", choices =["sim","pci","jtag"], help="Which interface, jtag,pcie:TBD")
    args = parser.parse_args()
    return args

def remove_trailing_zeros(byte_data):
    # Find the index of the last non-0x00 byte
    last_non_zero_index = len(byte_data)
    for i in range(len(byte_data) - 1, -1, -1):
        if byte_data[i] != 0x00:
            last_non_zero_index = i + 1
            break
    # Slice the bytes object to remove trailing 0x00 bytes
    return byte_data[:last_non_zero_index]

def find_patterns_in_string(long_string, patterns):
    # Compile the patterns into a single regex pattern for efficiency
    combined_pattern = re.compile('|'.join(map(re.escape, patterns)))

    # Split the long string into lines
    lines = long_string.splitlines()

    # Dictionary to store lines where patterns are found
    lines_with_patterns = {}

    # Iterate through each line and check for patterns
    for line_num, line in enumerate(lines, start=1):
        if combined_pattern.search(line):
            lines_with_patterns[line_num] = line

    return lines_with_patterns


def main():
    args = parse_args()
    chip = Chip.create_chip("blackhole", args.interface, "direct")




    l2cpu_node = chip.l2cpu(args.l2cpu)

    # We have 64MiB for each log, but this is way more than likely needed
    # Only read part of it back until we have a reason to read back more.
    log_size = 16*1024*1024
    # Read back ramoops log
    kernel_data = l2cpu_node.read_block(0x40012c000000, log_size)
    # Read back userspace log
    user_data = l2cpu_node.read_block(0x400128000000, log_size)

    kernel_data = remove_trailing_zeros(kernel_data)
    user_data = remove_trailing_zeros(user_data)

    with open(f"kernel-output-{args.l2cpu}.log", 'wb') as file:
        file.write(kernel_data)
    with open(f"user-output-{args.l2cpu}.log", 'wb') as file:
        file.write(user_data)

    kernel_ascii = kernel_data.decode('ascii', errors='ignore')
    user_ascii = user_data.decode('ascii', errors='ignore')

    # Print logs to stdout.
    if 1:
        print(f"\n================ Kernel log start {args.l2cpu} ===============\n")
        print(kernel_ascii)
        print(f"\n================ Kernel log end  {args.l2cpu} ===============\n")


        print(f"\n================ User log start {args.l2cpu}  ===============\n")
        print(user_ascii)
        print(f"\n================ User log end  {args.l2cpu} ===============\n")

    # Now check to confirm 'success' messages are there.
    # These don't guarantee a clean test, but their absence constitutes a failure.
    # All success patterns must be found
    kernel_success_patterns = ["Run /sbin/init as init process"]
    user_success_patterns = ["metrics-check: all stressor metrics validated and sane",
                             "skipped: 0", "failed: 0", "metrics untrustworthy: 0",
                             "successful run completed", "4 processors online, 4 processors configured"]

    match = find_patterns_in_string(kernel_ascii, kernel_success_patterns)
    if len(match) != len(kernel_success_patterns):
        print("ERROR: Kernel success patterns not found")
        for line_num, line in match.items():
           print(f"success pattern found: {line_num}: {line}")
        test_fail(args.l2cpu)
    match = find_patterns_in_string(user_ascii, user_success_patterns)
    if len(match) != len(user_success_patterns):
        print("ERROR: User success patterns not found")
        for line_num, line in match.items():
            print(f"success pattern found: {line_num}: {line}")
        test_fail(args.l2cpu)


    # Failure patterns.  Finding any failure patterns results in a failed test.
    kernel_fail_patterns = ["BUG:", "Oops:", "Call Trace", "NULL Pointer"]
    user_fail_patterns = ["Killed", "Segmentation", "segfault", "SIGBUS", "SIGSEGV", "SIGABRT", "signal"]
    user_fail_patterns += kernel_fail_patterns

    match = find_patterns_in_string(kernel_ascii, kernel_fail_patterns)
    if len(match):
        print("ERROR: Kernel fail patterns found")
        for line_num, line in match.items():
           print(f"fail pattern found: {line_num}: {line}")
        test_fail(args.l2cpu)

    match = find_patterns_in_string(user_ascii, user_fail_patterns)
    if len(match):
        print("ERROR: User fail patterns found")
        for line_num, line in match.items():
           print(f"fail pattern found: {line_num}: {line}")
        test_fail(args.l2cpu)

    # Expected log lengths, these are approximate
    # User output varies quite a bit, kernel very little
    exp_kernel_len = 11578
    exp_user_len = 6450
    len_margin_kernel = 20
    len_margin_user = 400
    print(f"Kernel log: {len(kernel_data)} bytes, user log: {len(user_data)} bytes.")
    if len(user_data) > (log_size - 100) or len(kernel_data) > (log_size - 100):
        print("ERROR: The last 100 bytes of user or kernel log are not all 0x00, buffer too small")
        test_fail(args.l2cpu)
    if len(user_data) > (exp_user_len + len_margin_user) or len(user_data) < (exp_user_len - len_margin_user):
        print(f"WARNING: user data length differs from expected length of {exp_user_len}", file=sys.stderr)
    if len(kernel_data) > (exp_kernel_len + len_margin_kernel) or len(kernel_data) < (exp_kernel_len - len_margin_kernel):
        print(f"WARNING: kernel data length differs from expected length of {exp_kernel_len}", file=sys.stderr)

    print(f"TEST PASSED, l2cpu: {args.l2cpu}")



if __name__ == "__main__":
    main()

    sys.exit(0)

