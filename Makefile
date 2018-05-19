all:
	$(CXX) -Wall -Wextra -Werror -pedantic -std=c++11 mim.cpp -o mim

install:
	./install.sh

run:
	./mim

clean:
	rm -fr mim ncurses

ncurses:
	$(CXX) -Wall -Wextra -Werror -pedantic -std=c++11 ncurses_demo.cpp -lncurses -o ncurses
