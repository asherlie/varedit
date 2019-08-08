CXX=gcc
all: v 
shared:
	$(CXX) varedit.c -lmemcarve -o v -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror -Wno-unused-result -std=c99 -O3 -lpthread
	strip v
v:
	$(CXX) varedit.c vmem_parser.c vmem_access.c -o v -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror -Wno-unused-result -std=c99 -O3 -lpthread
	strip v

clean:
	rm -f v
