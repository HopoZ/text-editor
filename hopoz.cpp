#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdarg>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** 常量与宏 ***/
const std::string HOPOZ_VERSION = "0.0.1-cpp";
const int HOPOZ_TAB_STOP = 8;
const int HOPOZ_QUIT_TIMES = 3;
#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum Highlight {
    HL_NORMAL = 0, HL_COMMENT, HL_MLCOMMENT, HL_KEYWORD1, 
    HL_KEYWORD2, HL_STRING, HL_NUMBER, HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/*** 语法定义结构 ***/
struct EditorSyntax {
    std::string filetype;
    std::vector<std::string> filematch;
    std::vector<std::string> keywords;
    std::string singleline_comment_start;
    std::string multiline_comment_start;
    std::string multiline_comment_end;
    int flags;
};

// 预设 C/C++ 语法高亮数据库
const std::vector<EditorSyntax> HLDB = {
    {
        "c/cpp",
        {".c", ".h", ".cpp", ".hpp", ".cc"},
        {"switch", "if", "while", "for", "break", "continue", "return", "else",
         "struct", "union", "typedef", "static", "enum", "class", "case",
         "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|"},
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    }
};

/*** 行数据管理类 ***/
class EditorRow {
public:
    int idx;
    std::string chars;
    std::string render;
    std::vector<Highlight> hl;
    bool hl_open_comment = false;

    EditorRow(std::string s, int index) : idx(index), chars(std::move(s)) {
        update();
    }

    void update() {
        render.clear();
        for (char c : chars) {
            if (c == '\t') {
                render += ' ';
                while (render.size() % HOPOZ_TAB_STOP != 0) render += ' ';
            } else {
                render += c;
            }
        }
        hl.assign(render.size(), HL_NORMAL);
    }
};

/*** 编辑器主类 ***/
class Editor {
private:
    int cx = 0, cy = 0, rx = 0;
    int rowoff = 0, coloff = 0;
    int screenrows, screencols;
    int dirty = 0;
    std::vector<EditorRow> rows;
    std::string filename;
    std::string statusmsg;
    time_t statusmsg_time = 0;
    const EditorSyntax* syntax = nullptr;
    struct termios orig_termios;

    /*** 终端底层处理 ***/
    void die(const std::string& s) {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        perror(s.c_str());
        exit(1);
    }

    void disableRawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    void enableRawMode() {
        if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
        struct termios raw = orig_termios;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
    }

    int readKey() {
        int nread;
        char c;
        while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
            if (nread == -1 && errno != EAGAIN) die("read");
        }
        if (c == '\x1b') {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                    if (seq[2] == '~') {
                        switch (seq[1]) {
                            case '1': return HOME_KEY;
                            case '3': return DEL_KEY;
                            case '4': return END_KEY;
                            case '5': return PAGE_UP;
                            case '6': return PAGE_DOWN;
                            case '7': return HOME_KEY;
                            case '8': return END_KEY;
                        }
                    }
                } else {
                    switch (seq[1]) {
                        case 'A': return ARROW_UP;
                        case 'B': return ARROW_DOWN;
                        case 'C': return ARROW_RIGHT;
                        case 'D': return ARROW_LEFT;
                        case 'H': return HOME_KEY;
                        case 'F': return END_KEY;
                    }
                }
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
            return '\x1b';
        }
        return c;
    }

    /*** 语法高亮引擎 ***/
    bool isSeparator(int c) {
        return isspace(c) || c == '\0' || strchr(",.()+-*/=~%<>[];", c) != NULL;
    }

    void updateSyntax(int idx) {
        EditorRow& row = rows[idx];
        row.hl.assign(row.render.size(), HL_NORMAL);
        if (!syntax) return;

        const auto& keywords = syntax->keywords;
        const std::string& scs = syntax->singleline_comment_start;
        const std::string& mcs = syntax->multiline_comment_start;
        const std::string& mce = syntax->multiline_comment_end;

        int prev_sep = 1;
        int in_string = 0;
        bool in_comment = (idx > 0 && rows[idx - 1].hl_open_comment);

        size_t i = 0;
        while (i < row.render.size()) {
            char c = row.render[i];
            Highlight prev_hl = (i > 0) ? row.hl[i - 1] : HL_NORMAL;

            // 单行注释
            if (!scs.empty() && !in_string && !in_comment) {
                if (row.render.compare(i, scs.size(), scs) == 0) {
                    std::fill(row.hl.begin() + i, row.hl.end(), HL_COMMENT);
                    break;
                }
            }

            // 多行注释
            if (!mcs.empty() && !mce.empty() && !in_string) {
                if (in_comment) {
                    row.hl[i] = HL_MLCOMMENT;
                    if (row.render.compare(i, mce.size(), mce) == 0) {
                        std::fill(row.hl.begin() + i, row.hl.begin() + i + mce.size(), HL_MLCOMMENT);
                        i += mce.size();
                        in_comment = false;
                        prev_sep = 1;
                        continue;
                    }
                    i++; continue;
                } else if (row.render.compare(i, mcs.size(), mcs) == 0) {
                    std::fill(row.hl.begin() + i, row.hl.begin() + i + mcs.size(), HL_MLCOMMENT);
                    i += mcs.size();
                    in_comment = true;
                    continue;
                }
            }

            // 字符串
            if (syntax->flags & HL_HIGHLIGHT_STRINGS) {
                if (in_string) {
                    row.hl[i] = HL_STRING;
                    if (c == '\\' && i + 1 < row.render.size()) {
                        row.hl[i + 1] = HL_STRING;
                        i += 2; continue;
                    }
                    if (c == in_string) in_string = 0;
                    i++; prev_sep = 1;
                    continue;
                } else if (c == '"' || c == '\'') {
                    in_string = c;
                    row.hl[i] = HL_STRING;
                    i++; continue;
                }
            }

            // 数字
            if (syntax->flags & HL_HIGHLIGHT_NUMBERS) {
                if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
                    row.hl[i] = HL_NUMBER;
                    i++; prev_sep = 0;
                    continue;
                }
            }

            // 关键字
            if (prev_sep) {
                bool matched = false;
                for (const auto& kw : keywords) {
                    bool is_kw2 = kw.back() == '|';
                    std::string k = is_kw2 ? kw.substr(0, kw.size() - 1) : kw;
                    if (row.render.compare(i, k.size(), k) == 0 && isSeparator(row.render[i + k.size()])) {
                        std::fill(row.hl.begin() + i, row.hl.begin() + i + k.size(), is_kw2 ? HL_KEYWORD2 : HL_KEYWORD1);
                        i += k.size();
                        matched = true;
                        break;
                    }
                }
                if (matched) { prev_sep = 0; continue; }
            }

            prev_sep = isSeparator(c);
            i++;
        }

        bool changed = (row.hl_open_comment != in_comment);
        row.hl_open_comment = in_comment;
        if (changed && idx + 1 < (int)rows.size()) updateSyntax(idx + 1);
    }

    int syntaxToColor(Highlight hl) {
        switch (hl) {
            case HL_COMMENT:
            case HL_MLCOMMENT: return 36; // 青色
            case HL_KEYWORD1:  return 33; // 黄色
            case HL_KEYWORD2:  return 32; // 绿色
            case HL_STRING:   return 35; // 洋红
            case HL_NUMBER:   return 31; // 红色
            case HL_MATCH:    return 34; // 蓝色
            default:          return 37;
        }
    }

    void selectSyntaxHighlight() {
        syntax = nullptr;
        if (filename.empty()) return;
        size_t dot = filename.find_last_of('.');
        std::string ext = (dot != std::string::npos) ? filename.substr(dot) : "";

        for (const auto& s : HLDB) {
            for (const auto& match : s.filematch) {
                bool is_ext = (match[0] == '.');
                if ((is_ext && ext == match) || (!is_ext && filename.find(match) != std::string::npos)) {
                    syntax = &s;
                    for (int i = 0; i < (int)rows.size(); i++) updateSyntax(i);
                    return;
                }
            }
        }
    }

    /*** 屏幕滚动与光标位置 ***/
    int rowCxToRx(const EditorRow& row, int _cx) {
        int _rx = 0;
        for (int j = 0; j < _cx; j++) {
            if (row.chars[j] == '\t')
                _rx += (HOPOZ_TAB_STOP - 1) - (_rx % HOPOZ_TAB_STOP);
            _rx++;
        }
        return _rx;
    }

    int rowRxToCx(const EditorRow& row, int _rx) {
        int cur_rx = 0;
        int _cx;
        for (_cx = 0; _cx < (int)row.chars.size(); _cx++) {
            if (row.chars[_cx] == '\t')
                cur_rx += (HOPOZ_TAB_STOP - 1) - (cur_rx % HOPOZ_TAB_STOP);
            cur_rx++;
            if (cur_rx > _rx) return _cx;
        }
        return _cx;
    }

    void scroll() {
        rx = 0;
        if (cy < (int)rows.size()) rx = rowCxToRx(rows[cy], cx);
        if (cy < rowoff) rowoff = cy;
        if (cy >= rowoff + screenrows) rowoff = cy - screenrows + 1;
        if (rx < coloff) coloff = rx;
        if (rx >= coloff + screencols) coloff = rx - screencols + 1;
    }

    /*** 绘制 UI ***/
    void drawRows(std::string& ab) {
        for (int y = 0; y < screenrows; y++) {
            int filerow = y + rowoff;
            if (filerow >= (int)rows.size()) {
                if (rows.empty() && y == screenrows / 3) {
                    std::string welcome = "HopoZ editor -- version " + HOPOZ_VERSION;
                    int welcomelen = std::min((int)welcome.size(), screencols);
                    int padding = (screencols - welcomelen) / 2;
                    if (padding) { ab += "~"; padding--; }
                    while (padding--) ab += " ";
                    ab += welcome.substr(0, welcomelen);
                } else {
                    ab += "~";
                }
            } else {
                int len = (int)rows[filerow].render.size() - coloff;
                if (len < 0) len = 0;
                if (len > screencols) len = screencols;

                const char* c = &rows[filerow].render[coloff];
                const Highlight* hl = &rows[filerow].hl[coloff];
                int current_color = -1;

                for (int j = 0; j < len; j++) {
                    if (iscntrl(c[j])) {
                        char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                        ab += "\x1b[7m"; ab += sym; ab += "\x1b[m";
                        if (current_color != -1) {
                            ab += "\x1b[" + std::to_string(current_color) + "m";
                        }
                    } else if (hl[j] == HL_NORMAL) {
                        if (current_color != -1) { ab += "\x1b[39m"; current_color = -1; }
                        ab += c[j];
                    } else {
                        int color = syntaxToColor(hl[j]);
                        if (color != current_color) {
                            current_color = color;
                            ab += "\x1b[" + std::to_string(color) + "m";
                        }
                        ab += c[j];
                    }
                }
                ab += "\x1b[39m";
            }
            ab += "\x1b[K\r\n";
        }
    }

    void drawStatusBar(std::string& ab) {
        ab += "\x1b[7m";
        char status[80], rstatus[80];
        int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                           filename.empty() ? "[No Name]" : filename.c_str(), 
                           (int)rows.size(), dirty ? "(modified)" : "");
        int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                            syntax ? syntax->filetype.c_str() : "no ft", cy + 1, (int)rows.size());
        if (len > screencols) len = screencols;
        ab.append(status, len);
        while (len < screencols) {
            if (screencols - len == rlen) { ab.append(rstatus, rlen); break; }
            else { ab += " "; len++; }
        }
        ab += "\x1b[m\r\n";
    }

    void drawMessageBar(std::string& ab) {
        ab += "\x1b[K";
        if (!statusmsg.empty() && time(NULL) - statusmsg_time < 5) {
            ab += statusmsg.substr(0, screencols);
        }
    }

public:
    Editor() {
        enableRawMode();
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) die("ioctl");
        screenrows = ws.ws_row - 2;
        screencols = ws.ws_col;
    }

    ~Editor() { disableRawMode(); }

    void setStatusMessage(const char* fmt, ...) {
        char buf[128];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        statusmsg = buf;
        statusmsg_time = time(NULL);
    }

    void refreshScreen() {
        scroll();
        std::string ab = "\x1b[?25l\x1b[H";
        drawRows(ab);
        drawStatusBar(ab);
        drawMessageBar(ab);

        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (cy - rowoff) + 1, (rx - coloff) + 1);
        ab += buf;
        ab += "\x1b[?25h";
        write(STDOUT_FILENO, ab.c_str(), ab.size());
    }

    /*** 交互提示框 ***/
    std::string prompt(const char* p_fmt, void (*callback)(Editor&, std::string, int) = nullptr) {
        std::string buf;
        while (true) {
            setStatusMessage(p_fmt, buf.c_str());
            refreshScreen();
            int c = readKey();
            if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
                if (!buf.empty()) buf.pop_back();
            } else if (c == '\x1b') {
                setStatusMessage("");
                if (callback) callback(*this, buf, c);
                return "";
            } else if (c == '\r') {
                if (!buf.empty()) { setStatusMessage(""); if (callback) callback(*this, buf, c); return buf; }
            } else if (!iscntrl(c) && c < 128) {
                buf += (char)c;
            }
            if (callback) callback(*this, buf, c);
        }
    }

    /*** 文件操作 ***/
    void open(const std::string& fname) {
        filename = fname;
        selectSyntaxHighlight();
        std::ifstream is(fname);
        if (!is) return;
        std::string line;
        rows.clear();
        while (std::getline(is, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            rows.emplace_back(line, rows.size());
        }
        dirty = 0;
    }

    void save() {
        if (filename.empty()) {
            filename = prompt("Save as: %s (ESC to cancel)");
            if (filename.empty()) { setStatusMessage("Save aborted"); return; }
            selectSyntaxHighlight();
        }
        std::ofstream os(filename);
        if (os) {
            for (const auto& r : rows) os << r.chars << "\n";
            dirty = 0;
            setStatusMessage("%zu bytes written to disk", (size_t)os.tellp());
        } else {
            setStatusMessage("Can't save! I/O error: %s", strerror(errno));
        }
    }

    /*** 搜索功能 ***/
    static void findCallback(Editor& e, std::string query, int key) {
        static int last_match = -1;
        static int direction = 1;
        static std::vector<Highlight> saved_hl;
        static int saved_hl_line = -1;

        if (saved_hl_line != -1) {
            e.rows[saved_hl_line].hl = saved_hl;
            saved_hl_line = -1;
        }

        if (key == '\r' || key == '\x1b') {
            last_match = -1; direction = 1; return;
        } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
            direction = 1;
        } else if (key == ARROW_LEFT || key == ARROW_UP) {
            direction = -1;
        } else {
            last_match = -1; direction = 1;
        }

        if (query.empty()) return;
        int current = last_match;
        for (int i = 0; i < (int)e.rows.size(); i++) {
            current += direction;
            if (current == -1) current = e.rows.size() - 1;
            else if (current == (int)e.rows.size()) current = 0;

            size_t pos = e.rows[current].render.find(query);
            if (pos != std::string::npos) {
                last_match = current;
                e.cy = current;
                e.cx = e.rowRxToCx(e.rows[current], pos);
                e.rowoff = e.rows.size(); // 强制滚动到光标处

                saved_hl_line = current;
                saved_hl = e.rows[current].hl;
                std::fill(e.rows[current].hl.begin() + pos, e.rows[current].hl.begin() + pos + query.size(), HL_MATCH);
                break;
            }
        }
    }

    void find() {
        int scx = cx, scy = cy, scol = coloff, srow = rowoff;
        std::string query = prompt("Search: %s (ESC/Arrows/Enter)", findCallback);
        if (query.empty()) { cx = scx; cy = scy; coloff = scol; rowoff = srow; }
    }

    /*** 编辑操作 ***/
    void moveCursor(int key) {
        EditorRow* row = (cy >= (int)rows.size()) ? nullptr : &rows[cy];
        switch (key) {
            case ARROW_LEFT:
                if (cx != 0) cx--;
                else if (cy > 0) { cy--; cx = rows[cy].chars.size(); }
                break;
            case ARROW_RIGHT:
                if (row && cx < (int)row->chars.size()) cx++;
                else if (row && cx == (int)row->chars.size()) { cy++; cx = 0; }
                break;
            case ARROW_UP: if (cy != 0) cy--; break;
            case ARROW_DOWN: if (cy < (int)rows.size()) cy++; break;
        }
        int len = (cy >= (int)rows.size()) ? 0 : rows[cy].chars.size();
        if (cx > len) cx = len;
    }

    void processKeypress() {
        static int quit_times = HOPOZ_QUIT_TIMES;
        int c = readKey();
        switch (c) {
            case '\r':
                if (cy == (int)rows.size()) rows.emplace_back("", rows.size());
                rows.insert(rows.begin() + cy + 1, EditorRow(rows[cy].chars.substr(cx), cy + 1));
                rows[cy].chars = rows[cy].chars.substr(0, cx);
                rows[cy].update(); updateSyntax(cy);
                cy++; cx = 0; dirty++;
                break;
            case CTRL_KEY('q'):
                if (dirty && quit_times > 0) {
                    setStatusMessage("WARNING! Unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times--);
                    return;
                }
                write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
                disableRawMode();
                exit(0);
            case CTRL_KEY('s'): save(); break;
            case CTRL_KEY('f'): find(); break;
            case BACKSPACE:
            case CTRL_KEY('h'):
            case DEL_KEY:
                if (c == DEL_KEY) moveCursor(ARROW_RIGHT);
                if (cy < (int)rows.size() && (cx > 0 || cy > 0)) {
                    if (cx > 0) {
                        rows[cy].chars.erase(--cx, 1);
                        rows[cy].update(); updateSyntax(cy);
                    } else {
                        cx = rows[cy - 1].chars.size();
                        rows[cy - 1].chars += rows[cy].chars;
                        rows.erase(rows.begin() + cy);
                        for (int i = cy - 1; i < (int)rows.size(); i++) { rows[i].idx = i; updateSyntax(i); }
                        cy--;
                    }
                    dirty++;
                }
                break;
            case PAGE_UP: case PAGE_DOWN: {
                if (c == PAGE_UP) cy = rowoff;
                else cy = std::min((int)rows.size(), rowoff + screenrows - 1);
                int times = screenrows;
                while (times--) moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            } break;
            case HOME_KEY: cx = 0; break;
            case END_KEY: if (cy < (int)rows.size()) cx = rows[cy].chars.size(); break;
            case ARROW_UP: case ARROW_DOWN: case ARROW_LEFT: case ARROW_RIGHT:
                moveCursor(c); break;
            default:
                if (!iscntrl(c)) {
                    if (cy == (int)rows.size()) rows.emplace_back("", rows.size());
                    rows[cy].chars.insert(cx++, 1, (char)c);
                    rows[cy].update(); updateSyntax(cy);
                    dirty++;
                }
                break;
        }
        quit_times = HOPOZ_QUIT_TIMES;
    }
};

int main(int argc, char* argv[]) {
    Editor editor;
    if (argc >= 2) editor.open(argv[1]);
    editor.setStatusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-F = find");
    while (true) {
        editor.refreshScreen();
        editor.processKeypress();
    }
    
    return 0;
}