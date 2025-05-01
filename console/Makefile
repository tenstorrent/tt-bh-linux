all: ioctl.h
	g++ -O2 console.cpp l2cpu.cpp tile.cpp tlb.cpp -o console
	g++ -O2 test.cpp l2cpu.cpp tile.cpp tlb.cpp -o test

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
	rm -f ioctl.h console test
