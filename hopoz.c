/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE // make our code more portable.

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>

/*** defines ***/

#define HOPOZ_VERSION "0.0.1"
#define HOPOZ_TAB_STOP 8
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

typedef struct erow{
    int size;
    int rsize; //render size
    char *chars;
    char* render;
}erow; //editer's every row
struct editorConfig {
    int cx,cy; //locate the cursor
    int rx; //render x
    int rowoff; // row offset
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
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

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx){
    int rx =0;
    int j;
    for(j =0;j<cx;j++){
        if(row->chars[j]=='\t'){
            rx +=(HOPOZ_TAB_STOP -1) -(rx%HOPOZ_TAB_STOP); // todo disunderstand
        }
        rx++;
    }
    return rx;
}

void editorUpdaterow(erow *row){
    int tabs =0;
    int j =0;
    for(j =0;j <row->size;j++){
        if(row->chars[j]=='\t') tabs++;
    }
    free(row->render);
    row->render =malloc(row->size +tabs *(HOPOZ_TAB_STOP -1) +1);
    int idx =0;
    for(int j =0;j<row->size;j++){
        if(row->chars[j]=='\t'){
            row->render[idx++] =' ';
            while(idx%HOPOZ_TAB_STOP !=0) row->render[idx++] =' ';
        }else {
            row->render[idx++] =row->chars[j];
        }
    }
    row->render[idx] ='\0';
    row->rsize =idx;
}
void editorAppendRow(char *s,size_t len){
    E.row =realloc(E.row,sizeof(erow)*(E.numrows +1));
    int at =E.numrows;
    E.row[at].size =len;
    E.row[at].chars =malloc(len+1);
    memcpy(E.row[at].chars,s,len);
    E.row[at].chars[len] ='\0';
    
    E.row[at].rsize =0;
    E.row[at].render =NULL;
    editorUpdaterow(&E.row[at]);

    E.numrows++;
}

/*** file i/o ***/
void editorOpen(char *filename){
    free(E.filename);
    E.filename =strdup(filename); // makes a copy of the given string, allocating the required memory and assuming you will free() that memory.

    FILE *fp =fopen(filename,"r");
    if(!fp) die("fopen");
    char *line =NULL;
    size_t linecap =0; // line capacity
    //In practice, size_t is more common than size because it is a type provided by the standard library,
    //has cross-platform compatibility, and is widely used in various standard library functions and types.
    ssize_t linelen; // signed size_t
    while((linelen =getline(&line,&linecap,fp)) !=-1){
        while(linelen >0 && line[linelen -1] == '\n' || line[linelen -1] == '\r')
            linelen--;
        editorAppendRow(line,linelen);
    }
    free(line);
    fclose(fp);
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

void editorScroll(){
    E.rx =0;
    if(E.cy <E.numrows){
        E.rx =editorRowCxToRx(&E.row[E.cy],E.cx);
    }

    if(E.cy <E.rowoff){
        E.rowoff =E.cy;
    }
    if(E.cy >=E.rowoff +E.screenrows){
        E.rowoff =E.cy -E.screenrows +1;
    }
    if(E.rx <E.rowoff){
        E.coloff =E.rx;
    }
    if(E.rx >=E.coloff +E.screencols){
        E.coloff =E.rx -E.screencols +1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow =y +E.rowoff;
        if(filerow >=E.numrows){
            if(E.numrows==0 && y==E.screenrows/3){
                char welcome[80];
                int welcomelen =snprintf(welcome,sizeof(welcome),"HopoZ editor --version %s",HOPOZ_VERSION); //string n print format
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
        }else{
            int len =E.row[filerow].rsize -E.coloff;
            if(len <0) len =0;
            if(len >E.screencols) len =E.screencols;
            abAppend(ab,&E.row[filerow].render[E.coloff],len);
        }

        abAppend(ab, "\x1b[K",3); // The K command (Erase In Line) erases part of the current line. Its argument is analogous to the J command’s argument:
            abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab){
    abAppend(ab,"\x1b[7m",4);
    char status[80],rstatus[80];
    int len =snprintf(status,sizeof(status),"%.20s - %d lines",
        E.filename ? E.filename : "[No Name]",E.numrows);
    int rlen =snprintf(rstatus,sizeof(rstatus),"%d/%d",
                       E.cy+1,E.numrows);
    if(len>E.screencols) len =E.screencols;
    abAppend(ab,status,len);
    while(len <E.screencols){
        if(E.screencols -len ==rlen){
            abAppend(ab,rstatus,rlen);
            break;
        }else{
            abAppend(ab," ",1);
            len++;
        }
    }
    abAppend(ab,"\x1b[m",3); // switches back to normal formatting
    // other m command: bold (1), underscore (4), blink (5), and inverted colors (7)
    abAppend(ab,"\r\n",2);
}

void editorDrawMessageBar(struct abuf *ab){
    abAppend(ab,"\x1b[K",3);
    int msglen =strlen(E.statusmsg);
    if(msglen >E.screencols) msglen =E.screencols;
    if(msglen && time(NULL) -E.statusmsg_time <5){
        abAppend(ab,E.statusmsg,msglen);
    }
}

void editorRefreshScreen() {

    editorScroll();

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
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",(E.cy -E.rowoff)+1,(E.rx -E.coloff)+1); // add 1 to E.cy and E.cx to convert from 0-indexed values 
    // to the 1-indexed values that the terminal uses.
    abAppend(&ab,buf,strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt,...){ // ... makes this function a variadic function
    va_list ap; // ap: argument_pointer, fmt: format
    va_start(ap,fmt);
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time =time(NULL);
}

/*** input ***/

void editorMoveCursor(int key){
    erow *row =(E.cy >=E.numrows) ? NULL :&E.row[E.cy];

    switch(key){
     case ARROW_LEFT:
        if(E.cx!=0) E.cx--;
        else if(E.cy >0){
                E.cy--;
                E.cx =E.row[E.cy].size;
            }
        break;
     case ARROW_RIGHT:
    if(row && E.cx <row->size){
        E.cx++;
    }else if(row && E.cx==row->size){
            E.cy++;
            E.cx =0;
        }
        break;
     case ARROW_UP:
      if(E.cy!=0) E.cy--;
      break;
     case ARROW_DOWN:
      if(E.cy < E.numrows) E.cy++;
      break;
    }
    row =(E.cy >=E.numrows) ? NULL :&E.row[E.cy];
    int rowlen =row ?row->size:0;
    if(E.cx>rowlen){
        E.cx =rowlen;
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
            if(E.cy <E.numrows){
                E.cx =E.row[E.cy].size;
            }
        break;
    case PAGE_UP:
    case PAGE_DOWN:
            {
                if(c==PAGE_UP){
                    E.cy =E.rowoff;
                }else if(c==PAGE_DOWN){
                    E.cy =E.rowoff +E.screenrows -1;
                    if(E.cy >E.numrows) E.cy =E.numrows;
                }

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
    E.cx =0,E.cy =0; 
    E.rx =0;
    E.numrows =0;
    E.rowoff =0;
    E.coloff =0;
    E.row =NULL;
    E.filename =NULL;
    E.statusmsg[0] ='\0';
    E.statusmsg_time =0;
    if (getWindowsSize(&E.screenrows, &E.screencols) == -1) die("getWindowsSize");
    E.screenrows -=2;
}

int main(int argc,char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >=2){
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-q to leave");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}


