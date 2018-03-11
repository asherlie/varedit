CPP=g++
C=gcc
all: v
v:
	$(C) c/varedit.c c/vmem_parser.c c/vmem_access.c -D_GNU_SOURCE -Wall -Wextra -Werror -std=c99 -o v

cpp:
	$(CPP) varedit.cpp vmem_parser.cpp vmem_access.cpp -std=c++11 -o v -Wall -Wextra -Werror
clean:
	rm -f v
