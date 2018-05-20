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

struct CursorPosition {
    int row;
    int col;

    CursorPosition(void) {
        this->row = 0;
        this->col = 0;
    }

    CursorPosition(int row, int col) {
        this->row = row;
        this->col = col;
    }

    CursorPosition(const CursorPosition &curpos) {
        this->row = curpos.row;
        this->col = curpos.col;
    }
};

class Mim {
    public:
        Mim(void) {
            this->editor_state = Mim::State::stoped;
            this->config.verbose = true;
        }

        Mim(const Mim &mim) {
            this->editor_state = Mim::State::stoped;
            this->set_config(mim.get_config());
        }

        Mim(const MimConfig &config) {
            this->editor_state = Mim::State::stoped;
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

                struct winsize ws = this->getWindowSize();
                this->config.screen_rows = ws.ws_row;
                this->config.screen_cols = ws.ws_col;

                this->editorRefreshScreen();
                this->editorRefreshBuffer();

                if (this->config.verbose) {
                    printf("=> Init...\r\n");
                }
            } catch (const MimError &e) {
                throw e;
            }
        }

        void start(void) throw(MimError) {
            this->editor_state = Mim::State::running;

            while (this->editor_state == Mim::State::running) {
                try {
                    this->editorProcessKeyPress();
                } catch (const MimError &e) {
                    throw e;
                }
            }
        }

    protected:
        const MimConfig &get_config(void) const {
            return this->config;
        }

        void set_config(const MimConfig &config) {
            this->config.verbose = config.verbose;
            this->config.orig_termios = config.orig_termios;
        }

    private:
        enum State {
            stoped,
            running,
            pending
        };

        enum Mode {
            command,
            insert,
            lastline
        };

        State editor_state;
        MimConfig config;
        const string version = "0.1.0";
        string editor_buffer;

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

        const struct winsize getWindowSize() throw(MimError) {
            struct winsize ws;

            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
                throw MimError("Get window size failed.");
            }

            return ws;
        }

        const CursorPosition getCursorPosition(void) throw(MimError) {
            char buf[32];
            int row = 0;
            int col = 0;

            if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
                throw MimError("Get cursor position failed.");
            }

            for (unsigned int i = 0; i < sizeof(buf) - 1; ++i) {
                if (read(STDIN_FILENO, &buf[i], 1) != 1 || buf[i] == 'R')  {
                    buf[i] = '\0';
                    break;
                }
            }

            if (buf[0] != '\x1b' || buf[1] != '[') {
                throw MimError("Get cursor position failed.");
            }

            if (sscanf(&buf[2], "%d;%d", &row, &col) != 2) {
                throw MimError("Get cursor position failed.");
            }

            return CursorPosition(row, col);
        }

        /*** input ***/

        void editorProcessKeyPress(void) throw(MimError) {
            try {
                char ch = this->editorReadKey();

                switch (ch) {
                    case KEY_CTRL('q'):
                        this->editor_state = Mim::State::stoped;
                        this->editorClearScreen();
                        this->editorRefreshBuffer();
                        break;
                    default:
                        break;
                }
            } catch (const MimError &e) {
                throw e;
            }
        }

        /*** output ***/
        inline void editorRefreshBuffer(void) {
            write(STDOUT_FILENO, this->editor_buffer.c_str(), this->editor_buffer.length());
            this->editor_buffer.clear();
        }

        inline void editorDrawRows(void) {
            for (int y = 0, rows = this->config.screen_rows; y < rows; ++y) {
                if (y == rows / 3) {
                    string welcome_msg = "Mim Editor -- version " + this->version;
                    int padding = (this->config.screen_cols - welcome_msg.length()) / 2;

                    if (padding) {
                        this->editor_buffer.append("~");
                        --padding;
                    }

                    while (padding--) {
                        this->editor_buffer.append(" ");
                    }

                    this->editor_buffer.append(welcome_msg);
                } else {
                    this->editor_buffer.append("~");
                }

                this->editor_buffer.append("\x1b[K");

                if (y < rows - 1) {
                    this->editor_buffer.append("\r\n");
                }
            }
        }

        inline void editorResetCursor(void) {
            this->editor_buffer.append("\x1b[H");  // move cursor to line 1 column 1
        }

        inline void editorHideCursor(void) {
            this->editor_buffer.append("\x1b[?25l");
        }

        inline void editorShowCursor(void) {
            this->editor_buffer.append("\x1b[?25h");
        }

        inline void editorClearScreen(void) {
            this->editor_buffer.append("\x1b[2J"); // clear whole screen
            this->editorResetCursor();
        }

        inline void editorRefreshScreen(void) {
            this->editorHideCursor();
            this->editorResetCursor();
            this->editorDrawRows();
            this->editorResetCursor();
            this->editorShowCursor();
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
