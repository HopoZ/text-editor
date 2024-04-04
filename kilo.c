#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK |
                     ISTRIP); // This is a flag used to enable or disable
                              // software flow control (XON/XOFF) for input
    raw.c_oflag &= ~(OPOST); // post-process of output
    raw.c_cflag |= (CS8);    // set not clear ,so use |=
    raw.c_lflag &=
        ~(ECHO | ICANON | IEXTEN |
          ISIG); // the ICANON flag that allows us to turn off canonical mode.
                 // This means we will finally be reading input byte-by-byte
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // terminal control set attributes
}

int main() {
    enableRawMode();
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c,
                   c); //%d tells it to format the byte as a decimal number (its
                       // ASCII code), and %c tells it to write out the byte
                       // directly, as a character.
        }
    }
    return 0;
}
