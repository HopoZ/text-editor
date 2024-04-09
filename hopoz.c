/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define HOPOZ_VERSION "0.0.1"
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
    write(STDOUT_FILENO, "\x1b[H", 3); // uses the H command (Cursor Position) to position the cursor,default 1,1
    perror(s); // print the errno descriptively
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
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP); // IXON is a flag used to enable or disable software flow control (XON/XOFF) for input
    raw.c_oflag &= ~(OPOST);  // post-process of output
    raw.c_cflag |= (CS8);     // to set not to clear ,so use |=
    raw.c_lflag &=
        ~(ECHO | ICANON | IEXTEN | ISIG); // the ICANON flag that allows us to turn off canonical mode.
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
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) // 6n ,request the position of cursor
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
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            // The C command (Cursor Forward) moves the cursor to the right,
            // and the B command (Cursor Down) moves the cursor down.
            // ? Why do methods b and c prevent the cursor from jumping out of the screen, but we don’t directly copy the source code of the prevention method?
            return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};
#define ABUF_INIT { NULL, 0 } // This acts as a constructor for our abuf type.

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if(y==E.screenrows/3){
            char welcome[80];
            int welcomelen =snprintf(welcome,sizeof(welcome),
              "HopoZ editor --version %s",HOPOZ_VERSION); //string n print format
            if(welcomelen>E.screencols)
                welcomelen =E.screencols;
            int padding =(E.screencols -welcomelen)/2;
            if(padding){
                abAppend(ab,"~",1);
                padding--;
            }
            while(padding--)
                abAppend(ab," ",1);
            abAppend(ab,welcome,welcomelen);
        }else
            abAppend(ab, "~", 1);

        abAppend(ab, "\x1b[K",3); // The K command (Erase In Line) erases part of the current line. Its argument is analogous to the J command’s argument:
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    // The h and l commands (Set Mode, Reset Mode) are used to turn on and turn
    // off various terminal features or “modes”.
    //  abAppend(&ab,"\x1b[2J",4);
    //  1b represent esc,always followed by [,
    //  0J is to  clear the screen from the cursor up to the end of the screen.
    //  1J is to clear the screen up to where the cursor is
    //  2J is to to clear the entire screen
    //  4 means 4 bytes
    abAppend(&ab, "\x1b[H", 3);
    editorDrawRows(&ab);
    abAppend(&ab, "\x1b[H", 3);

    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
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


