#pragma once

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace tt {

#if defined(__x86_64__) || defined(__i386__)
static inline __attribute__((always_inline)) void sfence()
{
    _mm_sfence();
}

static inline __attribute__((always_inline)) void lfence()
{
    _mm_lfence();
}

static inline __attribute__((always_inline)) void mfence()
{
    _mm_mfence();
}

#elif defined(__ARM_ARCH)

static inline __attribute__((always_inline)) void sfence()
{
    asm volatile("DMB SY" : : : "memory");
}

static inline __attribute__((always_inline)) void lfence()
{
    asm volatile("DMB LD" : : : "memory");
}

static inline __attribute__((always_inline)) void mfence()
{
    asm volatile("DMB SY" : : : "memory");
}

#elif defined(__riscv)

static inline __attribute__((always_inline)) void sfence()
{
    asm volatile("fence ow, ow" : : : "memory");
}

static inline __attribute__((always_inline)) void lfence()
{
    asm volatile("fence ir, ir" : : : "memory");
}

static inline __attribute__((always_inline)) void mfence()
{
    asm volatile("fence iorw, iorw" : : : "memory");
}

#else
#error "Unsupported architecture"
#endif

} // namespace tt
