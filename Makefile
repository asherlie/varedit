CPP=g++
C=gcc
all: cpp

cpp:
	$(CPP) varedit.cpp vmem_parser.cpp vmem_access.cpp -std=c++11 -o v -Wall -Wextra -Werror -O2
clean:
	rm -f v
