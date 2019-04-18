CC = gcc
CXX = g++
CFLAGS= -Wall -O3
CXXFLAGS= -Wall -O3

default: all
all: clean dicflate

test: check
check: all
	./test/all.sh

dicflate:
	$(CC) $(CCFLAGS) src/dicflate.c -lz -o dicflate

clean:
	-rm -f dicflate
