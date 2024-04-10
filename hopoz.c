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
enum editorKey{
    ARROW_LEFT =1000, //We will give them a large integer value that is out of the range of a char, so that they don’t conflict with any ordinary keypresses.
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

/*** data ***/

struct editorConfig {
    int cx,cy;
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

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP); // IXON is a flag used to enable or disable software flow control (XON/XOFF) for input
    raw.c_oflag &= ~(OPOST);  // post-process of output
    raw.c_cflag |= (CS8);     // to set not to clear ,so use |=
    raw.c_lflag &=~(ECHO | ICANON | IEXTEN | ISIG); // the ICANON flag that allows us to turn off canonical mode. This means we will finally be reading input byte-by-byte
    raw.c_cc[VMIN] = 0; //When set to a non-zero value, the read function will block until at least "vmin" characters are available.
    raw.c_cc[VTIME] = 1; //When set to a non-zero value, the read function will start a timer after reading "vmin" characters, and if no more characters are received within "vtime" time, the read function will return.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // terminal control set attributes
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO,&seq[0],1) !=1)  return '\x1b';
        if(read(STDIN_FILENO,&seq[1],1) !=1)  return '\x1b';
        if(seq[0]=='['){
            if(seq[1]>='0' && seq[1]<='9'){
                if(read(STDIN_FILENO,&seq[2],1)!=1) return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }else{
             switch (seq[1]) {
                 case 'A': return ARROW_UP;
                 case 'B': return ARROW_DOWN;
                 case 'C': return ARROW_RIGHT;
                 case 'D': return ARROW_LEFT;
                 case 'H': return HOME_KEY;
                 case 'F': return END_KEY;
               }
            }
        }else if(seq[0] == '0'){
                switch(seq[1]){
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
        }
        return('\x1b');
    }else{
     return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // 6n ,request the position of cursor
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

int getWindowsSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)  return -1;
            // The C command (Cursor Forward) moves the cursor to the right,
            // and the B command (Cursor Down) moves the cursor down.
            // ? Why do methods b and c prevent the cursor from jumping out of the screen, but we don’t directly copy the source code of the prevention method?
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

    if (new == NULL) return;
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

    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,E.cx+1); // add 1 to E.cy and E.cx to convert from 0-indexed values to the 1-indexed values that the terminal uses.
    abAppend(&ab,buf,strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key){
    switch(key){
     case ARROW_LEFT:
        if(E.cx!=0) E.cx--;
        break;
     case ARROW_RIGHT:
        if(E.cx !=E.screencols -1) E.cx++;
        break;
     case ARROW_UP:
      if(E.cy!=0) E.cy--;
      break;
     case ARROW_DOWN:
      if(E.cy != E.screenrows -1) E.cy++;
      break;
    }
}
void editorProcessKeypress() {
    int c = editorReadKey();
    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    case HOME_KEY:
        E.cx =0;
        break;
    case END_KEY:
        E.cx =E.screencols-1;
        break;
    case PAGE_UP:
    case PAGE_DOWN:
            {
                int times =E.screenrows;
                while(times--){
                    editorMoveCursor(c==PAGE_UP ?ARROW_UP :ARROW_DOWN);
                }
            }
        break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    }
}

/*** init ***/

void initEditor() {
    E.cx =0,E.cy =0; //locate the cursor
    if (getWindowsSize(&E.screenrows, &E.screencols) == -1) die("getWindowsSize");
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


