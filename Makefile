CXX=gcc
all: v 
v:
	$(CXX) varedit.c vmem_parser.c vmem_access.c -o v -D_GNU_SOURCE -Wall -Wextra -Werror -std=c99 -O2

clean:
	rm -f v
