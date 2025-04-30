#include "utility.hpp"

#include <fstream>

namespace tt {

std::vector<uint8_t> read_file(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    file.seekg(0, std::ios::end);

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size, 0x0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    return buffer;
}

void write_file(const std::string& filename, const void* data, size_t size)
{
    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data), size);
}

} // namespace tt