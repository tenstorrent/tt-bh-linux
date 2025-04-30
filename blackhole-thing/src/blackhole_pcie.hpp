#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "tlb_window.hpp"

namespace tt {

struct PciDeviceInfo
{
	uint16_t vendor_id;
	uint16_t device_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;
};

// namespaced to avoid ambiguity (X280 also has 2 MiB TLB windows)
namespace pcie {
struct Tlb2M;
struct Tlb4G;
} // namespace pcie

class BlackholePciDevice
{
    const int fd;
    const PciDeviceInfo info;
    const size_t bar0_size;
    const size_t bar2_size;
    const size_t bar4_size;

    uint8_t* bar0;
    uint8_t* bar2;
    uint8_t* bar4;

    // TODO: There is opportunity for more sophisticated management scheme.
    // The kernel driver should be responsible for managing them - an example
    // use case is virtual UART to L2CPU, which consumes one inbound TLB window.
    // There's a userspace console program to access the virtual UART, but it
    // dosn't know how to coordinate with other userspace programs that might
    // steal its TLB window.  Moving TLB window management to the kernel driver
    // eliminates this category of problem.
    std::mutex tlb_mutex;
    std::vector<size_t> free_tlb_indices_2M_WC;
    std::vector<size_t> free_tlb_indices_2M_UC;
    std::vector<size_t> free_tlb_indices_4G;

public:
    /**
     * @brief Construct a new BlackholePciDevice object.
     *
     * Opens the device file, reads the device info, and maps the BARs.
     * TODO: We don't do any device enumeration yet.
     *
     * @param path  e.g. /dev/tenstorrent/0
     */
    BlackholePciDevice(const std::string& path);

    /**
     * @brief Destroy the BlackholePciDevice object.
     *
     * Unmaps the BARs and closes the device file.
     */
    ~BlackholePciDevice();

    /**
     * @brief Information about the PCIe device as reported by TT-KMD.
     *
     * @return const PciDeviceInfo&
     */
    const PciDeviceInfo& get_info() const { return info; }

    /**
     * @brief Map a window of memory to an (x, y, address) location in the chip.
     *
     * If the requested address does not correspond to the bottom of the window,
     * the base address held by the returned TlbWindow object will be adjusted
     * upward, thus reducing the apparent size of the window.  The alternative
     * is for the caller to worry about it, but this turned out to be error
     * prone and annoying.
     *
     * @param x NOC0 coordinate of tile
     * @param y NOC0 coordinate of tile
     * @param address within tile
     * @return std::unique_ptr<TlbWindow> must not outlive BlackholePciDevice!
     */
    std::unique_ptr<TlbWindow> map_tlb_2M_WC(uint32_t x, uint32_t y, uint64_t address);
    std::unique_ptr<TlbWindow> map_tlb_2M_UC(uint32_t x, uint32_t y, uint64_t address);
    std::unique_ptr<TlbWindow> map_tlb_4G(uint32_t x, uint32_t y, uint64_t address);
    // TODO: the interface above is too simplistic.  TLB configuration supports
    // broadcast, has ordering bits, NOC bits, and so on.  Live with this for
    // now, but consider how to expose these features while maintaining an
    // ergonomic interface.
    //
    // There is also the question of how and where to translate coordinates.  I
    // don't have an answer to that other than NOT HERE!  That is a problem for
    // an abstraction layer higher up, where it is easier to test the coordinate
    // transformation mechanism(s) without needing any hardware.

    /**
     * @brief Map user-allocated memory for DMA access.
     *
     * @param buffer constraint: must be page-aligned
     * @param size constraint: must be a multiple of the page size
     * @return uint64_t IOVA address for DMA access
     */
    uint64_t map_for_dma(const void* buffer, size_t size);

    /**
     * @brief Unmap user-allocated memory from DMA access.
     *
     * KMD does not implement this, but it will unmap when the process exits.
     *
     * @param iova ignored
     */
    void unmap_for_dma(uint64_t iova) { /* TODO */ }

    /**
     * @brief Low-level access to the PCIe BARs.
     *
     * BAR0: 2 MiB TLB windows (x202) followed by registers, mixed WC/UC mapping
     * BAR2: PCIe registers
     * BAR4: 4 GiB TLB windows (x8)
     *
     * @return uint8_t*
     */
    uint8_t* get_bar0() { return bar0; }
    uint8_t* get_bar2() { return bar2; }
    uint8_t* get_bar4() { return bar4; }

    void configure_iatu_region(size_t region, uint64_t base, uint64_t target, size_t size);
    void dump_iatu_region(size_t region);

private:
    /**
     * @brief Inbound PCIe TLB configuration registers.
     *
     * Note: TLB indices are not separate for 2 MiB vs 4 GiB windows; the index
     * ifor the bottom 4 GiB window starts after the last 2 MiB window index.
     *
     * @param tlb_index which TLB entry to access
     * @return configuration registers for the TLB entry
     */
    uint32_t* tlb_config_registers(size_t tlb_index);

    /**
     * @brief Program inbound PCIe TLB configuration registers.
     *
     * @param tlb_index which TLB entry to program
     * @param tlb_config parameters
     */
    void write_tlb_config_2M(size_t tlb_index, const pcie::Tlb2M& tlb_config);
    void write_tlb_config_4G(size_t tlb_index, const pcie::Tlb4G& tlb_config);

    /**
     * @brief Read inbound PCIe TLB configuration registers.
     *
     * @param tlb_index which TLB to read
     * @return parameters
     */
    pcie::Tlb2M read_tlb_config_2M(size_t tlb_index);
    pcie::Tlb4G read_tlb_config_4G(size_t tlb_index);
};

} // namespace tt
