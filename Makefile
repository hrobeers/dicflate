CC = gcc
CXX = g++
CFLAGS= -Wall -O3
CXXFLAGS= -Wall -O3

default: all
all: clean dicflate dicstd

test: check
check: all
	./test/all.sh

dicflate:
	$(CC) $(CFLAGS) src/dicflate.c -lz -o dicflate

dicstd: src/dicstd.c vendor/zstd.c vendor/zstd.h vendor/zstd_errors.h
	$(CC) $(CFLAGS) -Ivendor src/dicstd.c vendor/zstd.c -o dicstd

clean:
	-rm -f dicflate dicstd
