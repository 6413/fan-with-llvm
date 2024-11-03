#pragma once


#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"

extern std::unique_ptr<llvm::IRBuilder<>> ir_builder;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//


enum token_e {
  // Token types
  tok_eof = 256,
  tok_definition,
  tok_extern,

  // Primary tokens
  tok_identifier,
  tok_number,

  // Control flow tokens
  tok_if,
  tok_then,
  tok_else,
  tok_for,
  tok_in,

  // Operator tokens
  tok_binary_operator,
  tok_unary_operator,

  // Variable definition tokens
  tok_variable,
  tok_literal_string,
  tok_type_string,
  tok_type_double,
};

struct debug_info_t {
  llvm::DICompileUnit* TheCU;
  llvm::DIType* DblTy;
  std::vector<llvm::DIScope*> LexicalBlocks;

  void emit_location(auto AST) {
    if constexpr (std::is_null_pointer_v<decltype(AST)>)
      return ir_builder->SetCurrentDebugLocation(llvm::DebugLoc());
    else {
      llvm::DIScope* Scope;
      if (LexicalBlocks.empty())
        Scope = TheCU;
      else
        Scope = LexicalBlocks.back();
      ir_builder->SetCurrentDebugLocation(llvm::DILocation::get(
        Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
    }
  }
  llvm::DIType* getDoubleTy();
};

struct source_location_t {
  int line;
  int col;
};
inline source_location_t CurLoc;
inline source_location_t LexLoc = { 1, 0 };

inline std::string code_input = "";

inline int index = 0;

static int advance() {
  int last_char = code_input.operator[](index);
  ++index;
  if (index >= code_input.size()) {
    index = 0;
    code_input.clear();
  }

  if (last_char == '\n' || last_char == '\r') {
    LexLoc.line++;
    LexLoc.col = 0;
  }
  else
    LexLoc.col++;
  return last_char;
}

inline static std::string identifier_string;
inline static double double_value;
inline static std::string string_value;

static int g_last_char = ' ';
/// gettok - Return the next token from standard input.
static int gettok() {
  // Skip any whitespace.
  while (isspace(g_last_char))
    g_last_char = advance();

  CurLoc = LexLoc;

  // Identifier: [a-zA-Z_][a-zA-Z0-9_]*
  if (isalpha(g_last_char) || g_last_char == '_') {
    identifier_string = g_last_char;
    while (isalnum(g_last_char = advance()) || g_last_char == '_')
      identifier_string += g_last_char;

    if (identifier_string == "def")
      return tok_definition;
    if (identifier_string == "extern")
      return tok_extern;
    if (identifier_string == "if")
      return tok_if;
    if (identifier_string == "then")
      return tok_then;
    if (identifier_string == "else")
      return tok_else;
    if (identifier_string == "for")
      return tok_for;
    if (identifier_string == "in")
      return tok_in;
    if (identifier_string == "binary")
      return tok_binary_operator;
    if (identifier_string == "unary")
      return tok_unary_operator;
    if (identifier_string == "var")
      return tok_variable;
    if (identifier_string == "string")
      return tok_type_string;
    if (identifier_string == "double")
      return tok_type_double;
    return tok_identifier;
  }

  // Number: [0-9.]+
  if (isdigit(g_last_char) || g_last_char == '.') {
    std::string num_str;
    do {
      num_str += g_last_char;
      g_last_char = advance();
    } while (isdigit(g_last_char) || g_last_char == '.');

    double_value = strtod(num_str.c_str(), nullptr);
    return tok_number;
  }

  // String literal: "..."
  if (g_last_char == '"') {
    std::string str;
    while ((g_last_char = advance()) != '"' && g_last_char != EOF)
      str += g_last_char;

    if (g_last_char == '"')
      g_last_char = advance();

    string_value = str;
    return tok_literal_string;
  }

  // Comment until end of line.
  if (g_last_char == '#') {
    do
      g_last_char = advance();
    while (g_last_char != EOF && g_last_char != '\n' && g_last_char != '\r');

    if (g_last_char != EOF)
      return gettok();
  }

  // Check for end of file. Don't eat the EOF.
  if (g_last_char == EOF)
    return tok_eof;

  int this_char = g_last_char;
  g_last_char = advance();
  return this_char;
}