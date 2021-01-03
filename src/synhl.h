#ifndef SYNHL_H
#define SYNHL_H

#define HL_NUMBERS (1<<0)
#define HL_STRINGS (1<<1)

#define HL_DB_ENTRIES (sizeof(HL_DB) / sizeof(HL_DB[0]))

struct editor_syntax {
   char *filetype;
   char **filematch;
   char **keyword;
   char **datatypes;
   char *sl_cmt_start;
   /* char *ml_cmt_start; */

   int flags;
} editor_syntax;

char *C_HL_FE[] = { ".c", ".h", ".cpp", 0};
char *GO_HL_FE[] = { ".go", 0 };

char *C_HL_KW[] = {"switch", "if", "while", "for", "break", "continue",
   "return", "else", "struct", "union", "typedef", "static", "enum", 
   "class", "case", "#include", 0 
};

char *C_HL_DT[] = {
   "int", "long", "double", "float", "char", "unsigned", "signed", "void", 0
};

char *GO_HL_KW[] = {
   "break", "default", "func", "interface", "select",
   "case"   , "defer"    , "go"  , "map", "struct",
   "chan"   , "else"    , "goto", "package", "switch",
   "const"   , "fallthrough", "if"  , "range", "type",
   "continue", "for"    , "import", "return", "var", 0
};

char *GO_HL_DT[] = {
   /* add datatypes here */
   "int", "bool", "string", 0
};

struct editor_syntax HL_DB[] = {
   {
      "c",
      C_HL_FE,
      C_HL_KW,
      C_HL_DT,
      "//",
      HL_NUMBERS | HL_STRINGS
   }, 
   {
      "go",
      GO_HL_FE,
      GO_HL_KW,
      GO_HL_DT,
      "//",
      HL_NUMBERS | HL_STRINGS
   },
};

#endif
