CC = gcc
CXX = g++
CFLAGS= -Wall -O3
CXXFLAGS= -Wall -O3

default: all
all: dicflate dicstd diclz4

test: check
check: all
	./test/all.sh

dicflate: src/dicflate.c
	$(CC) $(CFLAGS) src/dicflate.c -lz -o dicflate

dicstd: src/dicstd.c vendor/zstd.c vendor/zstd.h vendor/zstd_errors.h
	$(CC) $(CFLAGS) -Ivendor src/dicstd.c vendor/zstd.c -o dicstd

diclz4: src/diclz4.c vendor/lz4.c vendor/lz4.h vendor/lz4hc.c vendor/lz4hc.h \
         vendor/lz4frame.c vendor/lz4frame.h vendor/lz4frame_static.h \
         vendor/xxhash.c vendor/xxhash.h
	$(CC) $(CFLAGS) -Ivendor src/diclz4.c vendor/lz4.c vendor/lz4hc.c vendor/lz4frame.c vendor/xxhash.c -o diclz4

clean:
	-rm -f dicflate dicstd diclz4 bench/bench

bench: bench/bench
	bash ./bench/bench-process.sh

bench/bench: bench/bench.c $(wildcard vendor/*.c)
	$(CC) $(CFLAGS) -Ivendor bench/bench.c vendor/zstd.c vendor/lz4.c vendor/lz4hc.c vendor/xxhash.c -lz -o bench/bench

.PHONY: bench
