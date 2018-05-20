all:
	$(CXX) -Wall -Wextra -Werror -pedantic -std=c++14 mim.cpp -o mim
	mkdir -p ~/.bin
	rm -fr ~/.bin/mim
	mv ./mim ~/.bin

run:
	mim

