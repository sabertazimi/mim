all:
	$(CC) -Wall -Wextra -Werror -pedantic -std=c99 mim.c -o mim

install:
	./install.sh

run:
	./mim

clean:
	rm -fr mim ncurses

ncurses:
	$(CC) -Wall -Wextra -Werror -pedantic -std=c99 ncurses_demo.c -lncurses -o ncurses
