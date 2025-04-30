#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace tt {

void write_file(const std::string& filename, const void* data, size_t size);
std::vector<uint8_t> read_file(const std::string& filename);

template <typename T> T random_integer()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<T> dis(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    return dis(gen);
}

template <typename T> std::vector<T> random_vec(size_t num_elements)
{
    std::vector<T> vec(num_elements);
    std::generate(vec.begin(), vec.end(), random_integer<T>);
    return vec;
}

class Timer
{
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;

public:
    Timer()
        : start_time(std::chrono::high_resolution_clock::now())
    {
    }

    uint64_t elapsed_ns() const
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    }

    uint64_t elapsed_us() const
    {
        return elapsed_ns() / 1'000;
    }

    uint64_t elapsed_ms() const
    {
        return elapsed_ns() / 1'000'000;
    }

    uint64_t elapsed_s() const
    {
        return elapsed_ns() / 1'000'000'000;
    }

    void reset()
    {
        start_time = std::chrono::high_resolution_clock::now();
    }
};

} // namespace tt