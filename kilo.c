/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data **/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s) {
    perror(s); // print the errno descriptively
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK |
                     ISTRIP); // This is a flag used to enable or disable
                              // software flow control (XON/XOFF) for input
    raw.c_oflag &= ~(OPOST);  // post-process of output
    raw.c_cflag |= (CS8);     // set not clear ,so use |=
    raw.c_lflag &=
        ~(ECHO | ICANON | IEXTEN |
          ISIG); // the ICANON flag that allows us to turn off canonical mode.
    // This means we will finally be reading input byte-by-byte
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr"); // terminal control set attributes
}

/*** init ***/
int main() {
    enableRawMode();
    while (1) {
        char c = '0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) // error again
            die("read");
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c,
                   c); //%d tells it to format the byte as a decimal number (its
                       // ASCII code), and %c tells it to write out the byte
                       // directly, as a character.
        }
        if (c == 'q')
            break;
    }
    return 0;
}
