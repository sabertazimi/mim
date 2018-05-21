#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>

using namespace std;

#define KEY_CTRL(k)  ((k) & 0x1f)

enum EditorKey {
    KEY_ESC = 27,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DEL,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN
};

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
            this->config.verbose = true;
        }

        Mim(const Mim &mim) {
            this->set_config(mim.get_config());
        }

        Mim(const MimConfig &config) {
            this->set_config(config);
        }

        ~Mim(void) {
            try {
                this->disableRawMode();

                if (this->config.verbose) {
                    fprintf(log, "=> Exit...\r\n");
                    fclose(log);
                }
            } catch (const MimError &e) {
                printf("%s\r\n", e.what());
            }
        }

        void init(void) throw(MimError) {
            try {
                this->editor_state = Mim::MimState::stoped;
                this->editor_mode = Mim::MimMode::command;
                this->cx = 0;
                this->cy = 0;
                this->row_off = 0;
                this->col_off = 0;
                this->num_rows = 0;
                this->rows_buffer.clear();
                this->screen_buffer.clear();
                this->command_buffer.clear();

                this->enableRawMode();

                struct winsize ws = this->getWindowSize();
                this->config.screen_rows = ws.ws_row;
                this->config.screen_cols = ws.ws_col;

                if (this->config.verbose) {
                    log = fopen(".log", "w+");

                    if (log == NULL) {
                        throw MimError("Open log file failed.");
                    }

                    fprintf(log, "=> Init...\r\n");
                }
            } catch (const MimError &e) {
                throw e;
            }
        }

        void start(void) throw(MimError) {
            this->editor_state = Mim::MimState::running;

            while (this->editor_state == Mim::MimState::running) {
                try {
                    this->editorRefreshScreen();
                    this->editorRefreshBuffer();
                    this->editorProcessKeyPress();
                } catch (const MimError &e) {
                    throw e;
                }
            }
        }

        void openFile(const char *filename) throw(MimError) {
            try {
                this->editorOpen(filename);
            } catch (const MimError &e) {
                throw e;
            }
        }

    protected:
        const MimConfig &get_config(void) const {
            return this->config;
        }

        void set_config(const MimConfig &config) {
            this->config.screen_rows = config.screen_rows;
            this->config.screen_cols = config.screen_cols;
            this->config.verbose = config.verbose;
            this->config.orig_termios = config.orig_termios;
        }

    private:
        enum MimState {
            stoped,
            running,
            pending
        };

        enum MimMode {
            command,
            insert,
            lastline
        };

        MimState editor_state;
        MimMode editor_mode;
        MimConfig config;
        const string version = "0.1.0";

        int cx;     // column number in the file (start with 0) (not cursor position)
        int cy;     // row number in the file (start with 0) (not cursor position)
        int num_rows;
        int row_off;
        int col_off;
        vector<string> rows_buffer;
        vector<string> render_buffer;
        string screen_buffer;

        string command_buffer;

        FILE *log;

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

        int editorReadKey(void) throw(MimError) {
            int nread;
            char ch;

            while ((nread = read(STDIN_FILENO, &ch, 1)) != 1) {
                if (nread == -1 && errno != EAGAIN) {
                    throw MimError("Input failed.");
                }
            }

            if (ch == KEY_ESC) {
                char seq[3];

                if (read(STDIN_FILENO, &seq[0], 1) != 1) {
                    return KEY_ESC;
                }

                if (read(STDIN_FILENO, &seq[1], 1) != 1) {
                    return KEY_ESC;
                }

                if (seq[0] == '[') {
                    if (seq[1] >= '0' && seq[1] <= '9') {
                        if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                            return KEY_ESC;
                        }

                        if (seq[2] == '~') {
                            switch (seq[1]) {
                                case '1':
                                    return KEY_HOME;
                                case '3':
                                    return KEY_DEL;
                                case '4':
                                    return KEY_END;
                                case '5':
                                    return KEY_PAGE_UP;
                                case '6':
                                    return KEY_PAGE_DOWN;
                                case '7':
                                    return KEY_HOME;
                                case '8':
                                    return KEY_END;
                            }
                        }
                    } else {
                        switch (seq[1]) {
                            case 'A':
                                return KEY_ARROW_UP;
                            case 'B':
                                return KEY_ARROW_DOWN;
                            case 'C':
                                return KEY_ARROW_RIGHT;
                            case 'D':
                                return KEY_ARROW_LEFT;
                            case 'H':
                                return KEY_HOME;
                            case 'F':
                                return KEY_END;
                            default:
                                return KEY_ESC;
                        }
                    }
                } else if (seq[0] == 'O') {
                    switch (seq[1]) {
                        case 'H':
                            return KEY_HOME;
                        case 'F':
                            return KEY_END;
                    }
                }
            }

            return ch;
        }

        const struct winsize getWindowSize(void) throw(MimError) {
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

        /*** manipulation ***/

        void closeEditor(void) {
            this->editor_state = Mim::MimState::stoped;
            this->editorClearScreen();
            this->editorRefreshBuffer();
        }

        /*** input ***/

        inline void editorMoveCursor(const int &key) {
            string row = (this->cy < this->num_rows) ? this->rows_buffer[this->cy] : "";
            bool end_of_line = (this->cx >= (int)row.length());

            switch (key) {
                case KEY_ARROW_LEFT:
                    if (this->cx != 0) {
                        --this->cx;
                    } else if (this->cy > 0) {
                        --this->cy;
                        this->editorHomeEnd(KEY_END);
                    }

                    break;
                case KEY_ARROW_RIGHT:
                    if (this->cx < (int)row.length()) {
                        ++this->cx;
                    } else if (end_of_line) {
                        ++this->cy;
                        this->cx = 0;
                    }

                    break;
                case KEY_ARROW_UP:
                    if (this->cy != 0) {
                        --this->cy;
                    }

                    if (end_of_line) {
                        this->editorHomeEnd(KEY_END);
                    }

                    break;
                case KEY_ARROW_DOWN:
                    if (this->cy < this->num_rows) {
                        ++this->cy;
                    }

                    if (end_of_line) {
                        this->editorHomeEnd(KEY_END);
                    }

                    break;
                default:
                    break;
            }

            row = (this->cy < this->num_rows) ? this->rows_buffer[this->cy] : "";
            this->cx = min(this->cx, (int)row.length());
        }

        inline void editorPageUpDown(const int &key) {
            int times = this->config.screen_rows;

            while (times--) {
                editorMoveCursor(key == KEY_PAGE_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN);
            }
        }

        inline void editorHomeEnd(const int &key) {
            switch (key) {
                case KEY_HOME:
                    this->cx = 0;
                    break;
                case KEY_END:
                    {
                        string row = (this->cy < this->num_rows) ? this->rows_buffer[this->cy] : "";
                        this->cx = row.length();
                        break;
                    }
                default:
                    break;
            }
        }

        void editorProcessKeyPressInCommandMode(const int &ch) {
            switch (ch) {
                case 'i':
                    this->editor_mode = Mim::MimMode::insert;
                    break;
                case 'q':
                    this->closeEditor();
                    break;
                case 'h':
                    this->editorMoveCursor(KEY_ARROW_LEFT);
                    break;
                case 'j':
                    this->editorMoveCursor(KEY_ARROW_DOWN);
                    break;
                case 'k':
                    this->editorMoveCursor(KEY_ARROW_UP);
                    break;
                case 'l':
                    this->editorMoveCursor(KEY_ARROW_RIGHT);
                    break;
                case 'G':
                    this->cy = this->num_rows;
                    this->editorHomeEnd(KEY_END);
                    break;
                case KEY_ARROW_LEFT:
                case KEY_ARROW_RIGHT:
                case KEY_ARROW_UP:
                case KEY_ARROW_DOWN:
                    this->editorMoveCursor(ch);
                    break;
                case KEY_CTRL('u'):
                    this->editorPageUpDown(KEY_PAGE_UP);
                    break;
                case KEY_CTRL('d'):
                    this->editorPageUpDown(KEY_PAGE_DOWN);
                    break;
                case '0':
                    this->editorHomeEnd(KEY_HOME);
                    break;
                case '$':
                    this->editorHomeEnd(KEY_END);
                    break;
                default:
                    break;
            }
        }

        void editorProcessKeyPressInInsertMode(const int &ch) {
            switch (ch) {
                case KEY_ESC:
                    this->editor_mode = Mim::MimMode::command;
                    break;
                case KEY_CTRL('q'):
                    this->closeEditor();
                    break;
                case KEY_ARROW_LEFT:
                case KEY_ARROW_RIGHT:
                case KEY_ARROW_UP:
                case KEY_ARROW_DOWN:
                    this->editorMoveCursor(ch);
                    break;
                case KEY_PAGE_UP:
                case KEY_PAGE_DOWN:
                    this->editorPageUpDown(ch);
                    break;
                case KEY_HOME:
                case KEY_END:
                    this->editorHomeEnd(ch);
                    break;
                default:
                    break;
            }
        }

        void editorProcessKeyPressInLastlineMode(const int &ch) {
            switch (ch) {
                case KEY_CTRL('q'):
                    this->closeEditor();
                    break;
                default:
                    break;
            }
        }

        void editorProcessKeyPress(void) throw(MimError) {
            try {
                int ch = this->editorReadKey();

                switch (this->editor_mode) {
                    case Mim::MimMode::command:
                        this->editorProcessKeyPressInCommandMode(ch);
                        break;
                    case Mim::MimMode::insert:
                        this->editorProcessKeyPressInInsertMode(ch);
                        break;
                    case Mim::MimMode::lastline:
                        this->editorProcessKeyPressInLastlineMode(ch);
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
            write(STDOUT_FILENO, this->screen_buffer.c_str(), this->screen_buffer.length());
            this->screen_buffer.clear();
        }

        inline void editorScroll(void) {
            if (this->cy < this->row_off) {
                this->row_off = this->cy;
            }

            if (this->cy >= this->row_off + this->config.screen_rows) {
                this->row_off = this->cy - this->config.screen_rows + 1;
            }

            if (this->cx < this->col_off) {
                this->col_off = this->cx;
            }

            if (this->cx >= this->col_off + this->config.screen_cols) {
                this->col_off = this->cx - this->config.screen_cols + 1;
            }
        }

        inline void editorShowVersion(void) {
            string welcome_msg = "Mim Editor -- version " + this->version;
            int padding = (this->config.screen_cols - welcome_msg.length()) / 2;

            if (padding) {
                this->screen_buffer.append("~");
                --padding;
            }

            while (padding--) {
                this->screen_buffer.append(" ");
            }

            this->screen_buffer.append(welcome_msg);
        }

        inline void editorDrawRows(void) {
            for (int y = 0, rows = this->num_rows, maxrows = this->config.screen_rows; y < maxrows; ++y) {
                int file_row = y + row_off;
                if (file_row >= rows) {
                    if (rows == 0 && y == maxrows / 3) {
                        this->editorShowVersion();
                    } else {
                        this->screen_buffer.append("~");
                    }
                } else {
                    int length = this->rows_buffer[file_row].length() - this->col_off;

                    if (length > 0) {
                        length = min(length, this->config.screen_cols);
                        this->screen_buffer.append(this->rows_buffer[file_row].substr(this->col_off, length));
                    }
                }

                this->screen_buffer.append("\x1b[K");

                if (y < maxrows - 1) {
                    this->screen_buffer.append("\r\n");
                }
            }
        }

        inline void editorResetCursor(void) {
            this->screen_buffer.append("\x1b[H");  // move cursor to line 1 column 1
        }

        inline void editorHideCursor(void) {
            this->screen_buffer.append("\x1b[?25l");
        }

        inline void editorShowCursor(void) {
            this->screen_buffer.append("\x1b[?25h");
        }

        inline void editorMoveCursorTo(int x, int y) {
            string buf = "\x1b[" + to_string(y + 1) + ";" + to_string(x + 1) + "H";
            this->screen_buffer.append(buf);
        }

        inline void editorClearScreen(void) {
            this->screen_buffer.append("\x1b[2J"); // clear whole screen
            this->editorResetCursor();
        }

        inline void editorRefreshScreen(void) {
            this->editorScroll();
            this->editorHideCursor();
            this->editorResetCursor();
            this->editorDrawRows();
            this->editorMoveCursorTo(this->cx - this->col_off, this->cy - this->row_off);
            this->editorShowCursor();
        }

        /*** row operations ***/
        void editorUpdateRow(const string &data) {
            this->render_buffer.push_back(data);
        }

        void editorAppendRow(const string &line) {
            this->rows_buffer.push_back(line);
            this->editorUpdateRow(line);
            ++this->num_rows;
        }

        /*** files ***/
        void editorOpen(const char *filename) {
            fstream fs;
            fs.open(filename, fstream::in | fstream::out);

            if (!fs) {
                fs.open(filename, fstream::in | fstream::out | fstream::trunc);

                if (!fs) {
                    throw MimError("Open file failed.");
                }
            }

            string line;

            while (getline(fs, line)) {
                this->editorAppendRow(line);
            }

            fs.close();
        }
};

int main(int argc, char **argv) {
    Mim mim;

    try {
        mim.init();

        if (argc >= 2) {
            mim.openFile(argv[1]);
        }

        mim.start();
    } catch (const MimError &e) {
        printf("%s\r\n", e.what());
    }
}
