CXX=gcc
all: v 
v:
	$(CXX) varedit.c vmem_parser.c vmem_access.c -o v -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror -std=c99 -O3
	strip v

clean:
	rm -f v
