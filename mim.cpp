#include <termios.h>
#include <unistd.h>

class Mim {
    public:
        Mim(void) {
            this->enableRawMode();
        }
        ~Mim(void) {
            this->disableRawMode();
        }

       void read_loop(void) {
            char c;

            while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {

            }
        }

    private:
        struct termios orig_termios;

        void enableRawMode(void) {
            tcgetattr(STDIN_FILENO, &orig_termios);
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON); // turn off canonical mode
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }

        void disableRawMode(void) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        }
};

int main(void) {
    Mim mim;
    mim.read_loop();
    return 0;
}
