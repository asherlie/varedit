CXX=g++
all: v 
v:
	$(CXX) varedit.cpp vmem_parser.cpp vmem_access.cpp -std=c++11 -o v -Wall -Wextra -Werror

clean:
	rm -f v
