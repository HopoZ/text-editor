/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // define ctrl+character methods

/*** data ***/

struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3); // uses the H command (Cursor Position)
                                       // to position the cursor,default 1,1
    perror(s);                         // print the errno descriptively
    exit(1);
}

void disableRawMode() {

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK |
                     ISTRIP); // IXON is a flag used to enable or disable
                              // software flow control (XON/XOFF) for input
    raw.c_oflag &= ~(OPOST);  // post-process of output
    raw.c_cflag |= (CS8);     // to set not to clear ,so use |=
    raw.c_lflag &=
        ~(ECHO | ICANON | IEXTEN |
          ISIG); // the ICANON flag that allows us to turn off canonical mode.
    // This means we will finally be reading input byte-by-byte
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr"); // terminal control set attributes
}

char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) !=
        4) // 6n ,request the position of cursor
        return -1;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int getWindowsSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) !=
            12) // The C command (Cursor Forward) moves the cursor to the right,
                // and the B command (Cursor Down) moves the cursor down.
            return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** output ***/

void editorDrawRows() {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        write(STDOUT_FILENO, "~", 1);

        if (y < E.screenrows - 1) {
            write(STDOUT_FILENO, "\r\n", 2);
        }
    }
}
void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J",
          4); // 1b represent esc,always followed by [,
    // 0J is to  clear the screen from the cursor up to the end of the screen.
    // 1J is to clear the screen up to where the cursor is
    // 2J is to to clear the entire screen
    // 4 means 4 bytes
    write(STDOUT_FILENO, "\x1b[H]", 3);
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H]", 3);
}

/*** input ***/

void editorProcessKeypress() {
    char c = editorReadKey();
    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
}

/*** init ***/

void initEditor() {
    if (getWindowsSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowsSize");
}

int main() {
    enableRawMode();
    initEditor();
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
