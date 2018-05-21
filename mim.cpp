#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>

using namespace std;

/*** keypad macros ***/

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

/*** VT100 control sequences macros ***/

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
    int tabs_width;
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

    CursorPosition(const CursorPosition &curpos) {
        this->row = curpos.row;
        this->col = curpos.col;
    }

    CursorPosition(int row, int col) {
        this->row = row;
        this->col = col;
    }
};

struct RowBuffer {
    string raw;
    string render;

    RowBuffer(void) {
        this->raw = "";
        this->render = this->raw;
    }

    RowBuffer(const RowBuffer &buf) {
        this->raw = buf.raw;
        this->render = buf.render;
    }

    RowBuffer(const string &raw) {
        this->raw = raw;
        this->render = this->raw;
    }
};

class Mim {
    public:
        Mim(void) {
            this->config.tabs_width = 4;
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
                this->rx = 0;
                this->row_off = 0;
                this->col_off = 0;
                this->num_rows = 0;
                this->rows_buffer.clear();
                this->screen_buffer.clear();
                this->command_buffer.clear();
                this->dirty_flag = false;
                this->force_quit = false;

                this->updateLastlineBuffer("");
                this->editor_filename = "";

                this->enableRawMode();

                struct winsize ws = this->getWindowSize();
                this->config.screen_rows = ws.ws_row - 2;   // reserve two lines for status bar and lastline mode
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
                    this->refreshScreen();
                    this->refreshBuffer();
                    this->processKeyPress();
                } catch (const MimError &e) {
                    throw e;
                }
            }
        }

        void open(const char *filename) throw(MimError) {
            try {
                if (filename == NULL) {
                    return;
                }

                this->openFile(filename);
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
            this->config.tabs_width = config.tabs_width;
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
        int rx;     // rendered column number

        int num_rows;
        int row_off;
        int col_off;
        vector<RowBuffer> rows_buffer;

        string screen_buffer;
        string command_buffer;
        string lastline_buffer;

        time_t lastline_time;   // lastline update timer

        bool dirty_flag;
        bool force_quit;

        string editor_filename;
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

        int readKey(void) throw(MimError) {
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
            if (this->dirty_flag && !this->force_quit) {
                this->updateLastlineBuffer("[WARN] File has unsaved changes (Press more times to quit)");
                this->force_quit = true;
            } else {
                this->editor_state = Mim::MimState::stoped;
                this->clearScreen();
                this->refreshBuffer();
            }
        }

        /*** input ***/

        inline void keyMoveCursor(const int &key) {
            string row = (this->cy < this->num_rows) ? this->rows_buffer[this->cy].raw : "";
            bool end_of_line = (this->cx >= (int)row.length());

            switch (key) {
                case KEY_ARROW_LEFT:
                    if (this->cx != 0) {
                        --this->cx;
                    } else if (this->cy > 0) {
                        --this->cy;
                        this->keyHomeEnd(KEY_END);
                    }

                    break;
                case KEY_ARROW_RIGHT:
                    if (this->cx < (int)row.length()) {
                        ++this->cx;
                    } else if (end_of_line && this->cy < this->num_rows) {
                        ++this->cy;
                        this->cx = 0;
                    }

                    break;
                case KEY_ARROW_UP:
                    if (this->cy != 0) {
                        --this->cy;
                    }

                    if (end_of_line) {
                        this->keyHomeEnd(KEY_END);
                    }

                    break;
                case KEY_ARROW_DOWN:
                    if (this->cy < this->num_rows) {
                        ++this->cy;
                    }

                    if (end_of_line) {
                        this->keyHomeEnd(KEY_END);
                    }

                    break;
                default:
                    break;
            }

            row = (this->cy < this->num_rows) ? this->rows_buffer[this->cy].raw : "";
            this->cx = min(this->cx, (int)row.length());
        }

        inline void keyPageUpDown(const int &key) {
            if (key == KEY_PAGE_UP) {
                // to the top of screen
                this->cy = this->row_off;
            } else {
                // to the bottom of screen
                this->cy = min(this->row_off + this->config.screen_rows - 1, this->num_rows);
            }

            int times = this->config.screen_rows;

            while (times--) {
                keyMoveCursor(key == KEY_PAGE_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN);
            }
        }

        inline void keyHomeEnd(const int &key) {
            switch (key) {
                case KEY_HOME:
                    this->cx = 0;
                    break;
                case KEY_END:
                    {
                        string row = (this->cy < this->num_rows) ? this->rows_buffer[this->cy].raw : "";
                        this->cx = row.length();
                        break;
                    }
                default:
                    break;
            }
        }

        void processKeyPressInCommandMode(const int &ch) {
            switch (ch) {
                case 'i':
                    this->editor_mode = Mim::MimMode::insert;
                    break;
                case ':':
                    this->editor_mode = Mim::MimMode::lastline;
                    break;
                case 'q':
                    this->closeEditor();
                    break;
                case 'h':
                case '\b':
                    this->keyMoveCursor(KEY_ARROW_LEFT);
                    break;
                case 'j':
                case '\r':
                    this->keyMoveCursor(KEY_ARROW_DOWN);
                    break;
                case 'k':
                    this->keyMoveCursor(KEY_ARROW_UP);
                    break;
                case 'l':
                    this->keyMoveCursor(KEY_ARROW_RIGHT);
                    break;
                case 'g':
                    this->cx = 0;
                    this->cy = 0;
                    break;
                case 'G':
                    this->cy = this->num_rows;
                    this->keyHomeEnd(KEY_END);
                    break;
                case KEY_ARROW_LEFT:
                case KEY_ARROW_RIGHT:
                case KEY_ARROW_UP:
                case KEY_ARROW_DOWN:
                    this->keyMoveCursor(ch);
                    break;
                case KEY_CTRL('u'):
                    this->keyPageUpDown(KEY_PAGE_UP);
                    break;
                case KEY_CTRL('d'):
                    this->keyPageUpDown(KEY_PAGE_DOWN);
                    break;
                case '0':
                    this->keyHomeEnd(KEY_HOME);
                    break;
                case '$':
                    this->keyHomeEnd(KEY_END);
                    break;
                default:
                    break;
            }
        }

        void processKeyPressInInsertMode(const int &ch) {
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
                    this->keyMoveCursor(ch);
                    break;
                case KEY_PAGE_UP:
                case KEY_PAGE_DOWN:
                    this->keyPageUpDown(ch);
                    break;
                case KEY_HOME:
                case KEY_END:
                    this->keyHomeEnd(ch);
                    break;
                case '\r':
                    this->insertNewline();
                    break;
                case '\b':
                case KEY_DEL:
                    if (ch == KEY_DEL) {
                        this->keyMoveCursor(KEY_ARROW_RIGHT);
                    }

                    this->delChar();
                    break;
                case KEY_CTRL('l'):
                    // TODO
                    break;
                case KEY_CTRL('s'):
                    this->saveToFile();
                    break;
                default:
                    this->insertChar(ch);
                    break;
            }
        }

        string getLastlineFromInput(const string &prompt, int first_ch) {
            string lastline_command = "";

            if (first_ch != '\0') {
                lastline_command.append(string(1, first_ch));
            }

            while (true) {
                this->updateLastlineBuffer(prompt + lastline_command);
                this->refreshScreen();
                this->refreshBuffer();
                int ch = this->readKey();

                if (ch == '\b') {
                    if (lastline_command.length()) {
                        lastline_command = lastline_command.substr(0, lastline_command.length() - 1);
                    }
                } else if (ch == KEY_ESC) {
                    lastline_command = "";
                    break;
                } else if (ch == '\r') {
                    if (lastline_command.length()) {
                        break;
                    }
                } else if (!iscntrl(ch) && ch < 128) {
                    lastline_command.append(string(1, ch));
                }
            }

            this->updateLastlineBuffer("");
            return lastline_command;
        }

        void processLastlineCommand(const string &command) {
        }

        void processKeyPressInLastlineMode(const int &ch) {
            this->updateLastlineBuffer(":");

            switch (ch) {
                case KEY_ESC:
                case '\r':
                    this->updateLastlineBuffer("");
                    this->editor_mode = Mim::MimMode::command;
                    break;
                case KEY_CTRL('q'):
                    this->closeEditor();
                    break;
                default:
                    string lastline_command = this->getLastlineFromInput(":", ch);
                    this->editor_mode = Mim::MimMode::command;
                    break;
            }
        }

        void processKeyPress(void) throw(MimError) {
            try {
                int ch = this->readKey();

                switch (this->editor_mode) {
                    case Mim::MimMode::command:
                        this->processKeyPressInCommandMode(ch);
                        break;
                    case Mim::MimMode::insert:
                        this->processKeyPressInInsertMode(ch);
                        break;
                    case Mim::MimMode::lastline:
                        this->processKeyPressInLastlineMode(ch);
                        break;
                    default:
                        break;
                }

            } catch (const MimError &e) {
                throw e;
            }
        }

        /*** output ***/
        inline void refreshBuffer(void) {
            write(STDOUT_FILENO, this->screen_buffer.c_str(), this->screen_buffer.length());
            this->screen_buffer.clear();
        }

        inline void scroll(void) {
            this->rx = 0;

            if (this->cy < this->num_rows) {
                this->rx = cx2rx(this->rows_buffer[this->cy].raw, this->cx);
            }

            // up
            if (this->cy < this->row_off) {
                this->row_off = this->cy;
            }

            // down
            if (this->cy >= this->row_off + this->config.screen_rows) {
                this->row_off = this->cy - this->config.screen_rows + 1;
            }

            // left
            if (this->rx < this->col_off) {
                this->col_off = this->rx;
            }

            // right
            if (this->rx >= this->col_off + this->config.screen_cols) {
                this->col_off = this->rx - this->config.screen_cols + 1;
            }
        }

        inline void showVersion(void) {
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

        inline void drawRows(void) {
            for (int y = 0, rows = this->num_rows, maxrows = this->config.screen_rows; y < maxrows; ++y) {
                int file_row = y + row_off;

                if (file_row >= rows) {
                    // draw '~' placeholder or version
                    if (rows == 0 && y == maxrows / 3) {
                        this->showVersion();
                    } else {
                        this->screen_buffer.append("~");
                    }
                } else {
                    // draw text data from files
                    int length = this->rows_buffer[file_row].render.length() - this->col_off;

                    if (length > 0) {
                        length = min(length, this->config.screen_cols);
                        this->screen_buffer.append(this->rows_buffer[file_row].render.substr(this->col_off, length));
                    }
                }

                this->clearLineAfterCursor();
                this->screen_buffer.append("\r\n");
            }
        }

        inline void drawStatusBar(void) {
            string status = "";

            switch (this->editor_mode) {
                case Mim::MimMode::command:
                    status += "COMMAND | ";
                    break;
                case Mim::MimMode::insert:
                    status += "INSERT | ";
                    break;
                case Mim::MimMode::lastline:
                    status += "LASTLINE | ";
                    break;
                default:
                    break;
            }

            string filename = (this->editor_filename == "") ? "[No Name]" : this->editor_filename;
            status += (filename + " - " + to_string(this->num_rows) + " lines ");
            string modified = (this->dirty_flag) ? "(modified)" : "";
            status += modified;
            int length = min((int)status.length(), this->config.screen_cols);
            string rstatus = (to_string(this->cy + 1) + "/" + to_string(this->num_rows));
            int rlength = min((int)rstatus.length(), this->config.screen_cols);

            this->screen_buffer.append("\x1b[7m");
            this->screen_buffer.append(status.c_str(), length);

            for (int i = length, cols = this->config.screen_cols; i < cols; ++i) {
                if (cols - i == rlength) {
                    this->screen_buffer.append(rstatus);
                    break;
                } else {
                    this->screen_buffer.append(" ");
                }
            }

            this->screen_buffer.append("\x1b[m");
            this->screen_buffer.append("\r\n");
        }

        inline void updateLastlineBuffer(const string &lastline) {
            this->lastline_buffer.clear();
            this->lastline_buffer.append(lastline);
            this->lastline_time = time(NULL);
        }

        inline void drawLastline(void) {
            int length = min((int)this->lastline_buffer.length(), this->config.screen_cols);

            if (length && time(NULL) - this->lastline_time < 5) {
                this->screen_buffer.append(this->lastline_buffer.c_str(), length);
            }

            this->clearLineAfterCursor();
        }

        inline void resetCursor(void) {
            this->screen_buffer.append("\x1b[H");  // move cursor to line 1 column 1
        }

        inline void hideCursor(void) {
            this->screen_buffer.append("\x1b[?25l");
        }

        inline void showCursor(void) {
            this->screen_buffer.append("\x1b[?25h");
        }

        inline void moveCursorTo(int x, int y) {
            string buf = "\x1b[" + to_string(y + 1) + ";" + to_string(x + 1) + "H";
            this->screen_buffer.append(buf);
        }

        inline void clearLineAfterCursor(void) {
            this->screen_buffer.append("\x1b[K");
        }

        inline void clearScreen(void) {
            this->screen_buffer.append("\x1b[2J"); // clear whole screen
            this->resetCursor();
        }

        inline void refreshScreen(void) {
            this->scroll();
            this->hideCursor();
            this->resetCursor();
            this->drawRows();
            this->drawStatusBar();
            this->drawLastline();
            this->moveCursorTo(this->rx - this->col_off, this->cy - this->row_off);
            this->showCursor();
        }

        /*** row operations ***/
        int cx2rx(const string &raw, int cx) {
            int rx = 0;

            for (int i = 0; i < cx; ++i) {
                if (raw[i] == '\t') {
                    rx += ((this->config.tabs_width - 1) - (rx % this->config.tabs_width));
                }

                ++rx;
            }

            return rx;
        }

        const string renderFromRawWithConfig(const string &raw) {
            string ret = "";

            for (int i = 0, len = (int)raw.length(); i < len; ++i) {
                if (raw[i] == '\t') {
                    ret.append(" ");

                    while (ret.length() % (this->config.tabs_width) != 0) {
                        ret.append(" ");
                    }
                } else {
                    ret.append(1, raw[i]);
                }
            }

            return ret;
        }

        void insertRow(int num_row, const string &line) {
            if (num_row < 0 || num_row > this->num_rows) {
                return;
            }

            this->rows_buffer.insert(this->rows_buffer.begin() + num_row, RowBuffer(line));
            this->rows_buffer[num_row].render = this->renderFromRawWithConfig(line);
            ++this->num_rows;
            this->dirty_flag = true;
        }

        void delRow(int num_row) {
            if (num_row < 0 || num_row >= this->num_rows) {
                return;
            }

            this->rows_buffer.erase(this->rows_buffer.begin() + num_row);
            --this->num_rows;
            this->dirty_flag = true;
        }

        void appendStringToRow(int num_row, const string &str) {
            this->rows_buffer[num_row].raw.append(str);
            this->rows_buffer[num_row].render = this->renderFromRawWithConfig(this->rows_buffer[num_row].raw);
            this->dirty_flag = true;
        }

        void insertCharToRow(int num_row, int at, int ch) {
            RowBuffer row = this->rows_buffer[num_row];

            if (at < 0 || at > (int)row.raw.length()) {
                at = row.raw.length();
            }

            row.raw = row.raw.substr(0, at) + string(1, ch) + row.raw.substr(at);
            row.render = this->renderFromRawWithConfig(row.raw);
            this->rows_buffer[num_row] = row;
            this->dirty_flag = true;
        }

        void delCharFromRow(int num_row, int at) {
            RowBuffer row = this->rows_buffer[num_row];

            if (at < 1 || at > (int)row.raw.length()) {
                return;
            }

            string left = row.raw.substr(0, at - 1);
            string right = (at < (int)row.raw.length()) ? row.raw.substr(at) : "";
            row.raw = left + right;
            row.render = this->renderFromRawWithConfig(row.raw);
            this->rows_buffer[num_row] = row;
            this->dirty_flag = true;
        }

        /*** editor operations ***/

        void insertChar(int ch) {
            if (this->cy == this->num_rows) {
                this->insertRow(this->cy, "");
            }

            this->insertCharToRow(this->cy, this->cx, ch);
            ++this->cx;
        }

        void delChar(void) {
            if (this->cx <= 0 && this->cy <= 0) {
                return;
            }

            if (this->cy < 0 || this->cy > this->num_rows) {
                return;
            }

            if (this->cx == this->num_rows) {
                this->keyMoveCursor(KEY_ARROW_LEFT);
                return;
            }

            if (this->cx <= 0) {
                this->keyMoveCursor(KEY_ARROW_LEFT);
                this->appendStringToRow(this->cy, this->rows_buffer[this->cy + 1].raw);
                this->delRow(this->cy + 1);
            } else {
                this->delCharFromRow(this->cy, this->cx);
                --this->cx;
            }
        }

        void insertNewline(void) {
            if (this->cx == 0) {
                this->insertRow(this->cy, "");
            } else {
                string row_string = this->rows_buffer[this->cy].raw;
                this->rows_buffer[this->cy].raw = row_string.substr(0, this->cx);
                this->rows_buffer[this->cy].render = this->renderFromRawWithConfig(this->rows_buffer[this->cy].raw);

                string right_string = row_string.substr(this->cx);
                this->insertRow(this->cy + 1, right_string);
            }

            this->cx = 0;
            ++this->cy;
        }

        /*** files ***/
        const string rowsBufferToString(int &ret_len) {
            string ret_string = "";
            ret_len = 0;

            for (int i = 0, rows = this->num_rows; i < rows; ++i) {
                string row = this->rows_buffer[i].raw;
                ret_len += (row.length() + 1);
                ret_string += (row + "\n");
            }

            return ret_string;
        }

        void openFile(const char *filename) throw(MimError) {
            fstream fs;
            fs.open(filename, fstream::in | fstream::out);

            if (!fs) {
                fs.open(filename, fstream::in | fstream::out | fstream::trunc);

                if (!fs) {
                    throw MimError("Open file failed.");
                }
            }

            this->editor_filename = string(filename);

            string line;

            while (getline(fs, line)) {
                this->insertRow(this->num_rows, line);
            }

            this->dirty_flag = false;
            fs.close();
        }

        void saveToFile(void) {
            if (this->editor_filename == "") {
                this->editor_filename = this->getLastlineFromInput("Save as: ", '\0');

                if (this->editor_filename == "") {
                    this->updateLastlineBuffer("Save aborted");
                    return;
                }
            }

            fstream fs(this->editor_filename, fstream::in | fstream::out | fstream::trunc);

            if (!fs) {
                this->updateLastlineBuffer("Save to file " + this->editor_filename + " failed");
                return;
            }

            int buf_len = 0;
            string buf_string = this->rowsBufferToString(buf_len);
            fs.write(buf_string.c_str(), buf_len);
            this->updateLastlineBuffer(to_string(buf_len) + " bytes written to disk");
            this->dirty_flag = false;
            fs.close();
        }
};

int main(int argc, char **argv) {
    Mim mim;

    try {
        mim.init();

        if (argc >= 2) {
            mim.open(argv[1]);
        }

        mim.start();
    } catch (const MimError &e) {
        printf("%s\r\n", e.what());
    }
}
