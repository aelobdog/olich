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
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#include "config.h"
#include "synhl.h"

/* defines */

#define _DEFAULT_SOURCE

#define OLICH_VERSION "0.0.1"
#define CTRL(k) ((k) & 0x1f)
#define BUFFER_INIT {NULL, 0}

/* handling special keys */

enum special_keys {
   BACKSPACE = 127,
   ARROWL = 1000,
   ARROWR,
   ARROWU,
   ARROWD,
   DELETE,
   END,
   HOME,
   ESC
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
   unsigned char *highlighted;
   char *render;
   char *data;
} ed_row_data;

struct editor_config {
   struct termios init_termios;
   ed_row_data *rows_data;
   struct editor_syntax *syntax;
   char *filename;
   char status_extra[80];
   time_t statis_extra_time;
   int numrows;
   int rows;
   int cols;
   int cx;
   int cy;
   int rx;
   int rowoff;
   int coloff;
   int mod;
} E;

ssize_t getline(char** one, size_t* two, FILE* three);
char    *strdup(const char *string);
char* editor_prompt(char *prompt, void (*callback)(char*, int));
void editor_update_hl(ed_row_data *row);

/* row operations */

int cx_to_rx(ed_row_data *row, int cx) {
   int rx;
   int j;
   rx = 0;
   for (j = 0; j < cx; j++) {
      if (row->data[j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
      rx++;
   }
   return rx;
}

int rx_to_cx(ed_row_data *row, int rx) {
   int cx;
   int rx_now;
   rx_now = 0; 
   for (cx = 0; cx < row->size; cx++) {
      if (row->data[cx] == '\t') rx_now += (TAB_STOP - 1) - (rx_now % TAB_STOP);
      rx_now++;
      if (rx_now > rx) return cx;
   }
   return cx;
}

void editor_update_row(ed_row_data *row) {
   int j;
   int idx;
   int tabs;

   tabs = 0;
   for (j = 0; j < row->size; j++) {
      if (row->data[j] == '\t') tabs++;
   }
   
   free(row->render);
   row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

   idx = 0;
   for (j = 0; j < row->size; j++) {
      if (row->data[j] == '\t') {
         row->render[idx++] = ' ';
         while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
      } 
      else row->render[idx++] = row->data[j];
   }
   row->render[idx] = '\0';
   row->rensize = idx;
   editor_update_hl(row);
}

void editor_insert_row(int current, char *str, size_t len) {
   if (current < 0 || current > E.numrows) return;

   E.rows_data = realloc(E.rows_data, sizeof(ed_row_data) * (E.numrows + 1));
   memmove(&E.rows_data[current+1], &E.rows_data[current], sizeof(ed_row_data) * (E.numrows - current));

   E.rows_data[current].size = len;
   E.rows_data[current].data = malloc(len + 1);
   memcpy(E.rows_data[current].data, str, len);
   E.rows_data[current].data[len] = '\0';

   E.rows_data[current].rensize = 0;
   E.rows_data[current].render = NULL;
   E.rows_data[current].highlighted = NULL;
   editor_update_row(&E.rows_data[current]);
   
   E.numrows++;
   E.mod++;
}

void editor_free_row(ed_row_data *row) {
   free(row->highlighted);
   free(row->render);
   free(row->data);
}

void editor_del_row(int row_num) {
   if (row_num < 0 || row_num >= E.numrows) return;
   editor_free_row(&E.rows_data[row_num]);
   memmove(
      &E.rows_data[row_num], 
      &E.rows_data[row_num + 1], 
      sizeof(ed_row_data) * (E.numrows - row_num - 1)
   );
   E.numrows--;
   E.mod++;
}

void editor_append_to_row(ed_row_data *row, char *s, size_t len) {
   row->data = realloc(row->data, row->size + len + 1);
   memcpy(&row->data[row->size], s, len);
   row->size += len;
   row->data[row->size] = '\0';
   editor_update_row(row);
   E.mod++;
}

void editor_put_char_in_row(ed_row_data *row, int pos, int c) {
   if (pos < 0 || pos > row->size) pos = row->size;
   row->data = realloc(row->data, row->size + 2);
   memmove(&row->data[pos+1], &row->data[pos], row->size - pos + 1);
   row->size++;
   row->data[pos] = c;
   editor_update_row(row);
   E.mod++;
}

/* editor operations */

void insert_char(int c) {
   if (E.cy == E.numrows) editor_insert_row(E.numrows, "", 0);
   editor_put_char_in_row(&E.rows_data[E.cy], E.cx, c);
   E.cx++;
} 

void insert_newline() {
   if (E.cx == 0) editor_insert_row(E.cy, "", 0);
   else {
      ed_row_data *row;
      row = &E.rows_data[E.cy];
      editor_insert_row(E.cy+1, &row->data[E.cx], row->size - E.cx);
      row = &E.rows_data[E.cy];
      row->size = E.cx;
      row->data[row->size] = '\0';
      editor_update_row(row);
   }
   E.cy++;
   E.cx = 0;
}

void editor_del_char_in_row(ed_row_data *row, int pos) {
   if (pos < 0 || pos >= row->size) return;
   memmove(&row->data[pos], &row->data[pos+1], row->size - pos);
   row->size--;
   editor_update_row(row);
   E.mod++;
}

void delete_char() {
   ed_row_data *row;
   
   if (E.cy == E.numrows) return;
   if (E.cx == 0 && E.cy == 0) return;

   row = &E.rows_data[E.cy];
   if (E.cx > 0) {
      editor_del_char_in_row(row, E.cx-1);
      E.cx--;
   } else {
      E.cx = E.rows_data[E.cy-1].size;
      editor_append_to_row(&E.rows_data[E.cy-1], row->data, row->size);
      editor_del_row(E.cy);
      E.cy--;
   }
}

/* output */

void scroll_editor() {
   E.rx = 0;
   if (E.cy < E.numrows) E.rx = cx_to_rx(&E.rows_data[E.cy], E.cx);

   if (E.cy < E.rowoff) E.rowoff = E.cy;
   if (E.cy >= E.rowoff + E.rows) E.rowoff = E.cy - E.rows + 1;
   if (E.rx < E.coloff) E.coloff = E.rx;
   if (E.rx >= E.coloff + E.cols) E.coloff = E.rx - E.cols + 1;
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
         int j;
         char* c;
         unsigned char* hl;
         int curcolor;
         
         len = E.rows_data[filerow].rensize - E.coloff;
         if (len < 0) len = 0;
         if (len > E.cols)  len = E.cols;
         curcolor = -1;
         c = &E.rows_data[filerow].render[E.coloff];
         hl = &E.rows_data[filerow].highlighted[E.coloff];

         for (j = 0; j < len; j++) {
            if (hl[j] == HL_NORMAL) {
               if (curcolor != -1) { 
                  buffer_append(buf, "\x1b[39m", 5);
                  curcolor = -1;
               }
               buffer_append(buf, &c[j], 1);  
            } else {
               int color;
               char cbuf[16];
               int clen;
               
               color = hl_colors(hl[j]);
               if (color != curcolor) {
                  curcolor = color;
                  clen = snprintf(cbuf, sizeof(cbuf), "\x1b[%dm", color);
                  buffer_append(buf, cbuf, clen);
               }
               buffer_append(buf, &c[j], 1);
            }
         }
         buffer_append(buf, "\x1b[39m", 5);
      }
      buffer_append(buf, "\x1b[K", 3);
      buffer_append(buf, "\r\n", 2);
   }
}

void draw_statusbar(struct buffer *buf) {
   int len;
   int rlen;
   char l_status_info[80];
   char r_status_info[80];

   buffer_append(buf, "\x1b[7m", 4);

   len = snprintf(
         l_status_info, 
         sizeof(l_status_info),
         "  %.20s %s  | %d lines |",
         E.filename ? E.filename : "[No Name]",
         E.mod ? "[+]" : "",
         E.numrows
   );

   rlen = snprintf(
         r_status_info, 
         sizeof(r_status_info), 
         " [ %s ] [ %d / %d ]",
         E.syntax ? E.syntax->filetype : "text",
         E.cy + 1,
         E.numrows
   ); 

   if (len > E.cols) len = E.cols;
   buffer_append(buf, l_status_info, len);

   while (len < E.cols) {
      if (E.cols - len == rlen) {
         buffer_append(buf, r_status_info, rlen);
         break;
      } else {
         buffer_append(buf, " ", 1);
         len++;
      }
   }
   buffer_append(buf, "\x1b[m", 3);
   buffer_append(buf, "\r\n", 2);
}

void draw_extra_bar(struct buffer *buf) {
   int msglen;
   buffer_append(buf, "\x1b[K", 3);
   msglen = strlen(E.status_extra);
   if (msglen > E.cols) msglen = E.cols;
   if (msglen && time(NULL) - E.statis_extra_time < 5) {
      buffer_append(buf, E.status_extra, msglen);
   }
}

void refresh_screen() {
   char cposbuf[32];
   struct buffer buf = BUFFER_INIT;

   scroll_editor();
   
   buffer_append(&buf, "\x1b[?25l", 6);
   buffer_append(&buf, "\x1b[H", 3);
   draw_rows(&buf);   
   draw_statusbar(&buf);
   draw_extra_bar(&buf);

   snprintf(cposbuf, sizeof(cposbuf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1 , (E.rx - E.coloff) + 1);
   buffer_append(&buf, cposbuf, strlen(cposbuf));

   buffer_append(&buf, "\x1b[?25h", 6);
   write(STDOUT_FILENO, buf.data, buf.len);
   buffer_free(&buf);
}

void set_status_extra(const char *fmt, ...) {
   va_list params;
   va_start(params, fmt);
   vsnprintf(
      E.status_extra, 
      sizeof(E.status_extra),
      fmt, 
      params 
   );
   va_end(params);
   E.statis_extra_time = time(NULL);
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
            case 'P': return DELETE;
         }
      }
      return '\x1b';
   } 
   else if (c == HOME_KEY) return HOME;
   else if (c == END_KEY) return END;
   else if (c == NEXTLINE) return ARROWD;
   else if (c == PREVLINE) return ARROWU;
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

/* syntax highlighting */

int is_separator(int c) {
   return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editor_update_hl(ed_row_data *row) {
   int i;
   char c;
   int prev_sep;
   int in_string;
   char *scs;
   int scs_len;
   unsigned char prev_hl;

   row->highlighted = realloc(row->highlighted, row->rensize);
   memset(row->highlighted, HL_NORMAL, row->rensize); 

   if (E.syntax == NULL) return;
   scs = E.syntax->sl_cmt_start;
   scs_len = scs ? strlen(scs) : 0;

   prev_sep = 1;
   i = 0;
   in_string = 0;
   while (i < row->rensize) {
      c = row->render[i];
      prev_hl = (i > 0) ? row->highlighted[i-1] : HL_NORMAL;

      if (scs_len && !in_string) {
         if (!strncmp(&row->render[i], scs, scs_len)) {
            memset(&row->highlighted[i], HL_COMMENT, row->rensize - i);
            break;
         }
      }
      
      if (E.syntax->flags & HL_STRING) {
         if (in_string) {
            row->highlighted[i] = HL_STRING;
            if (c == '\\' && i+1 < row->rensize) {
               row->highlighted[i+1] = HL_STRING;
               i += 2;
               continue;
            }
            if (c == in_string) in_string = 0;
            i++;
            prev_sep = 1;
            continue;
         } else {
            if (c == '"' || c == '\'') {
               in_string = c;
               row->highlighted[i] = HL_STRING;
               i++;
               continue;
            }
         }
      }

      if (E.syntax->flags & HL_NUMBERS) {
         if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
            row->highlighted[i] = HL_NUMBER;
            i++;
            prev_sep = 0;
            continue;
         }
      }

      prev_sep = is_separator(c);
      i++;
   }
}

void select_highlighting() {
   unsigned int i;
   unsigned int j;
   int is_ext;
   int loopvar;
   char *ext;

   E.syntax = NULL;
   if (E.filename == NULL) return;

   ext = strrchr(E.filename, '.');
   
   for (i = 0; i < HL_DB_ENTRIES; i++) {
      struct editor_syntax *edsyn = &HL_DB[i];
      j = 0;
      while (edsyn->filematch[j]) {
         is_ext = (edsyn->filematch[j][0] == '.');
         if ((is_ext && ext && !strcmp(ext, edsyn->filematch[j])) ||
            (!is_ext && strstr(E.filename, edsyn->filematch[j]))) {
            E.syntax = edsyn;

            for (loopvar = 0; loopvar < E.numrows; loopvar++) editor_update_hl(&E.rows_data[loopvar]);

            return;
         }
         j++;
      }
   }
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

   free(E.filename);
   E.filename = strdup(filename);
   select_highlighting();

   while ((linelen = getline(&line, &linecap, file_handle)) != -1) {
      while (linelen > 0 && 
            (line[linelen-1] == '\n' || 
             line[linelen-1] == '\r'))
         linelen--;
      editor_insert_row(E.numrows, line, linelen);
   }

   free(line);
   fclose(file_handle);
   E.mod = 0;
}

char *editor_to_string(int *len) {
   int totlen; 
   int j;
   char *content;
   char *pointer;

   content = NULL;
   pointer = NULL;
   totlen = 0;
   
   for (j = 0; j < E.numrows; j++)
      totlen += E.rows_data[j].size + 1;
   
   *len = totlen;
   content = malloc(totlen);
   pointer = content;

   for (j = 0; j < E.numrows; j++) {
      memcpy(pointer, E.rows_data[j].data, E.rows_data[j].size);
      pointer += E.rows_data[j].size;
      *pointer = '\n';
      pointer++;
   }

   return content; 
}

void save_editor() {
   int len;
   char *content;
   int fd;

   if (E.filename == NULL) {
      E.filename = editor_prompt("Save as : %s [ESC to cancel]", NULL);
      if (E.filename == NULL) {
         set_status_extra("Did not save file");
         return;
      }
   }

   select_highlighting();
   content = editor_to_string(&len);
   fd = open(E.filename, O_RDWR | O_CREAT, 0644);
   
   if (fd != -1) {
      if (ftruncate(fd, len) != -1) {
         if (write(fd, content, len) == len) {
            close(fd);
            free(content);
            E.mod = 0;
            set_status_extra("%d bytes written.", len);
            return;
         }
      }
      close(fd);
   }
   free(content);
   set_status_extra("cannot save ! %s", strerror(errno));
}

/* incremental search */

void callback_find(char* search_for, int key) {
   static int last = -1;
   static int direction = 1;

   static int prev_instance_line;
   static char *prev_instance = NULL;

   int i;
   int current;
   ed_row_data *row;
   char* match;

   if (prev_instance) {
      memcpy(E.rows_data[prev_instance_line].highlighted, prev_instance, E.rows_data[prev_instance_line].rensize);
      free(prev_instance);
      prev_instance = NULL;
   }

   if (key == '\r' || key == '\x1b') {
      last = -1;
      direction = 1;
      return;
   } else if (key == ARROWR || key == ARROWD) {
      direction = 1;
   } else if (key == ARROWL || key == ARROWU) {
      direction = -1;
   } else {
      last = -1;
      direction = 1;
   }

   if (last == -1) direction = 1;
   current = last; 

   for (i = 0; i < E.numrows; i++) {
      current += direction;
      if (current == -1) current = E.numrows - 1;
      else if (current == E.numrows) current = 0;

      row = &E.rows_data[current];
      match = strstr(row->render, search_for);
      if (match) {
         last = current;
         E.cy = current;
         E.cx = rx_to_cx(row, match - row->render);
         E.rowoff = E.numrows;

         prev_instance_line = current;
         prev_instance = malloc(row->rensize);
         memcpy(prev_instance, row->highlighted, row->rensize);
         memset(&row->highlighted[match - row->render], HL_MATCH, strlen(search_for));

         break;
      }
   }
}

void find_editor() {
   int old_cx; 
   int old_cy;
   int old_coloff; 
   int old_rowoff;
   char* search_for; 

   old_cx = E.cx;
   old_cy = E.cy;
   old_coloff = E.coloff;
   old_rowoff = E.rowoff;

   search_for = editor_prompt("Search : %s [ESC to cancel]", callback_find);
   
   if (search_for) free(search_for);
   else {
      E.cx = old_cx;
      E.cy = old_cy;
      E.coloff = old_coloff;
      E.rowoff = old_rowoff;
   }
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
         } else if (row && E.cx == row->size) {
            E.cy++;
            E.cx = 0;
         }
         break;
      case ARROWD:
         if (E.cy < E.numrows) E.cy++;
         break;
      case ARROWL:
         if (E.cx != 0) E.cx--;
         else if (E.cy > 0) {
            E.cy--;
            E.cx = E.rows_data[E.cy].size;
         }
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
   static int quit_times = QUIT_CONF_CONTROL;
   int c = read_key();
   switch (c) {

      case '\r':
         insert_newline();
         break;
      
      case QUIT_KEY:
         if (E.mod && quit_times > 0) {
            set_status_extra(
               "%d unsaved changes ! Close %d times more to quit.",
               E.mod,
               quit_times
            );
            quit_times--;
            return;
         }
         write(STDOUT_FILENO, "\x1b[2J", 3);
         write(STDOUT_FILENO, "\x1b[H", 3);
         exit(0);
         break;
      
      case ARROWL: case ARROWU: case ARROWR: case ARROWD: case HOME: case END:
         cursor_move(c);
         break;
      
      case BACKSPACE: case DELETE: case CTRL('h'):
         if (c == DELETE) cursor_move(ARROWR); 
         delete_char();
         break;

      case '\x1b': case CTRL('l'):
         break;

      case SAVE_KEY:
         save_editor();
         break;

      case FIND_KEY:
         find_editor();
         break;

      default:
         insert_char(c);
   }
   quit_times = QUIT_CONF_CONTROL;
}

char* editor_prompt(char *prompt, void (*callback)(char*, int)) {
   size_t input_buf_size; 
   size_t input_buf_len; 
   char *input_buf;
   int c;

   input_buf_size = 128;
   input_buf = malloc(input_buf_size);
   input_buf_len = 0;
   input_buf[0] = '\0';

   while(1) {
      set_status_extra(prompt, input_buf);
      refresh_screen();

      c = read_key();
      if (c == DELETE || c == BACKSPACE || c == CTRL('h')) {
         if (input_buf_len != 0) input_buf[--input_buf_len] = '\0';
      } else if (c == '\x1b') {
         set_status_extra("");
         if (callback) callback(input_buf, c);
         free(input_buf);
         return NULL;
      } else if (c == '\r') {
         if (input_buf_len != 0) {
            set_status_extra("");
            if (callback) callback(input_buf, c);
            return input_buf;
         }
      }
      else if (!iscntrl(c) && c < 128) {
         if (input_buf_len == input_buf_size - 1) {
            input_buf_size *= 2;
            input_buf = realloc(input_buf, input_buf_size);
         }
         input_buf[input_buf_len++] = c;
         input_buf[input_buf_len] = '\0';
      }
      if (callback) callback(input_buf, c);
   }
}

/* initialization */

void init() {
   E.cx = 0;
   E.cy = 0;
   E.rx = 0;
   E.rowoff = 0;
   E.coloff = 0;
   E.numrows = 0;
   E.mod = 0;
   E.rows_data = NULL;
   E.filename = NULL;
   E.syntax = NULL;
   E.statis_extra_time = 0;
   E.status_extra[0] = '\0';
   if (term_size(&E.rows, &E.cols) == -1) die("term_size");
   E.rows -= 2;
}

/* execution entry point */

int main(int argc, char *argv[]) {
   enable_raw();
   init();

   if (argc >= 2) {
      open_editor(argv[1]);
   }

   set_status_extra("Read 'config.h' for keybindings");

   while (1) {
      refresh_screen();
      key_proc();
   }
   disable_raw();
   return 0;
}
