// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include "console.hpp"
#include "network.hpp"

std::atomic<bool> exit_thread_flag{false};

void console_main(int l2cpu){
    printf("Press Ctrl-A x to exit.\n\n");
    while (!exit_thread_flag) {
        try {
            int r = uart_loop(l2cpu, exit_thread_flag);
            if (r == -EAGAIN) {
                printf("Error (UART vanished) -- was the chip reset?  Retrying...\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                exit_thread_flag = true;
                return;
            }
        } catch (const std::exception& e) {
            printf("Error (%s) -- was the chip reset?  Retrying...\n", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void network_main(int l2cpu){
    while (!exit_thread_flag){
      network_loop(l2cpu, exit_thread_flag);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char **argv){
    int l2cpu=0;
    const char* const short_opts = "l:h";
    const option long_opts[] = {
            {"l2cpu", required_argument, nullptr, 'l'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, no_argument, nullptr, 0}
    };

    while (true)
    {
        const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);

        if (-1 == opt)
            break;

        switch (opt)
        {
        case 'l':
            l2cpu = std::stoi(optarg);
            break;

        case 'h': // -h or --help
        case '?': // Unrecognized option
        default:
            std::cout <<
            "--l2cpu <l>:         L2CPU to attach to\n"
            "--help:              Show help\n";
            exit(1);
        }
    }

    if (l2cpu < 0 || l2cpu > 3){
        std::cerr<<"l2cpu must be one of 0,1,2,3"<<"\n";
        exit(1);
    }

  std::thread console_thread(console_main, l2cpu);
  //std::thread network_thread(network_main, l2cpu);
  console_thread.join();
  //network_thread.join();
}
