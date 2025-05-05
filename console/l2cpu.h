#include <cstdint>
#include <map>

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

extern std::map<int, xy_t> l2cpu_tile_mapping;
extern std::map<int, uint64_t> l2cpu_starting_address_mapping;
