all:
	g++ -O2 test.cpp l2cpu.cpp tlb.cpp -o test
	g++ -O2 tt-bh-linux.cpp network.hpp console.hpp l2cpu.cpp tlb.cpp -pthread -Wall -lvdeslirp -lslirp -o tt-bh-linux

clean:
	rm -f test tt-bh-linux
