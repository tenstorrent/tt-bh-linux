#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <thread>
#include <chrono>
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "l2cpu.h"

using le64_t = uint64_t;
using le32_t = uint32_t;

static const uint64_t VIRTUAL_UART_MAGIC = 0x5649525455415254ULL;

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


static constexpr uint64_t OPENSBI_DEBUG_PTR = 0x80;
static constexpr uint8_t EYE_CATCHER[] = "OSBIdbug";


using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

struct debug_descriptor {
    u8 eye_catcher[8];
    u32 version;
    u64 virtuart_base;
};

uint64_t find_uart(L2CPU& l2cpu)
{
    // 1. Look at the bottom of the X280 DRAM for the debug structure.
    uint32_t debug_descriptor = l2cpu.read32_offset(OPENSBI_DEBUG_PTR);
    auto tile = l2cpu.get_coordinates();
    printf("L2CPU[%d, %d] debug descriptor: %x\n", tile.x, tile.y, debug_descriptor);
    
    struct debug_descriptor *desc = reinterpret_cast<struct debug_descriptor*>(l2cpu.get_persistent_2M_tlb_window_offset(debug_descriptor));
    
    for (size_t i = 0; i < 8; i++) {
        if (desc->eye_catcher[i] != EYE_CATCHER[i]) {
            printf("L2CPU[%d, %d] debug descriptor eye catcher mismatch\n", tile.x, tile.y);
            std::exit(0);
            return 0;
        }
    }

    return desc->virtuart_base;
}

int uart_loop(int l2cpu_idx) {
    
    L2CPU l2cpu(l2cpu_idx);
    uint64_t uart_base = find_uart(l2cpu);
    printf("%lx", uart_base);
    
    volatile queues* q = reinterpret_cast<volatile queues*>(l2cpu.get_persistent_2M_tlb_window(uart_base));

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
                        printf("\n\n");
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
            printf("%c", c);
            std::fflush(stdout);
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int l2cpu;
    if (argc < 2){
        l2cpu = 0;
    } else {
        l2cpu = std::atoi(argv[1]);
    }
    if (l2cpu < 0 || l2cpu > 3){
        std::cerr<<"l2cpu must be one of 0,1,2,3"<<"\n";
        return 1;
    }
    
    printf("Press Ctrl-A x to exit.\n\n");
    while (running) {
        try {
            int r = uart_loop(l2cpu);
            if (r == -EAGAIN) {
                printf("Error (UART vanished) -- was the chip reset?  Retrying...\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                return r;
            }
        } catch (const std::exception& e) {
            printf("Error (%s) -- was the chip reset?  Retrying...\n", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    printf("Exiting...\n");
    return 0;
}