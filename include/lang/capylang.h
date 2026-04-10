#ifndef LANG_CAPYLANG_H
#define LANG_CAPYLANG_H

#include <stdint.h>
#include <stddef.h>

#define CAPYLANG_VERSION_MAJOR 0
#define CAPYLANG_VERSION_MINOR 1
#define CAPYLANG_VERSION_PATCH 0

#define CL_TOKEN_MAX     128
#define CL_STRING_MAX    1024
#define CL_STACK_MAX     256
#define CL_LOCALS_MAX    64
#define CL_GLOBALS_MAX   128
#define CL_FUNCTIONS_MAX 64
#define CL_BYTECODE_MAX  4096
#define CL_CALLSTACK_MAX 32

enum cl_token_type {
  CL_TOK_EOF = 0, CL_TOK_INT, CL_TOK_FLOAT, CL_TOK_STRING, CL_TOK_IDENT,
  CL_TOK_PLUS, CL_TOK_MINUS, CL_TOK_STAR, CL_TOK_SLASH, CL_TOK_PERCENT,
  CL_TOK_EQ, CL_TOK_NEQ, CL_TOK_LT, CL_TOK_GT, CL_TOK_LEQ, CL_TOK_GEQ,
  CL_TOK_AND, CL_TOK_OR, CL_TOK_NOT, CL_TOK_ASSIGN, CL_TOK_LPAREN,
  CL_TOK_RPAREN, CL_TOK_LBRACE, CL_TOK_RBRACE, CL_TOK_LBRACKET,
  CL_TOK_RBRACKET, CL_TOK_COMMA, CL_TOK_DOT, CL_TOK_COLON, CL_TOK_SEMICOLON,
  CL_TOK_ARROW, CL_TOK_IF, CL_TOK_ELSE, CL_TOK_WHILE, CL_TOK_FOR,
  CL_TOK_FN, CL_TOK_LET, CL_TOK_RETURN, CL_TOK_TRUE, CL_TOK_FALSE,
  CL_TOK_NIL, CL_TOK_PRINT, CL_TOK_IMPORT, CL_TOK_STRUCT, CL_TOK_BREAK,
  CL_TOK_CONTINUE, CL_TOK_NEWLINE, CL_TOK_ERROR
};

enum cl_value_type {
  CL_VAL_NIL = 0, CL_VAL_INT, CL_VAL_FLOAT, CL_VAL_BOOL, CL_VAL_STRING,
  CL_VAL_ARRAY, CL_VAL_FUNCTION, CL_VAL_NATIVE_FN, CL_VAL_OBJECT
};

enum cl_opcode {
  CL_OP_NOP = 0, CL_OP_PUSH, CL_OP_POP, CL_OP_ADD, CL_OP_SUB, CL_OP_MUL,
  CL_OP_DIV, CL_OP_MOD, CL_OP_NEG, CL_OP_EQ, CL_OP_NEQ, CL_OP_LT,
  CL_OP_GT, CL_OP_LEQ, CL_OP_GEQ, CL_OP_AND, CL_OP_OR, CL_OP_NOT,
  CL_OP_LOAD_LOCAL, CL_OP_STORE_LOCAL, CL_OP_LOAD_GLOBAL,
  CL_OP_STORE_GLOBAL, CL_OP_JUMP, CL_OP_JUMP_IF_FALSE, CL_OP_CALL,
  CL_OP_RETURN, CL_OP_PRINT, CL_OP_HALT, CL_OP_PUSH_STRING,
  CL_OP_PUSH_TRUE, CL_OP_PUSH_FALSE, CL_OP_PUSH_NIL, CL_OP_DUP,
  CL_OP_INDEX, CL_OP_STORE_INDEX, CL_OP_ARRAY_NEW, CL_OP_ARRAY_PUSH,
  CL_OP_LEN, CL_OP_CONCAT
};

struct cl_token {
  enum cl_token_type type;
  char text[CL_TOKEN_MAX];
  int64_t int_val;
  double float_val;
  uint32_t line;
  uint32_t col;
};

struct cl_value {
  enum cl_value_type type;
  union {
    int64_t i;
    double f;
    int b;
    char *s;
    void *obj;
  };
};

typedef struct cl_value (*cl_native_fn)(struct cl_value *args, int argc);

struct cl_function {
  char name[CL_TOKEN_MAX];
  uint32_t ip;
  uint8_t arity;
  uint8_t local_count;
};

struct cl_vm {
  uint8_t bytecode[CL_BYTECODE_MAX];
  uint32_t bytecode_len;
  struct cl_value stack[CL_STACK_MAX];
  int32_t sp;
  struct cl_value globals[CL_GLOBALS_MAX];
  char global_names[CL_GLOBALS_MAX][CL_TOKEN_MAX];
  uint32_t global_count;
  struct cl_function functions[CL_FUNCTIONS_MAX];
  uint32_t function_count;
  uint32_t ip;
  struct { uint32_t ip; uint32_t bp; } callstack[CL_CALLSTACK_MAX];
  int32_t csp;
  int32_t bp;
  int running;
  int error;
  char error_msg[256];
  char string_pool[CL_STRING_MAX];
  uint32_t string_pool_used;
};

void cl_vm_init(struct cl_vm *vm);
void cl_vm_reset(struct cl_vm *vm);
int cl_compile(struct cl_vm *vm, const char *source);
int cl_run(struct cl_vm *vm);
int cl_run_string(struct cl_vm *vm, const char *source);
int cl_run_file(struct cl_vm *vm, const char *path,
                int (*read_fn)(const char *path, char *buf, size_t sz, size_t *len));
void cl_register_native(struct cl_vm *vm, const char *name, cl_native_fn fn);
struct cl_value cl_value_int(int64_t v);
struct cl_value cl_value_float(double v);
struct cl_value cl_value_bool(int v);
struct cl_value cl_value_string(struct cl_vm *vm, const char *s);
struct cl_value cl_value_nil(void);
const char *cl_type_name(enum cl_value_type type);
const char *cl_error_message(struct cl_vm *vm);

#endif /* LANG_CAPYLANG_H */
