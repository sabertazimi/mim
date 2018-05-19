#include <unistd.h>

int main(void) {
    char c;

    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {

    }

    return 0;
}
