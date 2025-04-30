#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace tt {

/**
 * @brief Represents a window of memory mapped to a location in the chip.
 */
class TlbWindow
{
public:
    TlbWindow(uint8_t* base, size_t size)
        : base(base)
        , window_size(size)
    {
    }

    virtual ~TlbWindow() = default;

    size_t size() const
    {
        return window_size;
    }

    template <typename T> T as()
    {
        return reinterpret_cast<T>(base);
    }

    void write8(uint64_t address, uint8_t value)
    {
        write<uint8_t>(address, value);
    }

    void write16(uint64_t address, uint16_t value)
    {
        write<uint16_t>(address, value);
    }

    void write32(uint64_t address, uint32_t value)
    {
        write<uint32_t>(address, value);
    }

    void write64(uint64_t address, uint64_t value)
    {
        write<uint64_t>(address, value);
    }

    uint8_t read8(uint64_t address)
    {
        return read<uint8_t>(address);
    }

    uint16_t read16(uint64_t address)
    {
        return read<uint16_t>(address);
    }

    uint32_t read32(uint64_t address)
    {
        return read<uint32_t>(address);
    }

    uint64_t read64(uint64_t address)
    {
        return read<uint64_t>(address);
    }

    virtual void write_block(uint64_t address, const void* buffer, size_t size) = 0;
    virtual void read_block(uint64_t address, void* buffer, size_t size) = 0;

private:
    template <class T> void write(uint64_t address, T value)
    {
        auto dst = reinterpret_cast<uintptr_t>(base) + address;

        if (address > window_size) {
            throw std::out_of_range("Out of bounds access");
        }

        if (alignof(T) > 1 && (dst & alignof(T) - 1)) {
            throw std::runtime_error("Bad alignment");
        }
        *reinterpret_cast<volatile T*>(dst) = value;
    }

    template <class T> T read(uint64_t address)
    {
        auto src = reinterpret_cast<const uintptr_t>(base) + address;

        if (address > window_size) {
            throw std::out_of_range("Out of bounds access");
        }

        if (alignof(T) > 1 && (src & alignof(T) - 1)) {
            throw std::runtime_error("Bad alignment");
        }

        return *reinterpret_cast<const volatile T*>(src);
    }

protected:
    uint8_t* base;
    size_t window_size;
};

} // namespace tt
