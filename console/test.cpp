#include <random>
#include <cassert>

#include "l2cpu.h"


std::random_device rd;
std::mt19937 gen(rd());

/*
Test that checks that any address read between 0x4000'3000'0000ULL and 0x4001'3000'0000ULL
for L2CPU 2 and 3 are the same, cause they share a memory tile
*/
void TestL2CPU23SharedMemoryTile(){
    L2CPU two(2), three(3);
    std::uniform_int_distribution<uint64_t> distribution(0, 4ULL * 1024*1024*1024);
    uint64_t starting_address = 0x4000'3000'0000ULL;

    for (int i=0; i< 100; i++){
        uint64_t random_address = distribution(gen);
        assert(two.read32(starting_address + random_address) == three.read32(starting_address + random_address));
    }
}

/*
Checks that NOC Node ID register of L2CPU matches expected value
*/
void TestL2CPUNocNodeID(){
    uint64_t NODEID = 0xfffff7fefff56000 + 0x44;
    for (int i=0; i<4; i++){
        L2CPU l2cpu(i);
        xy_t coordinates = l2cpu.get_coordinates();
        uint32_t nodeid = l2cpu.read32(NODEID);
        assert((nodeid & ((1 << 6)-1)) == coordinates.x); // Last 6 bits for Noc location X
        assert(((nodeid >> 6) & ((1 << 6)-1)) == coordinates.y); // Next 6 bits for Noc location Y
    }
}

int main(){
    TestL2CPU23SharedMemoryTile();
    TestL2CPUNocNodeID();
    return 0;
}