#ifndef CONFIG_H
#define CONFIG_H

#define TAB_STOP 4 
#define QUIT_CONF_CONTROL 1
#define QUIT_KEY ('q' & 0x1f)
#define HOME_KEY ('a' & 0x1f)
#define END_KEY  ('e' & 0x1f)
#define SAVE_KEY ('s' & 0x1f)
#define FIND_KEY ('f' & 0x1f)

enum ed_highlighting {
   HL_NORMAL = 0,
   HL_NUMBER
};

/* this function returns the color code
 * for a particular highlighting code from
 * the enum above 
 *
 * Color table :
 *    30 black
 *    31 red
 *    32 green
 *    33 yellow
 *    34 blue
 *    35 magenta
 *    36 cyan
 *    37 white                          */

int hl_colors(int code) {
   switch (code) {
      case HL_NUMBER: return 31; 
      default : return 37;
   }
}

#endif
