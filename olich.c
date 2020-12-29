/* includes */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

#include "config.h"

/* defines */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#define OLICH_VERSION "0.0.1"
#define CTRL(k) ((k) & 0x1f)
#define BUFFER_INIT {NULL, 0}

ssize_t getline(char** one, size_t* two, FILE* three);

/* handling special keys */

enum special_keys {
   ARROWL = 1000,
   ARROWR,
   ARROWU,
   ARROWD,
   END,
   HOME
};

/* custom strings */

struct buffer {
   char *data;
   int len;
};

void buffer_append(struct buffer *buf, const char *s, int len) {
   char *new = realloc(buf->data, buf->len + len);
   if (new == NULL) return;
   memcpy(&new[buf->len], s, len);
   buf->data = new;
   buf->len += len;
}

void buffer_free(struct buffer *buf) {
   free(buf->data);
}

/* data */

typedef struct ed_row_data {
   int size;
   int rensize;
   char *render;
   char *data;
} ed_row_data;

struct editor_config {
   struct termios init_termios;
   ed_row_data *rows_data;
   int numrows;
   int rows;
   int cols;
   int cx;
   int cy;
   int rowoff;
   int coloff;
} E;

/* output */

void scroll_editor() {
   if (E.cy < E.rowoff) E.rowoff = E.cy;
   if (E.cy >= E.rowoff + E.rows) E.rowoff = E.cy - E.rows + 1;
   if (E.cx < E.coloff) E.coloff = E.cx;
   if (E.cx >= E.coloff + E.cols) E.coloff = E.cx - E.cols + 1;
}

void draw_rows(struct buffer *buf) {
   int y;
   int padding;
   for (y = 0; y < E.rows; y++) {
      int filerow;
      filerow = y + E.rowoff;
      if (filerow >= E.numrows) {
         if (E.numrows == 0 && y == E.rows / 3) {
            char welcome[80];
            int welcomelen = snprintf(
                welcome, 
                sizeof(welcome),
                "Olich editor -- version %s", OLICH_VERSION
                );
            if (welcomelen > E.cols) welcomelen = E.cols;

            padding = (E.cols - welcomelen) / 2;
            if (padding) {
               buffer_append(buf, ".", 1);
               padding --;
            }
            while (padding--) buffer_append(buf, " ", 1);
            buffer_append(buf, welcome, welcomelen);

         } else {
            buffer_append(buf, ".", 1);
         }
      } else {
         int len;
         len = E.rows_data[filerow].rensize - E.coloff;
         if (len < 0) len = 0;
         if (len > E.cols)  len = E.cols;
         buffer_append(buf, &E.rows_data[filerow].render[E.coloff], len);
      }

         buffer_append(buf, "\x1b[K", 3);
         if (y < E.rows - 1) buffer_append(buf, "\r\n", 2);
   }
}

void refresh_screen() {
   char cposbuf[32];
   struct buffer buf = BUFFER_INIT;

   scroll_editor();
   
   buffer_append(&buf, "\x1b[?25l", 6);
   buffer_append(&buf, "\x1b[H", 3);
   draw_rows(&buf);   

   snprintf(cposbuf, sizeof(cposbuf), "\x1b[%d;%dH", E.cy - E.rowoff + 1 , E.cx - E.coloff + 1);
   buffer_append(&buf, cposbuf, strlen(cposbuf));

   buffer_append(&buf, "\x1b[?25h", 6);
   write(STDOUT_FILENO, buf.data, buf.len);
   buffer_free(&buf);
}

/* terminal functions */

void die(const char *s) {
   write(STDOUT_FILENO, "\x1b[2J", 3);
   write(STDOUT_FILENO, "\x1b[H", 3);
   perror(s);
   exit(1);
}

void disable_raw() {
   if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.init_termios) == -1) die("tcsetattr");
}

void enable_raw() {
   struct termios raw;
   
   if (tcgetattr(STDIN_FILENO, &E.init_termios) == -1) die("tcgetattr");
   atexit(disable_raw);
   raw = E.init_termios;
  
   /* setting the necessary terminal flags */
   raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
   raw.c_oflag &= ~(OPOST);
   raw.c_cflag |= (CS8);
   raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
   raw.c_cc[VMIN] = 0;
   raw.c_cc[VTIME] = 1;
  
   if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int read_key() {
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
         switch (seq[1]) {
            case 'A': return ARROWU;
            case 'B': return ARROWD;
            case 'C': return ARROWR;
            case 'D': return ARROWL;
         }
      }
      return '\x1b';
   } 
   else if (c == HOME_KEY) return HOME;
   else if (c == END_KEY) return END;
   else {
      return c;
   }
}

int cursor_pos(int *rows, int *cols) {
   char buf[32];
   unsigned int i = 0;

   if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
   
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

int term_size(int *rows, int *cols) {
   struct winsize ws;
   
   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
      if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
      read_key();
      return cursor_pos(rows, cols);
   } else {
      *cols = ws.ws_col;
      *rows = ws.ws_row;
      return 0;
   }
}

/* row operations */

void editor_update_row(ed_row_data *row) {
   int j;
   int idx;
   int tabs;

   tabs = 0;
   for (j = 0; j < row->size; j++) {
      if (row->data[j] == '\t') tabs++;
   }
   
   free(row->render);
   row->render = malloc(row->size + tabs*2 + 1);

   idx = 0;
   for (j = 0; j < row->size; j++) {
      if (row->data[j] == '\t') {
         row->render[idx++] = ' ';
         while (idx % 3 != 0) row->render[idx++] = ' ';
      } 
      else row->render[idx++] = row->data[j];
   }
   row->render[idx] = '\0';
   row->rensize = idx;
}

void editor_append_row(char *str, size_t len) {
   int current;

   E.rows_data = realloc(E.rows_data, sizeof(ed_row_data) * (E.numrows + 1));
   current = E.numrows;
   E.rows_data[current].size = len;
   E.rows_data[current].data = malloc(len + 1);
   memcpy(E.rows_data[current].data, str, len);
   E.rows_data[current].data[len] = '\0';

   E.rows_data[current].rensize = 0;
   E.rows_data[current].render = NULL;
   editor_update_row(&E.rows_data[current]);
   
   E.numrows++;
}

/* file io */

void open_editor(char *filename) {
   FILE *file_handle = fopen(filename, "r");
   char *line; 
   size_t linecap;
   ssize_t linelen;

   if (!file_handle) die("fopen");
   line = NULL;
   linecap = 0;

   while ((linelen = getline(&line, &linecap, file_handle)) != -1) {
      while (linelen > 0 && 
            (line[linelen-1] == '\n' || 
             line[linelen-1] == '\r'))
         linelen--;
      editor_append_row(line, linelen);
   }

   free(line);
   fclose(file_handle);
}

/* input */

void cursor_move(int key) {
   int rowlen;
   ed_row_data *row = (E.cy >= E.numrows) ? NULL : &E.rows_data[E.cy];
   switch (key) {
      case ARROWU:
         if (E.cy != 0) E.cy--;
         break;
      case ARROWR:
         if (row && E.cx < row->size) {
            E.cx++;
         }
         break;
      case ARROWD:
         if (E.cy < E.numrows) E.cy++;
         break;
      case ARROWL:
         if (E.cx != 0) E.cx--;
         break;
      case HOME: 
         E.cx = 0;
         while (isspace(row->data[E.cx])) E.cx++;
         break;
      case END : E.cx = row ? row->size : 0; break;
   }
   
   row = (E.cy >= E.numrows) ? NULL : &E.rows_data[E.cy];
   rowlen = row ? row->size : 0;
   if (E.cx > rowlen) E.cx = rowlen;
}

void key_proc() {
   int c = read_key();
   switch (c) {
      case QUIT_KEY:
         write(STDOUT_FILENO, "\x1b[2J", 3);
         write(STDOUT_FILENO, "\x1b[H", 3);
         exit(0);
         break;
      case ARROWL: case ARROWU: case ARROWR: case ARROWD: case HOME: case END:
         cursor_move(c);
         break;
   }
}

/* initialization */

void init() {
   E.cx = 0;
   E.cy = 0;
   E.rowoff = 0;
   E.coloff = 0;
   E.numrows = 0;
   E.rows_data = NULL;
   if (term_size(&E.rows, &E.cols) == -1) die("term_size");
}

/* execution entry point */

int main(int argc, char *argv[]) {
   enable_raw();
   init();

   if (argc >= 2) {
      open_editor(argv[1]);
   }

   while (1) {
      refresh_screen();
      key_proc();
   }
   return 0;
}
