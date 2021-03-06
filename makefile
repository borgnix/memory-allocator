
DEBUG ?= 0
CFLAGS=-c -g -Wall -Wextra -fPIC -DDEBUG=$(DEBUG)
APP=/bin/ls

lib: bin/libheap.so

run: bin/libheap.so
	LD_PRELOAD=$< $(APP)

gdb: bin/libheap.so
	gdb -ex "set exec-wrapper env LD_PRELOAD=$<" --args $(APP)

clean:
	rm ./bin/*

bin/libheap.so: bin/heap.o
	$(CC) -shared -g $< -o $@

bin/heap.o: memory.c memory.h
	$(CC) $(CFLAGS) $< -o $@

.PHONY: lib run gdb clean

