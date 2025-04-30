#pragma once 

#include "fmt/format.h"

// TODO: at least timestamp and src.cpp:line should be prepended
#define LOG_INFO(s, ...) fmt::print("[INFO] " s "\n", ##__VA_ARGS__)
#define LOG_ERROR(s, ...) fmt::print("[ERROR] " s "\n", ##__VA_ARGS__)