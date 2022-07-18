#include <ctype.h>
# include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// global vars
struct termios orig_termios;


// disables raw mode of terminal after program termination
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


// sets terminal attributes so that it runs in raw mode
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;

    
    // 'ECHO' prints each key you type to the terminal
    // 'ICANON' allows us to turn off canonical mode
    // 'ISIG' disable Ctrl-C, Ctrl-Z and Ctrl-Y (Mac Only) signals 
    // 'IXON' disables Ctrl-s and Ctrl-Q signals
    // 'IEXTEN' disable Ctrl-V and Ctrl-o signals
    // 'ICRNL' fices the Ctrl-M signal that pronted as a new line byte 10, I = input, CR = carriage return and NL 0 new line
    // 'OPOST' turns off all of terminal's output processing
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN| ISIG);  // 'c_lflag' is for local flags 

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int main() {
    enableRawMode();

    char c;

    // prints each typed char's ASCII numeric value
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q'){
        if (iscntrl(c)) { // iscntrl() comes from <ctype.h>, it tests whether a character is a control character. 
            printf("%d\r\n", c); // '\r' carriage returns the cursor to the beginning of the line and '\n' moves cursor a line downwards
        } else {
            printf ("%d (%c)\r\n", c, c);
        }
    }

    return 0;
}