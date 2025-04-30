#pragma once

#include "logger.hpp"
#include "tlb_window.hpp"
#include <chrono>
#include <thread>

namespace tt {

// NB: I wrote this for GS/WH to poke the ARC of those chips when connected to
// Blackhole via Blackhole root port PCIe.  I don't know if the BAR0 layout is
// the same for Blackhole, but I'm guessing it is not.  So this class will need
// to be at least examined and possibly modified to work with Blackhole.

class ArcFirmwareMessenger
{
private:
    TlbWindow& bar0;

    static constexpr uint32_t SCRATCH_REG(int n)
    {
        return 0x60 + (n) * sizeof(uint32_t);
    }
    static constexpr uint32_t ARC_MISC_CNTL_REG = 0x100;
    static constexpr uint32_t GS_FW_MESSAGE_PRESENT = 0xAA00;
    static constexpr uint32_t ARC_MISC_CNTL_IRQ0_MASK = 1 << 16;
    static constexpr uint64_t RESET_UNIT_REGS = 0x1FF30000;

    int arc_msg_poll_completion(uint32_t msg_reg_addr, uint32_t msg_code, uint32_t timeout_us, uint16_t* exit_code)
    {
        uint32_t poll_period_us = std::max(10u, timeout_us / 100);
        auto end_time = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_us);

        while (true) {
            uint32_t read_val = bar0.read32(msg_reg_addr);

            if ((read_val & 0xffff) == msg_code) {
                if (exit_code)
                    *exit_code = read_val >> 16;
                return 0;
            }

            if (std::chrono::steady_clock::now() > end_time) {
                LOG_ERROR("FW message timeout: 0x{:x}", msg_code);
                return -1;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(poll_period_us));
        }
    }

public:
    ArcFirmwareMessenger(TlbWindow& gs_or_wh_bar0)
        : bar0(gs_or_wh_bar0)
    {
    }

    bool send_arc_fw_message_with_args(uint8_t message_id, uint16_t arg0, uint16_t arg1, uint32_t timeout_us,
                                       uint16_t* exit_code)
    {
        uint32_t base_addr = RESET_UNIT_REGS;
        uint32_t args_reg_addr = base_addr + SCRATCH_REG(3);
        uint32_t message_reg_addr = base_addr + SCRATCH_REG(5);
        uint32_t arc_misc_cntl_reg_addr = base_addr + ARC_MISC_CNTL_REG;
        uint32_t args = arg0 | ((uint32_t)arg1 << 16);

        bar0.write32(args_reg_addr, args);
        bar0.write32(message_reg_addr, GS_FW_MESSAGE_PRESENT | message_id);

        // Trigger IRQ to ARC
        uint32_t arc_misc_cntl = bar0.read32(arc_misc_cntl_reg_addr);
        bar0.write32(arc_misc_cntl_reg_addr, arc_misc_cntl | ARC_MISC_CNTL_IRQ0_MASK);

        return arc_msg_poll_completion(message_reg_addr, message_id, timeout_us, exit_code) >= 0;
    }
};

} // namespace tt