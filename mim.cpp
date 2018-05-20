#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>

using namespace std;

#define KEY_CTRL(k)  ((k) & 0x1f)

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
    int screen_rows;
    int screen_cols;
    bool verbose;
    struct termios orig_termios;
};

class Mim {
    public:
        Mim(void) {
            this->editor_state = Mim::State::stop;
            this->config.verbose = true;
        }

        Mim(const Mim &mim) {
            this->editor_state = Mim::State::stop;
            this->set_config(mim.get_config());
        }

        Mim(const struct MimConfig &config) {
            this->editor_state = Mim::State::stop;
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
                this->getWindowSize();
                this->editorRefreshScreen();

                if (this->config.verbose) {
                    printf("=> Init...\r\n");
                }
            } catch (const MimError &e) {
                throw e;
            }
        }

        int start(void) throw(MimError) {
            this->editor_state = Mim::State::running;

            while (this->editor_state == Mim::State::running) {
                try {
                    this->editorProcessKeyPress();
                } catch (const MimError &e) {
                    throw e;
                }
            }

            return 0;
        }

    protected:
        const struct MimConfig &get_config(void) const {
            return this->config;
        }

        void set_config(const struct MimConfig &config) {
            this->config.verbose = config.verbose;
            this->config.orig_termios = config.orig_termios;
        }

    private:
        enum State {
            stop,
            running,
            pending
        };

        State editor_state;
        struct MimConfig config;

        /*** terminal ***/

        void enableRawMode(void) throw(MimError) {
            if (tcgetattr(STDIN_FILENO, &(this->config.orig_termios)) == -1) {
                throw MimError("Get terminal mode failed.");
            }

            struct termios raw = this->config.orig_termios;

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
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &(this->config.orig_termios)) == -1) {
                throw MimError("Set terminal mode failed.");
            }
        }

        char editorReadKey(void) throw(MimError) {
            int nread;
            char ch;

            while ((nread = read(STDIN_FILENO, &ch, 1)) != 1) {
                if (nread == -1 && errno != EAGAIN) {
                    throw MimError("Input failed.");
                }
            }

            return ch;
        }

        void getWindowSize() throw(MimError) {
            struct winsize ws;

            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
                throw MimError("Get window size failed.");
            } else {
                this->config.screen_rows = ws.ws_row;
                this->config.screen_cols = ws.ws_col;
            }
        }

        /*** input ***/

        void editorProcessKeyPress(void) throw(MimError) {
            try {
                char ch = this->editorReadKey();

                switch (ch) {
                    case KEY_CTRL('q'):
                        this->editor_state = Mim::State::stop;
                        this->editorClearScreen();
                        break;
                    default:
                        break;
                }
            } catch (const MimError &e) {
                throw e;
            }
        }

        /*** output ***/
        inline void editorDrawRows(void) {
            for (int y = 0; y < this->config.screen_rows; ++y) {
                write(STDOUT_FILENO, "~\r\n", 3);
            }
        }

        inline void editorResetCursor(void) {
            write(STDOUT_FILENO, "\x1b[H", 3);  // move cursor to line 1 column 1
        }

        inline void editorClearScreen(void) {
            write(STDOUT_FILENO, "\x1b[2J", 4); // clear whole screen
            this->editorResetCursor();
        }

        inline void editorRefreshScreen(void) {
            this->editorClearScreen();
            this->editorDrawRows();
            this->editorResetCursor();
        }
};

int main(void) {
    Mim mim;

    try {
        mim.init();
        mim.start();
    } catch (const MimError &e) {
        printf("%s\r\n", e.what());
    }
}
