#include "lang/capylang.h"
#include <stddef.h>

/* ---- helpers ---- */

static void cl_memset(void *dst, int v, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)v;
}

static void cl_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

static int cl_streq(const char *a, const char *b) {
  while (*a && *b) { if (*a != *b) return 0; a++; b++; }
  return *a == *b;
}

static size_t cl_strlen(const char *s) {
  size_t n = 0; while (s[n]) n++;
  return n;
}

static int cl_isdigit(char c) { return c >= '0' && c <= '9'; }
static int cl_isalpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int cl_isalnum(char c) { return cl_isalpha(c) || cl_isdigit(c); }

/* ---- values ---- */

struct cl_value cl_value_int(int64_t v)   { struct cl_value r; r.type = CL_VAL_INT;   r.i = v; return r; }
struct cl_value cl_value_float(double v)  { struct cl_value r; r.type = CL_VAL_FLOAT; r.f = v; return r; }
struct cl_value cl_value_bool(int v)      { struct cl_value r; r.type = CL_VAL_BOOL;  r.b = v; return r; }
struct cl_value cl_value_nil(void)        { struct cl_value r; r.type = CL_VAL_NIL;   r.i = 0; return r; }

struct cl_value cl_value_string(struct cl_vm *vm, const char *s) {
  struct cl_value r;
  r.type = CL_VAL_STRING;
  size_t len = cl_strlen(s);
  if (vm->string_pool_used + len + 1 <= CL_STRING_MAX) {
    r.s = vm->string_pool + vm->string_pool_used;
    cl_strcpy(r.s, s, len + 1);
    vm->string_pool_used += (uint32_t)(len + 1);
  } else {
    r.s = NULL;
  }
  return r;
}

const char *cl_type_name(enum cl_value_type type) {
  switch (type) {
    case CL_VAL_NIL: return "nil";
    case CL_VAL_INT: return "int";
    case CL_VAL_FLOAT: return "float";
    case CL_VAL_BOOL: return "bool";
    case CL_VAL_STRING: return "string";
    case CL_VAL_ARRAY: return "array";
    case CL_VAL_FUNCTION: return "function";
    case CL_VAL_NATIVE_FN: return "native_fn";
    case CL_VAL_OBJECT: return "object";
    default: return "unknown";
  }
}

const char *cl_error_message(struct cl_vm *vm) {
  return vm ? vm->error_msg : "no vm";
}

/* ---- VM ---- */

void cl_vm_init(struct cl_vm *vm) {
  cl_memset(vm, 0, sizeof(*vm));
  vm->sp = -1;
  vm->csp = -1;
  vm->bp = 0;
}

void cl_vm_reset(struct cl_vm *vm) { cl_vm_init(vm); }

void cl_register_native(struct cl_vm *vm, const char *name, cl_native_fn fn) {
  if (vm->global_count >= CL_GLOBALS_MAX) return;
  uint32_t idx = vm->global_count++;
  cl_strcpy(vm->global_names[idx], name, CL_TOKEN_MAX);
  vm->globals[idx].type = CL_VAL_NATIVE_FN;
  vm->globals[idx].obj = (void *)fn;
}

static int find_global(struct cl_vm *vm, const char *name) {
  for (uint32_t i = 0; i < vm->global_count; i++) {
    if (cl_streq(vm->global_names[i], name)) return (int)i;
  }
  return -1;
}

static void vm_error(struct cl_vm *vm, const char *msg) {
  vm->error = 1;
  vm->running = 0;
  cl_strcpy(vm->error_msg, msg, 256);
}

static void emit(struct cl_vm *vm, uint8_t op) {
  if (vm->bytecode_len < CL_BYTECODE_MAX)
    vm->bytecode[vm->bytecode_len++] = op;
}

static void emit_u32(struct cl_vm *vm, uint32_t val) {
  emit(vm, (uint8_t)(val & 0xFF));
  emit(vm, (uint8_t)((val >> 8) & 0xFF));
  emit(vm, (uint8_t)((val >> 16) & 0xFF));
  emit(vm, (uint8_t)((val >> 24) & 0xFF));
}

static void emit_i64(struct cl_vm *vm, int64_t val) {
  for (int i = 0; i < 8; i++) emit(vm, (uint8_t)((val >> (i * 8)) & 0xFF));
}

static uint32_t read_u32(struct cl_vm *vm) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= ((uint32_t)vm->bytecode[vm->ip++]) << (i * 8);
  return v;
}

static int64_t read_i64(struct cl_vm *vm) {
  int64_t v = 0;
  for (int i = 0; i < 8; i++) v |= ((int64_t)vm->bytecode[vm->ip++]) << (i * 8);
  return v;
}

static void push(struct cl_vm *vm, struct cl_value v) {
  if (vm->sp >= CL_STACK_MAX - 1) { vm_error(vm, "stack overflow"); return; }
  vm->stack[++vm->sp] = v;
}

static struct cl_value pop(struct cl_vm *vm) {
  if (vm->sp < 0) { vm_error(vm, "stack underflow"); return cl_value_nil(); }
  return vm->stack[vm->sp--];
}

/* ---- Lexer ---- */

struct cl_lexer {
  const char *src;
  uint32_t pos;
  uint32_t line;
  uint32_t col;
};

static char lex_peek(struct cl_lexer *l) { return l->src[l->pos]; }
static char lex_advance(struct cl_lexer *l) { char c = l->src[l->pos++]; if (c == '\n') { l->line++; l->col = 1; } else l->col++; return c; }

static void skip_whitespace(struct cl_lexer *l) {
  while (lex_peek(l) == ' ' || lex_peek(l) == '\t' || lex_peek(l) == '\r' ||
         lex_peek(l) == '#') {
    if (lex_peek(l) == '#') { while (lex_peek(l) && lex_peek(l) != '\n') lex_advance(l); }
    else lex_advance(l);
  }
}

static struct cl_token lex_next(struct cl_lexer *l) {
  struct cl_token tok;
  cl_memset(&tok, 0, sizeof(tok));
  tok.line = l->line;
  tok.col = l->col;

  skip_whitespace(l);
  char c = lex_peek(l);

  if (c == '\0') { tok.type = CL_TOK_EOF; return tok; }
  if (c == '\n') { lex_advance(l); tok.type = CL_TOK_NEWLINE; return tok; }

  if (cl_isdigit(c)) {
    int p = 0;
    while (cl_isdigit(lex_peek(l)) && p < CL_TOKEN_MAX - 1) tok.text[p++] = lex_advance(l);
    if (lex_peek(l) == '.') {
      tok.text[p++] = lex_advance(l);
      while (cl_isdigit(lex_peek(l)) && p < CL_TOKEN_MAX - 1) tok.text[p++] = lex_advance(l);
      tok.type = CL_TOK_FLOAT;
      double f = 0, frac = 0.1; int past_dot = 0;
      for (int i = 0; tok.text[i]; i++) {
        if (tok.text[i] == '.') { past_dot = 1; continue; }
        if (past_dot) { f += (tok.text[i] - '0') * frac; frac *= 0.1; }
        else f = f * 10 + (tok.text[i] - '0');
      }
      tok.float_val = f;
    } else {
      tok.type = CL_TOK_INT;
      tok.int_val = 0;
      for (int i = 0; tok.text[i]; i++) tok.int_val = tok.int_val * 10 + (tok.text[i] - '0');
    }
    tok.text[p] = '\0';
    return tok;
  }

  if (c == '"') {
    lex_advance(l);
    int p = 0;
    while (lex_peek(l) != '"' && lex_peek(l) != '\0' && p < CL_TOKEN_MAX - 1) {
      char ch = lex_advance(l);
      if (ch == '\\') {
        ch = lex_advance(l);
        switch (ch) { case 'n': ch = '\n'; break; case 't': ch = '\t'; break; case '\\': ch = '\\'; break; case '"': ch = '"'; break; }
      }
      tok.text[p++] = ch;
    }
    tok.text[p] = '\0';
    if (lex_peek(l) == '"') lex_advance(l);
    tok.type = CL_TOK_STRING;
    return tok;
  }

  if (cl_isalpha(c)) {
    int p = 0;
    while (cl_isalnum(lex_peek(l)) && p < CL_TOKEN_MAX - 1) tok.text[p++] = lex_advance(l);
    tok.text[p] = '\0';
    if (cl_streq(tok.text, "if"))       tok.type = CL_TOK_IF;
    else if (cl_streq(tok.text, "else"))     tok.type = CL_TOK_ELSE;
    else if (cl_streq(tok.text, "while"))    tok.type = CL_TOK_WHILE;
    else if (cl_streq(tok.text, "for"))      tok.type = CL_TOK_FOR;
    else if (cl_streq(tok.text, "fn"))       tok.type = CL_TOK_FN;
    else if (cl_streq(tok.text, "let"))      tok.type = CL_TOK_LET;
    else if (cl_streq(tok.text, "return"))   tok.type = CL_TOK_RETURN;
    else if (cl_streq(tok.text, "true"))     tok.type = CL_TOK_TRUE;
    else if (cl_streq(tok.text, "false"))    tok.type = CL_TOK_FALSE;
    else if (cl_streq(tok.text, "nil"))      tok.type = CL_TOK_NIL;
    else if (cl_streq(tok.text, "print"))    tok.type = CL_TOK_PRINT;
    else if (cl_streq(tok.text, "import"))   tok.type = CL_TOK_IMPORT;
    else if (cl_streq(tok.text, "break"))    tok.type = CL_TOK_BREAK;
    else if (cl_streq(tok.text, "continue")) tok.type = CL_TOK_CONTINUE;
    else tok.type = CL_TOK_IDENT;
    return tok;
  }

  lex_advance(l);
  tok.text[0] = c; tok.text[1] = '\0';
  switch (c) {
    case '+': tok.type = CL_TOK_PLUS; break;
    case '-': tok.type = (lex_peek(l) == '>') ? (lex_advance(l), CL_TOK_ARROW) : CL_TOK_MINUS; break;
    case '*': tok.type = CL_TOK_STAR; break;
    case '/': tok.type = CL_TOK_SLASH; break;
    case '%': tok.type = CL_TOK_PERCENT; break;
    case '(': tok.type = CL_TOK_LPAREN; break;
    case ')': tok.type = CL_TOK_RPAREN; break;
    case '{': tok.type = CL_TOK_LBRACE; break;
    case '}': tok.type = CL_TOK_RBRACE; break;
    case '[': tok.type = CL_TOK_LBRACKET; break;
    case ']': tok.type = CL_TOK_RBRACKET; break;
    case ',': tok.type = CL_TOK_COMMA; break;
    case '.': tok.type = CL_TOK_DOT; break;
    case ':': tok.type = CL_TOK_COLON; break;
    case ';': tok.type = CL_TOK_SEMICOLON; break;
    case '=': tok.type = (lex_peek(l) == '=') ? (lex_advance(l), CL_TOK_EQ) : CL_TOK_ASSIGN; break;
    case '!': tok.type = (lex_peek(l) == '=') ? (lex_advance(l), CL_TOK_NEQ) : CL_TOK_NOT; break;
    case '<': tok.type = (lex_peek(l) == '=') ? (lex_advance(l), CL_TOK_LEQ) : CL_TOK_LT; break;
    case '>': tok.type = (lex_peek(l) == '=') ? (lex_advance(l), CL_TOK_GEQ) : CL_TOK_GT; break;
    case '&': if (lex_peek(l) == '&') { lex_advance(l); tok.type = CL_TOK_AND; } break;
    case '|': if (lex_peek(l) == '|') { lex_advance(l); tok.type = CL_TOK_OR; } break;
    default: tok.type = CL_TOK_ERROR; break;
  }
  return tok;
}

/* ---- Simple compiler (single-pass to bytecode) ---- */

struct cl_compiler {
  struct cl_vm *vm;
  struct cl_lexer lex;
  struct cl_token current;
  struct cl_token previous;
  char locals[CL_LOCALS_MAX][CL_TOKEN_MAX];
  uint32_t local_count;
  int scope_depth;
};

static void comp_advance(struct cl_compiler *c) {
  c->previous = c->current;
  for (;;) {
    c->current = lex_next(&c->lex);
    if (c->current.type != CL_TOK_NEWLINE) break;
  }
}

static int comp_check(struct cl_compiler *c, enum cl_token_type t) {
  return c->current.type == t;
}

static int comp_match(struct cl_compiler *c, enum cl_token_type t) {
  if (!comp_check(c, t)) return 0;
  comp_advance(c);
  return 1;
}

static void compile_expr(struct cl_compiler *c);
static void compile_stmt(struct cl_compiler *c);

static int resolve_local(struct cl_compiler *c, const char *name) {
  for (int i = (int)c->local_count - 1; i >= 0; i--)
    if (cl_streq(c->locals[i], name)) return i;
  return -1;
}

static void compile_primary(struct cl_compiler *c) {
  struct cl_vm *vm = c->vm;
  if (comp_match(c, CL_TOK_INT)) {
    emit(vm, CL_OP_PUSH);
    emit_i64(vm, c->previous.int_val);
  } else if (comp_match(c, CL_TOK_STRING)) {
    int idx = find_global(vm, "__str_pool__");
    (void)idx;
    emit(vm, CL_OP_PUSH_STRING);
    uint32_t off = vm->string_pool_used;
    size_t len = cl_strlen(c->previous.text);
    if (off + len + 1 <= CL_STRING_MAX) {
      cl_strcpy(vm->string_pool + off, c->previous.text, len + 1);
      vm->string_pool_used += (uint32_t)(len + 1);
    }
    emit_u32(vm, off);
  } else if (comp_match(c, CL_TOK_TRUE)) {
    emit(vm, CL_OP_PUSH_TRUE);
  } else if (comp_match(c, CL_TOK_FALSE)) {
    emit(vm, CL_OP_PUSH_FALSE);
  } else if (comp_match(c, CL_TOK_NIL)) {
    emit(vm, CL_OP_PUSH_NIL);
  } else if (comp_match(c, CL_TOK_IDENT)) {
    int local = resolve_local(c, c->previous.text);
    if (local >= 0) {
      emit(vm, CL_OP_LOAD_LOCAL);
      emit_u32(vm, (uint32_t)local);
    } else {
      int g = find_global(vm, c->previous.text);
      if (g < 0) {
        g = (int)vm->global_count;
        cl_strcpy(vm->global_names[vm->global_count], c->previous.text, CL_TOKEN_MAX);
        vm->globals[vm->global_count] = cl_value_nil();
        vm->global_count++;
      }
      emit(vm, CL_OP_LOAD_GLOBAL);
      emit_u32(vm, (uint32_t)g);
    }
    if (comp_match(c, CL_TOK_LPAREN)) {
      int argc = 0;
      if (!comp_check(c, CL_TOK_RPAREN)) {
        do { compile_expr(c); argc++; } while (comp_match(c, CL_TOK_COMMA));
      }
      comp_match(c, CL_TOK_RPAREN);
      emit(vm, CL_OP_CALL);
      emit_u32(vm, (uint32_t)argc);
    }
  } else if (comp_match(c, CL_TOK_LPAREN)) {
    compile_expr(c);
    comp_match(c, CL_TOK_RPAREN);
  } else if (comp_match(c, CL_TOK_MINUS)) {
    compile_primary(c);
    emit(vm, CL_OP_NEG);
  } else if (comp_match(c, CL_TOK_NOT)) {
    compile_primary(c);
    emit(vm, CL_OP_NOT);
  } else {
    comp_advance(c);
  }
}

static void compile_binary(struct cl_compiler *c, int min_prec) {
  compile_primary(c);
  for (;;) {
    int prec = 0; uint8_t op = CL_OP_NOP;
    switch (c->current.type) {
      case CL_TOK_OR:      prec = 1; op = CL_OP_OR; break;
      case CL_TOK_AND:     prec = 2; op = CL_OP_AND; break;
      case CL_TOK_EQ:      prec = 3; op = CL_OP_EQ; break;
      case CL_TOK_NEQ:     prec = 3; op = CL_OP_NEQ; break;
      case CL_TOK_LT:      prec = 4; op = CL_OP_LT; break;
      case CL_TOK_GT:      prec = 4; op = CL_OP_GT; break;
      case CL_TOK_LEQ:     prec = 4; op = CL_OP_LEQ; break;
      case CL_TOK_GEQ:     prec = 4; op = CL_OP_GEQ; break;
      case CL_TOK_PLUS:    prec = 5; op = CL_OP_ADD; break;
      case CL_TOK_MINUS:   prec = 5; op = CL_OP_SUB; break;
      case CL_TOK_STAR:    prec = 6; op = CL_OP_MUL; break;
      case CL_TOK_SLASH:   prec = 6; op = CL_OP_DIV; break;
      case CL_TOK_PERCENT: prec = 6; op = CL_OP_MOD; break;
      default: prec = 0; break;
    }
    if (prec <= min_prec) break;
    comp_advance(c);
    compile_binary(c, prec);
    emit(c->vm, op);
  }
}

static void compile_expr(struct cl_compiler *c) { compile_binary(c, 0); }

static void compile_stmt(struct cl_compiler *c) {
  struct cl_vm *vm = c->vm;

  if (comp_match(c, CL_TOK_LET)) {
    comp_match(c, CL_TOK_IDENT);
    if (c->local_count < CL_LOCALS_MAX) {
      cl_strcpy(c->locals[c->local_count], c->previous.text, CL_TOKEN_MAX);
      uint32_t idx = c->local_count++;
      if (comp_match(c, CL_TOK_ASSIGN)) compile_expr(c);
      else emit(vm, CL_OP_PUSH_NIL);
      emit(vm, CL_OP_STORE_LOCAL); emit_u32(vm, idx);
    }
  } else if (comp_match(c, CL_TOK_PRINT)) {
    compile_expr(c);
    emit(vm, CL_OP_PRINT);
  } else if (comp_match(c, CL_TOK_IF)) {
    compile_expr(c);
    emit(vm, CL_OP_JUMP_IF_FALSE);
    uint32_t patch = vm->bytecode_len; emit_u32(vm, 0);
    comp_match(c, CL_TOK_LBRACE);
    while (!comp_check(c, CL_TOK_RBRACE) && !comp_check(c, CL_TOK_EOF))
      compile_stmt(c);
    comp_match(c, CL_TOK_RBRACE);
    if (comp_match(c, CL_TOK_ELSE)) {
      emit(vm, CL_OP_JUMP);
      uint32_t else_patch = vm->bytecode_len; emit_u32(vm, 0);
      uint32_t here = vm->bytecode_len;
      vm->bytecode[patch] = (uint8_t)(here & 0xFF);
      vm->bytecode[patch+1] = (uint8_t)((here>>8)&0xFF);
      vm->bytecode[patch+2] = (uint8_t)((here>>16)&0xFF);
      vm->bytecode[patch+3] = (uint8_t)((here>>24)&0xFF);
      comp_match(c, CL_TOK_LBRACE);
      while (!comp_check(c, CL_TOK_RBRACE) && !comp_check(c, CL_TOK_EOF))
        compile_stmt(c);
      comp_match(c, CL_TOK_RBRACE);
      here = vm->bytecode_len;
      vm->bytecode[else_patch] = (uint8_t)(here & 0xFF);
      vm->bytecode[else_patch+1] = (uint8_t)((here>>8)&0xFF);
      vm->bytecode[else_patch+2] = (uint8_t)((here>>16)&0xFF);
      vm->bytecode[else_patch+3] = (uint8_t)((here>>24)&0xFF);
    } else {
      uint32_t here = vm->bytecode_len;
      vm->bytecode[patch] = (uint8_t)(here & 0xFF);
      vm->bytecode[patch+1] = (uint8_t)((here>>8)&0xFF);
      vm->bytecode[patch+2] = (uint8_t)((here>>16)&0xFF);
      vm->bytecode[patch+3] = (uint8_t)((here>>24)&0xFF);
    }
  } else if (comp_match(c, CL_TOK_WHILE)) {
    uint32_t loop_start = vm->bytecode_len;
    compile_expr(c);
    emit(vm, CL_OP_JUMP_IF_FALSE);
    uint32_t patch = vm->bytecode_len; emit_u32(vm, 0);
    comp_match(c, CL_TOK_LBRACE);
    while (!comp_check(c, CL_TOK_RBRACE) && !comp_check(c, CL_TOK_EOF))
      compile_stmt(c);
    comp_match(c, CL_TOK_RBRACE);
    emit(vm, CL_OP_JUMP); emit_u32(vm, loop_start);
    uint32_t here = vm->bytecode_len;
    vm->bytecode[patch] = (uint8_t)(here&0xFF);
    vm->bytecode[patch+1] = (uint8_t)((here>>8)&0xFF);
    vm->bytecode[patch+2] = (uint8_t)((here>>16)&0xFF);
    vm->bytecode[patch+3] = (uint8_t)((here>>24)&0xFF);
  } else if (comp_match(c, CL_TOK_RETURN)) {
    if (!comp_check(c, CL_TOK_NEWLINE) && !comp_check(c, CL_TOK_RBRACE) &&
        !comp_check(c, CL_TOK_EOF))
      compile_expr(c);
    else emit(vm, CL_OP_PUSH_NIL);
    emit(vm, CL_OP_RETURN);
  } else if (comp_check(c, CL_TOK_IDENT)) {
    char name[CL_TOKEN_MAX];
    cl_strcpy(name, c->current.text, CL_TOKEN_MAX);
    comp_advance(c);
    if (comp_match(c, CL_TOK_ASSIGN)) {
      compile_expr(c);
      int local = resolve_local(c, name);
      if (local >= 0) { emit(vm, CL_OP_STORE_LOCAL); emit_u32(vm, (uint32_t)local); }
      else {
        int g = find_global(vm, name);
        if (g < 0) { g = (int)vm->global_count; cl_strcpy(vm->global_names[vm->global_count], name, CL_TOKEN_MAX); vm->globals[vm->global_count] = cl_value_nil(); vm->global_count++; }
        emit(vm, CL_OP_STORE_GLOBAL); emit_u32(vm, (uint32_t)g);
      }
    } else {
      int local = resolve_local(c, name);
      if (local >= 0) { emit(vm, CL_OP_LOAD_LOCAL); emit_u32(vm, (uint32_t)local); }
      else {
        int g = find_global(vm, name);
        if (g < 0) { g = (int)vm->global_count; cl_strcpy(vm->global_names[vm->global_count], name, CL_TOKEN_MAX); vm->globals[vm->global_count] = cl_value_nil(); vm->global_count++; }
        emit(vm, CL_OP_LOAD_GLOBAL); emit_u32(vm, (uint32_t)g);
      }
      if (comp_match(c, CL_TOK_LPAREN)) {
        int argc = 0;
        if (!comp_check(c, CL_TOK_RPAREN)) { do { compile_expr(c); argc++; } while (comp_match(c, CL_TOK_COMMA)); }
        comp_match(c, CL_TOK_RPAREN);
        emit(vm, CL_OP_CALL); emit_u32(vm, (uint32_t)argc);
      }
      emit(vm, CL_OP_POP);
    }
  } else {
    compile_expr(c);
    emit(vm, CL_OP_POP);
  }
}

int cl_compile(struct cl_vm *vm, const char *source) {
  if (!vm || !source) return -1;
  struct cl_compiler comp;
  cl_memset(&comp, 0, sizeof(comp));
  comp.vm = vm;
  comp.lex.src = source;
  comp.lex.pos = 0;
  comp.lex.line = 1;
  comp.lex.col = 1;
  comp.local_count = 0;
  comp.scope_depth = 0;
  vm->bytecode_len = 0;
  vm->ip = 0;

  comp_advance(&comp);
  while (!comp_check(&comp, CL_TOK_EOF)) compile_stmt(&comp);
  emit(vm, CL_OP_HALT);
  return vm->error ? -1 : 0;
}

/* ---- VM execution ---- */

int cl_run(struct cl_vm *vm) {
  if (!vm) return -1;
  vm->running = 1;
  vm->error = 0;
  vm->ip = 0;

  while (vm->running && vm->ip < vm->bytecode_len) {
    uint8_t op = vm->bytecode[vm->ip++];
    switch (op) {
    case CL_OP_NOP: break;
    case CL_OP_HALT: vm->running = 0; break;
    case CL_OP_PUSH: { int64_t v = read_i64(vm); push(vm, cl_value_int(v)); break; }
    case CL_OP_PUSH_STRING: { uint32_t off = read_u32(vm); struct cl_value sv; sv.type = CL_VAL_STRING; sv.s = vm->string_pool + off; push(vm, sv); break; }
    case CL_OP_PUSH_TRUE: push(vm, cl_value_bool(1)); break;
    case CL_OP_PUSH_FALSE: push(vm, cl_value_bool(0)); break;
    case CL_OP_PUSH_NIL: push(vm, cl_value_nil()); break;
    case CL_OP_POP: pop(vm); break;
    case CL_OP_DUP: { struct cl_value v = vm->stack[vm->sp]; push(vm, v); break; }
    case CL_OP_ADD: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_int(a.i + b.i)); break; }
    case CL_OP_SUB: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_int(a.i - b.i)); break; }
    case CL_OP_MUL: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_int(a.i * b.i)); break; }
    case CL_OP_DIV: { struct cl_value b = pop(vm), a = pop(vm); if (b.i == 0) { vm_error(vm, "division by zero"); break; } push(vm, cl_value_int(a.i / b.i)); break; }
    case CL_OP_MOD: { struct cl_value b = pop(vm), a = pop(vm); if (b.i == 0) { vm_error(vm, "modulo by zero"); break; } push(vm, cl_value_int(a.i % b.i)); break; }
    case CL_OP_NEG: { struct cl_value a = pop(vm); push(vm, cl_value_int(-a.i)); break; }
    case CL_OP_EQ:  { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.i == b.i)); break; }
    case CL_OP_NEQ: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.i != b.i)); break; }
    case CL_OP_LT:  { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.i < b.i)); break; }
    case CL_OP_GT:  { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.i > b.i)); break; }
    case CL_OP_LEQ: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.i <= b.i)); break; }
    case CL_OP_GEQ: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.i >= b.i)); break; }
    case CL_OP_AND: { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.b && b.b)); break; }
    case CL_OP_OR:  { struct cl_value b = pop(vm), a = pop(vm); push(vm, cl_value_bool(a.b || b.b)); break; }
    case CL_OP_NOT: { struct cl_value a = pop(vm); push(vm, cl_value_bool(!a.b && a.i == 0)); break; }
    case CL_OP_LOAD_LOCAL:  { uint32_t idx = read_u32(vm); push(vm, vm->stack[vm->bp + (int32_t)idx]); break; }
    case CL_OP_STORE_LOCAL: { uint32_t idx = read_u32(vm); struct cl_value v = pop(vm); vm->stack[vm->bp + (int32_t)idx] = v; break; }
    case CL_OP_LOAD_GLOBAL:  { uint32_t idx = read_u32(vm); if (idx < vm->global_count) push(vm, vm->globals[idx]); else push(vm, cl_value_nil()); break; }
    case CL_OP_STORE_GLOBAL: { uint32_t idx = read_u32(vm); struct cl_value v = pop(vm); if (idx < CL_GLOBALS_MAX) vm->globals[idx] = v; break; }
    case CL_OP_JUMP: { uint32_t addr = read_u32(vm); vm->ip = addr; break; }
    case CL_OP_JUMP_IF_FALSE: { uint32_t addr = read_u32(vm); struct cl_value v = pop(vm); if ((v.type == CL_VAL_BOOL && !v.b) || (v.type == CL_VAL_INT && v.i == 0) || v.type == CL_VAL_NIL) vm->ip = addr; break; }
    case CL_OP_CALL: {
      uint32_t argc = read_u32(vm);
      struct cl_value fn_val = vm->stack[vm->sp - (int32_t)argc];
      if (fn_val.type == CL_VAL_NATIVE_FN && fn_val.obj) {
        cl_native_fn native = (cl_native_fn)fn_val.obj;
        struct cl_value args[16];
        for (int i = (int)argc - 1; i >= 0; i--) args[i] = pop(vm);
        pop(vm);
        struct cl_value result = native(args, (int)argc);
        push(vm, result);
      } else {
        vm_error(vm, "not callable");
      }
      break;
    }
    case CL_OP_RETURN: {
      struct cl_value retval = pop(vm);
      if (vm->csp < 0) { push(vm, retval); vm->running = 0; break; }
      vm->ip = vm->callstack[vm->csp].ip;
      vm->bp = (int32_t)vm->callstack[vm->csp].bp;
      vm->csp--;
      push(vm, retval);
      break;
    }
    case CL_OP_PRINT: {
      struct cl_value v = pop(vm);
      /* In kernel context, print to debugcon */
      if (v.type == CL_VAL_INT) {
        char buf[24]; int p = 0; int64_t val = v.i;
        if (val < 0) { buf[p++] = '-'; val = -val; }
        if (val == 0) buf[p++] = '0';
        else { char tmp[20]; int tp = 0; while (val > 0) { tmp[tp++] = '0' + (val % 10); val /= 10; } for (int i = tp-1; i >= 0; i--) buf[p++] = tmp[i]; }
        buf[p++] = '\n'; buf[p] = 0;
        for (int i = 0; buf[i]; i++) __asm__ volatile("outb %0, %1" : : "a"((uint8_t)buf[i]), "Nd"((uint16_t)0xE9));
      } else if (v.type == CL_VAL_STRING && v.s) {
        for (int i = 0; v.s[i]; i++) __asm__ volatile("outb %0, %1" : : "a"((uint8_t)v.s[i]), "Nd"((uint16_t)0xE9));
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)'\n'), "Nd"((uint16_t)0xE9));
      } else if (v.type == CL_VAL_BOOL) {
        const char *s = v.b ? "true\n" : "false\n";
        for (int i = 0; s[i]; i++) __asm__ volatile("outb %0, %1" : : "a"((uint8_t)s[i]), "Nd"((uint16_t)0xE9));
      } else {
        const char *s = "nil\n";
        for (int i = 0; s[i]; i++) __asm__ volatile("outb %0, %1" : : "a"((uint8_t)s[i]), "Nd"((uint16_t)0xE9));
      }
      break;
    }
    default: vm_error(vm, "unknown opcode"); break;
    }
    if (vm->error) break;
  }
  return vm->error ? -1 : 0;
}

int cl_run_string(struct cl_vm *vm, const char *source) {
  if (cl_compile(vm, source) != 0) return -1;
  return cl_run(vm);
}

int cl_run_file(struct cl_vm *vm, const char *path,
                int (*read_fn)(const char *, char *, size_t, size_t *)) {
  if (!vm || !path || !read_fn) return -1;
  char buf[4096];
  size_t len = 0;
  if (read_fn(path, buf, sizeof(buf) - 1, &len) != 0) return -1;
  buf[len] = '\0';
  return cl_run_string(vm, buf);
}
