all: ioctl.h
	g++ -O2 test.cpp l2cpu.cpp tlb.cpp -o test
	g++ -O2 tt-bh-linux.cpp network.hpp console.hpp l2cpu.cpp tlb.cpp -pthread -Wall -lvdeslirp -lslirp -o tt-bh-linux

ioctl.h:
	@TT_VERSION=$$(modinfo tenstorrent | grep -i ^version | awk '{print $$2}'); \
	if [ -z $$TT_VERSION ]; then \
		echo "Tenstorrent driver not found"; \
		exit 1; \
	else \
		echo "Found Tenstorrent driver version: $$TT_VERSION"; \
		cp /var/lib/dkms/tenstorrent/$$TT_VERSION/source/ioctl.h ./; \
		echo "Copied ioctl.h to current directory"; \
	fi

clean:
	rm -f ioctl.h test tt-bh-linux
