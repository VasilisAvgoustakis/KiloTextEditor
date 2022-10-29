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
#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)


/*** data ***/

// global struct that will contain the editor's state
struct editorConfig {
    int cx, cy; // int variables to track cursor's x and y position
    int screenrows;
    int screencols;
    struct termios orig_termios; // termios struct holds terminal attributes, orig_termios holds the original state of our terminal
};

struct editorConfig E;

/*** terminal ***/

// prints an error message and exits the programm
void die (const char *s){

    // see editorRefreshScreen
    // cleans up screen in case of error during rendering the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    /* perror() comes from <stdio.h>, and exit() comes from <stdlib.h>.
    * Most C library functions that fail will set the global errno variable to indicate what the error was. 
    * perror() looks at the global errno variable and prints a descriptive error message for it. 
    */
    perror(s);
    exit(1);
}

// disables raw mode of terminal after program termination
void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
        die("tcsetattr");
    };
}

// sets terminal attributes so that it runs in raw mode
void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    
    // 'ECHO' prints each key you type to the terminal
    // 'ICANON' allows us to turn off canonical mode
    // 'ISIG' disables Ctrl-C, Ctrl-Z and Ctrl-Y (Mac Only) signals 
    // 'IXON' disables Ctrl-s and Ctrl-Q signals
    // 'IEXTEN' disable Ctrl-V and Ctrl-o signals
    // 'ICRNL' fices the Ctrl-M signal that pronted as a new line byte 10, I = input, CR = carriage return and NL 0 new line
    // 'OPOST' turns off all of terminal's output processing
    /*
    * When BRKINT is turned on, a break condition will cause a SIGINT signal to be sent to the program, like pressing Ctrl-C.
    * INPCK enables parity checking, which doesn’t seem to apply to modern terminal emulators.
    * ISTRIP causes the 8th bit of each input byte to be stripped, meaning it will set it to 0. This is probably already turned off.
    * CS8 is not a flag, it is a bit mask with multiple bits, which we set using the bitwise-OR (|) operator unlike all the flags we are turning off. It sets the character size (CS) to 8 bits per byte. On my system, it’s already set that way.
    */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN| ISIG);  // 'c_lflag' is for local flags 

    /*
    * VMIN & VTIME come from termios.h and are indexes into c_cc field which stands for control characters an array of bytes that control various terminal settings
    */
    raw.c_cc[VMIN] = 0; // sets min# of bytes of input needed before read can return.
    raw.c_cc[VTIME] = 1; // sets max wait time before read can return

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}


// wait for one keypress, and return it
char editorReadKey() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    return c;
}


int getCursorPosition(int *rows, int *cols) {
    char buf[32]; // buffer to read in the response until we get to char 'R'
    int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while ( i < sizeof(buf) - 1){
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0'; // printf() expects strings to end with a 0 byte, so we make sure to assign '\0' to the final byte of buf.

    // Let’s parse the two numbers out of there using sscanf()
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;

    return -1;
}


// the easy way to get window size
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // ioctl(), TIOCGWINSZ, and struct winsize come from <sys/ioctl.h>.

    // ioctl isn't guaranteed to be able to request the window size on all systems
    // so we provide a fallback method

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // we move the cursoer to the bottom-right by sending two escape sequences one after the other.
        // The C command (Cursor Forward) moves the cursor to the right, and the B command (Cursor Down) moves the cursor down.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; 
            return getCursorPosition(rows, cols);
    } else {                // If it succeeded, we pass the values back by setting the int references 
                            // that were passed to the function. (This is a common approach to having 
                            // functions return multiple values in C. It also allows you to use the return value to 
                            // indicate success or failure.)
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


/*** append buffer ***/

// We want to replace all our write() calls with code that appends the string to a buffer, and then write() this buffer out at the end.
struct abuf { // dynamic string type that supports one operation: appending.
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0} // acts as a constructor for our abuf type.


// append operation
void abAppend(struct abuf *ab, const char *s, int len){
    // make sure we allocate enough memory to hold the new string. 
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;

    // copy the string s after the end of the current data in the buffer, 
    memcpy(&new[ab->len], s, len); // and we update the pointer and length of the abuf to the new values.
    ab->b = new;
    ab->len += len;
}


//destructor that deallocates the dynamic memory used by an abuf.
void abFree(struct abuf *ab){
    free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab){
    int y;

    for(y = 0; y < E.screenrows; y++){
        /***
         * We use the welcome buffer and snprintf() to interpolate our KILO_VERSION string into the welcome message. 
         * We also truncate the length of the string in case the terminal is too tiny to fit our welcome message.
         * ***/
        if (y == E.screenrows /3){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                "Kilo editor -- version %s", KILO_VERSION);
            
            if( welcomelen > E.screencols) welcomelen = E.screencols;
            // center the message
            int padding = (E.screencols - welcomelen) / 2; // divide the screen width by 2, subtract half of the string’s length from that
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            /***
             * That tells you how far from the left edge of the screen you should start printing the string. 
             * So we fill that space with space characters, except for the first character, which should be a tilde.
             * ***/
            while (padding--) abAppend(ab, " ", 1);

            abAppend(ab, welcome, welcomelen);
        }else{
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3); // The K command (Erase In Line) erases part of the current line.

        if (y < E.screenrows -1) {
            
            abAppend(ab, "\r\n",2);
        }
    }
}

void editorRefreshScreen() {
    
    struct abuf ab = ABUF_INIT;
    // The h and l commands (Set Mode, Reset Mode) are used to turn on and turn off various terminal features or “modes”.
    abAppend(&ab, "\x1b[?25l", 6); // hides the cursor before refreshing screen to avoid flicker effect

    // H command (Cursor Position) to position the cursor.
    abAppend(&ab, "\x1b[H", 3);

    // draws a column of tildes at the left part of the screen
    editorDrawRows(&ab);

    // After we’re done drawing, we do another <esc>[H escape sequence to reposition the cursor back up at the top-left corner.
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx +1); // We add 1 to E.cy and E.cx to convert from 0-indexed values to the 1-indexed values that the terminal uses.
    abAppend(&ab, buf, strlen(buf));

    // The h and l commands (Set Mode, Reset Mode) are used to turn on and turn off various terminal features or “modes”.
    abAppend(&ab, "\x1b[?25h", 6);

    // finally we use just one write() to format our screen as wished
    write(STDOUT_FILENO, ab.b, ab.len);
    // then we free the memory of the struct abuf ab
    abFree(&ab);
}

/*** input ***/


// makes the cursor move around by using w,s,a,d keys
void editorMoveCursor(char key) {
    switch (key) {
        case 'a':
            E.cx--;
            break;
        case 'd':
            E.cx++;
            break;
        case 'w':
            E.cy--;
            break;
        case 's':
            E.cy++;
            break;
    }
}

// waits for a keypress, and then handles it.
void editorProcessKeypress(){
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            // quiting
            // clears screen after quiting the program
            // see editorRefreshScreen
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        
        // cursor movement
        case 'w':
        case 's':
        case 'a':
        case 'd':
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

// initializes all fields in E struct
void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
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

// next: CH3 Arrow Keys