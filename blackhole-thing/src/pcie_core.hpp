#pragma once

#include "blackhole_pcie.hpp"
#include "tlb_window.hpp"
#include <iostream>

namespace tt {

struct __attribute__((packed)) NocTlbData
{
    uint32_t tlp_type : 5;
    uint32_t ser_np : 1;
    uint32_t ep : 1;
    uint32_t reserved0 : 1;
    uint32_t ns : 1;
    uint32_t ro : 1;
    uint32_t tc : 3;
    uint32_t msg : 8;
    uint32_t dbi : 1;
    uint32_t atu_bypass : 1;
    uint32_t addr : 6;
    uint32_t reserved1 : 3;
};

/**
 * @brief One PCIe core in Blackhole.
 *
 * PCIe cores are on the NOC.  One is typically connected to the Linux host.
 * The other can be in root port mode.  This class will work for either.
 *
 * NOC0 coordinates for PCIe cores are (x=11, y=0) and (x=2, y=0).
 */
class PCIeCore
{
    static constexpr uint64_t SII_A = 0xFFFF'FFFF'F000'0000ULL;
    static constexpr size_t DBI_TLB_INDEX = 20;

    BlackholePciDevice& device;
    const uint32_t our_noc0_x;
    const uint32_t our_noc0_y;

public:
    PCIeCore(BlackholePciDevice& device, uint32_t noc_x, uint32_t noc_y)
        : device(device)
        , our_noc0_x(noc_x)
        , our_noc0_y(noc_y)
    {
    }

    void write_sii32(uint64_t addr, uint32_t value)
    {
        auto tlb = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, SII_A);
        tlb->write32(addr, value);
    }

    uint32_t read_sii32(uint64_t addr)
    {
        auto tlb = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, SII_A);
        return tlb->read32(addr);
    }

    uint32_t read_dbi_register(uint64_t addr)
    {
        const NocTlbData data{.dbi = 1};
        uint64_t access_address = configure_noc_tlb_data(DBI_TLB_INDEX, data);
        auto tlb = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, access_address + addr);
        return tlb->read32(0);
    }

    void write_dbi_register(uint64_t addr, uint32_t value)
    {
        NocTlbData data{};
        data.dbi = 1;

        uint64_t access_address = configure_noc_tlb_data(DBI_TLB_INDEX, data);

        auto tlb = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, access_address + addr);
        tlb->write32(0, value);
    }

    /**
     * @brief Configure a NOC outbound TLB entry.
     *
     * @param index which entry to configure
     * @param data parameters
     * @return address of 58 bit access window
     */
    uint64_t configure_noc_tlb_data(size_t index, const NocTlbData& data)
    {
        const uint64_t config_address = 0x134 + (4 * index);
        const uint64_t access_address = index << 58;
        auto registers = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, SII_A);

        registers->write32(config_address, *reinterpret_cast<const uint32_t*>(&data));

        return access_address;
    }

    void dump_noc_tlb_data(size_t index)
    {
        const uint64_t config_address = 0x134 + (4 * index);
        const uint64_t access_address = index << 58;
        auto registers = device.map_tlb_2M_UC(our_noc0_x, our_noc0_y, SII_A);
        uint32_t data = registers->read32(config_address);
        NocTlbData* tlb_data = reinterpret_cast<NocTlbData*>(&data);
        std::cout << "tlp_type: " << tlb_data->tlp_type << "\n";
        std::cout << "ser_np: " << tlb_data->ser_np << "\n";
        std::cout << "ep: " << tlb_data->ep << "\n";
        std::cout << "ns: " << tlb_data->ns << "\n";
        std::cout << "ro: " << tlb_data->ro << "\n";
        std::cout << "tc: " << tlb_data->tc << "\n";
        std::cout << "msg: " << tlb_data->msg << "\n";
        std::cout << "dbi: " << tlb_data->dbi << "\n";
        std::cout << "atu_bypass: " << tlb_data->atu_bypass << "\n";
        std::cout << "addr: " << tlb_data->addr << "\n";
    }

    void configure_inbound_iatu(size_t region, uint64_t base, uint64_t target, uint32_t limit)
    {
        // Addresses for region 0 config registers.
        // There are 16 regions.  Each region's registers are 0x200 apart.
        static const uint64_t IATU_REGION_CTRL_1_OFF_INBOUND_0 = 0x300100;
        static const uint64_t IATU_REGION_CTRL_2_OFF_INBOUND_0 = 0x300104;
        static const uint64_t IATU_LWR_BASE_ADDR_OFF_INBOUND_0 = 0x300108;
        static const uint64_t IATU_UPPER_BASE_ADDR_OFF_INBOUND_0 = 0x30010C;
        static const uint64_t IATU_LIMIT_ADDR_OFF_INBOUND_0 = 0x300110;
        static const uint64_t IATU_LWR_TARGET_ADDR_OFF_INBOUND_0 = 0x300114;
        static const uint64_t IATU_UPPER_TARGET_ADDR_OFF_INBOUND_0 = 0x300118;
        static const uint64_t IATU_UPPR_LIMIT_ADDR_OFF_INBOUND_0 = 0x300120;

        const uint64_t reg_base = 0x200 * region;
        const uint32_t enable = (limit != 0) ? (1 << 31) : 0;

        if (region >= 16) {
            throw std::runtime_error("Invalid inbound iATU region");
        }

        uint32_t base_lo = (base >> 0x00) & 0xFFFF'FFFF;
        uint32_t base_hi = (base >> 0x20) & 0xFFFF'FFFF;
        uint32_t target_lo = (target >> 0x00) & 0xFFFF'FFFF;
        uint32_t target_hi = (target >> 0x20) & 0xFFFF'FFFF;

        write_dbi_register(reg_base + IATU_REGION_CTRL_1_OFF_INBOUND_0, 0x0);
        write_dbi_register(reg_base + IATU_REGION_CTRL_2_OFF_INBOUND_0, enable);
        write_dbi_register(reg_base + IATU_LWR_BASE_ADDR_OFF_INBOUND_0, base_lo);
        write_dbi_register(reg_base + IATU_UPPER_BASE_ADDR_OFF_INBOUND_0, base_hi);
        write_dbi_register(reg_base + IATU_LIMIT_ADDR_OFF_INBOUND_0, limit);
        write_dbi_register(reg_base + IATU_LWR_TARGET_ADDR_OFF_INBOUND_0, target_lo);
        write_dbi_register(reg_base + IATU_UPPER_TARGET_ADDR_OFF_INBOUND_0, target_hi);

        // TODO: Do we get more than 4GiB/region??  Outbound in GS/WH did not.
        write_dbi_register(base + IATU_UPPR_LIMIT_ADDR_OFF_INBOUND_0, 0x0);
    }
};

} // namespace tt