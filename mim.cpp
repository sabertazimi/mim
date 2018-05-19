#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>

using namespace std;

class MimError : public exception {
    public:
        MimError(const string &msg) {
            this->msg = msg;
        }

        virtual ~MimError(void) throw() {

        }

        const char *what() const throw() {
            return msg.c_str();
        }

    private:
        string msg;
};

struct MimConfig {
    bool verbose;
};

class Mim {
    public:
        Mim(void) {
            this->config.verbose = true;
        }

        Mim(const Mim &mim) {
            this->set_config(mim.get_config());
        }

        Mim(const struct MimConfig &config) {
            this->set_config(config);
        }

        ~Mim(void) {
            try {
                this->disableRawMode();

                if (this->config.verbose) {
                    printf("=> Exit...\r\n");
                }
            } catch (const MimError &e) {
                printf("%s\r\n", e.what());
            }
        }

        void init(void) throw(MimError) {
            try {
                this->enableRawMode();

                if (this->config.verbose) {
                    printf("=> Init...\r\n");
                }
            } catch (const MimError &e) {
                throw e;
            }
        }

        int start(void) throw(MimError) {
            while (true) {
                char c = '\0';

                if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
                    throw MimError("Read failed.");
                };

                if (c == 'q') {
                    break;
                }

                if (iscntrl(c)) {
                    printf("%d\r\n", c);
                } else {
                    printf("%c\r\n", c);
                }
            }

            return 0;
        }

        const struct MimConfig &get_config(void) const {
            return this->config;
        }

        void set_config(const struct MimConfig &config) {
            this->config.verbose = config.verbose;
        }

    private:
        struct termios orig_termios;
        struct MimConfig config;

        void enableRawMode(void) throw(MimError) {
            if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
                throw MimError("Get terminal mode failed.");
            }

            struct termios raw = orig_termios;

            // turn on raw mode
            raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
            raw.c_oflag &= ~(OPOST);
            raw.c_cflag |= (CS8);
            raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

            // set timeout for read
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 1;

            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
                throw MimError("Set terminal mode failed.");
            }
        }

        void disableRawMode(void) throw(MimError) {
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
                throw MimError("Set terminal mode failed.");
            }
        }
};

Mim *mim;

int main(void) {
    Mim mim;

    try {
        mim.init();
        mim.start();
    } catch (const MimError &e) {
        printf("%s\r\n", e.what());
    }
}
