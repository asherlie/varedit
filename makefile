CC=gcc
CFLAGS= -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror -Wno-unused-result -std=c99 -O3 -lpthread

all: v 

vmem_parser.o: vmem_parser.c vmem_parser.h
vmem_access.o: vmem_parser.o vmem_access.c vmem_access.h

v: vmem_access.o varedit.c
	$(CC) $(CFLAGS) vmem_access.o vmem_parser.o varedit.c -o v

.PHONY:
shared: varedit.c
	$(CC) varedit.c -lmemcarve -o v $(CFLAGS)
	strip v

clean:
	rm -f v *.o
