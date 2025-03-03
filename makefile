CC=gcc
CFLAGS= -D_GNU_SOURCE -DNO_SUB -Wall -Wextra -Wpedantic -Werror -Wno-unused-result -g3 -lpthread -lreadline

all: v 

vmem_parser.o: vmem_parser.c vmem_parser.h
vmem_access.o: vmem_parser.o vmem_access.c vmem_access.h
ashio.o: ashio.c ashio.h

v: vmem_access.o ashio.o varedit.c
	$(CC) $(CFLAGS) vmem_access.o vmem_parser.o ashio.o varedit.c -o v

.PHONY:
shared: varedit.c ashio.o
	$(CC) varedit.c ashio.o -lmemcarve -o v $(CFLAGS)
	strip v

clean:
	rm -f v *.o
