#include "blackhole_pcie.hpp"
#include <chrono>
#include <csignal>
#include <cstring>
#include <fmt/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

using le64_t = uint64_t;
using le32_t = uint32_t;
using namespace tt;

static const uint64_t VIRTUAL_UART_MAGIC = 0x5649525455415254ULL;
static constexpr size_t L2CPU_X = 8;
static constexpr size_t L2CPU_Y = 3;

// Must match what is in OpenSBI.  tx/rx is from OpenSBI/X280 perspective.
#define BUFFER_SIZE 0x1000
struct __attribute__((packed, aligned(4))) queues {
    volatile le64_t magic;
    volatile char tx_buf[BUFFER_SIZE];
    volatile char rx_buf[BUFFER_SIZE];
    volatile le32_t tx_head;
    volatile le32_t tx_tail;
    volatile le32_t rx_head;
    volatile le32_t rx_tail;
};

static inline bool can_push(volatile queues* q)
{
    std::atomic_thread_fence(std::memory_order_acquire);
    // No reads/writes in the current thread can be reordered before this load
    auto head = q->rx_head % BUFFER_SIZE;
    auto tail = q->rx_tail % BUFFER_SIZE;
    return (head + 1) % BUFFER_SIZE != tail;
}

static inline bool can_pop(volatile queues* q)
{
    std::atomic_thread_fence(std::memory_order_acquire);
    auto head = q->tx_head % BUFFER_SIZE;
    auto tail = q->tx_tail % BUFFER_SIZE;
    return head != tail;
}

static inline void push_char(volatile queues* q, char c)
{
    while (!can_push(q));
    q->rx_buf[q->rx_head % BUFFER_SIZE] = c;
    std::atomic_thread_fence(std::memory_order_release);
    // No reads/writes in the current thread can be reordered after this store
    q->rx_head = (q->rx_head + 1) % BUFFER_SIZE;
}

static inline char pop_char(volatile queues* q)
{
    while (!can_pop(q));
    char c = q->tx_buf[q->tx_tail % BUFFER_SIZE];
    std::atomic_thread_fence(std::memory_order_release);
    // No reads/writes in the current thread can be reordered after this store
    q->tx_tail = (q->tx_tail + 1) % BUFFER_SIZE;
    return c;
}

bool running = true;

class TerminalRawMode
{
    struct termios orig_termios;
public:
    TerminalRawMode()
    {
        // Get the current terminal settings
        tcgetattr(STDIN_FILENO, &orig_termios);
        struct termios raw = orig_termios;

        // Modify the terminal settings for raw mode
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);

        // Apply the raw mode settings
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    ~TerminalRawMode()
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }
};

struct NocCoordinate {
    size_t x;
    size_t y;
};

static constexpr uint64_t X280_DDR_BASE = 0x4000'3000'0000ULL;
static constexpr uint64_t OPENSBI_DEBUG_PTR = 0x80;
static constexpr std::array<NocCoordinate, 4> L2CPU_COORDINATES = {
    NocCoordinate{8, 3},
    NocCoordinate{8, 4},
    NocCoordinate{8, 5},
    NocCoordinate{8, 6},
};
static constexpr uint8_t EYE_CATCHER[] = "OSBIdbug";


using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

struct debug_descriptor {
    u8 eye_catcher[8];
    u32 version;
    u64 virtuart_base;
};

uint64_t find_uart(BlackholePciDevice& device, uint32_t x, uint32_t y)
{
    // 1. Look at the bottom of the X280 DRAM for the debug structure.
    auto window = device.map_tlb_2M_UC(x, y, X280_DDR_BASE);
    auto debug_descriptor = window->read32(OPENSBI_DEBUG_PTR);
    fmt::print("L2CPU[{}, {}] debug descriptor: {:#x}\n", x, y, debug_descriptor);
    window = device.map_tlb_2M_UC(x, y, X280_DDR_BASE + debug_descriptor);

    const auto* desc = window->as<volatile struct debug_descriptor*>();
    for (size_t i = 0; i < 8; i++) {
        if (desc->eye_catcher[i] != EYE_CATCHER[i]) {
            fmt::print("L2CPU[{}, {}] debug descriptor eye catcher mismatch\n", x, y);
            std::exit(0);
            return 0;
        }
    }


    return desc->virtuart_base;
}

int uart_loop() {
    BlackholePciDevice device("/dev/tenstorrent/0");
    uint64_t uart_base = find_uart(device, L2CPU_X, L2CPU_Y);
    fmt::print("{:#x}\n", uart_base);

    auto window = device.map_tlb_2M_UC(L2CPU_X, L2CPU_Y, uart_base);
    volatile queues* q = window->as<volatile queues*>();

    TerminalRawMode raw_mode;
    bool ctrl_a_pressed = false;

    while (running) {
        if (le64toh(q->magic) != VIRTUAL_UART_MAGIC) {
            return -EAGAIN;
        }

        // Check for input from the terminal
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 1;

        int retval = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (retval > 0) {
            char input;
            if (read(STDIN_FILENO, &input, 1) > 0) {
                if (ctrl_a_pressed) {
                    if (input == 'x') {
                        running = false;
                        fmt::print("\n\n");
                        break;
                    }
                    ctrl_a_pressed = false;
                } else if (input == 1) {  // Ctrl-A
                    ctrl_a_pressed = true;
                } else {
                    push_char(q, input);
                }
            }
        }

        // Check for output from the device
        if (can_pop(q)) {
            char c = pop_char(q);
            fmt::print("{}", c);
            std::fflush(stdout);
        }
    }

    return 0;
}

int main()
{
    fmt::print("Press Ctrl-A x to exit.\n\n");
    while (running) {
        try {
            int r = uart_loop();
            if (r == -EAGAIN) {
                fmt::print("Error (UART vanished) -- was the chip reset?  Retrying...\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                return r;
            }
        } catch (const std::exception& e) {
            fmt::print("Error ({}) -- was the chip reset?  Retrying...\n", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    fmt::print("Exiting...\n");
    return 0;
}