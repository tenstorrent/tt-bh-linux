CXXFLAGS := -Wall -O2 -MMD -MP
LDLIBS := -lvdeslirp -lslirp
LINK.o := $(LINK.cc)

.PHONY: all clean

all: test tt-bh-linux

test: test.o l2cpu.o tlb.o

tt-bh-linux: tt-bh-linux.o l2cpu.o tlb.o

-include *.d

clean:
	$(RM) test tt-bh-linux *.o *.d
