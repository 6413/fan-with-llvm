#pragma once


#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"

extern std::unique_ptr<llvm::IRBuilder<>> ir_builder;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,

  // control
  tok_if = -6,
  tok_then = -7,
  tok_else = -8,
  tok_for = -9,
  tok_in = -10,

  // operators
  tok_binary = -11,
  tok_unary = -12,

  // var definition
  tok_var = -13,
  tok_string = -14,
  tok_type_string = -15,
  tok_type_double = -16,
};

struct debug_info_t {
  llvm::DICompileUnit* TheCU;
  llvm::DIType* DblTy;
  std::vector<llvm::DIScope*> LexicalBlocks;

  void emitLocation(auto AST) {
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

struct SourceLocation {
  int Line;
  int Col;
};
inline SourceLocation CurLoc;
inline SourceLocation LexLoc = { 1, 0 };

inline std::string code_input = "";

inline int index = 0;

static int advance() {
  int LastChar = code_input.operator[](index);
  ++index;
  if (index >= code_input.size()) {
    index = 0;
    code_input.clear();
  }

  if (LastChar == '\n' || LastChar == '\r') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  }
  else
    LexLoc.Col++;
  return LastChar;
}

inline static std::string IdentifierStr;
inline static double NumVal;
inline static std::string StringVal;

static int gLastChar = ' ';
/// gettok - Return the next token from standard input.
static int gettok() {
  // Skip any whitespace.
  while (isspace(gLastChar))
    gLastChar = advance();

  CurLoc = LexLoc;

  // Identifier: [a-zA-Z_][a-zA-Z0-9_]*
  if (isalpha(gLastChar) || gLastChar == '_') {
    IdentifierStr = gLastChar;
    while (isalnum(gLastChar = advance()) || gLastChar == '_')
      IdentifierStr += gLastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    if (IdentifierStr == "if")
      return tok_if;
    if (IdentifierStr == "then")
      return tok_then;
    if (IdentifierStr == "else")
      return tok_else;
    if (IdentifierStr == "for")
      return tok_for;
    if (IdentifierStr == "in")
      return tok_in;
    if (IdentifierStr == "binary")
      return tok_binary;
    if (IdentifierStr == "unary")
      return tok_unary;
    if (IdentifierStr == "var")
      return tok_var;
    if (IdentifierStr == "string")
      return tok_type_string;
    if (IdentifierStr == "double")
      return tok_type_double;
    return tok_identifier;
  }

  // Number: [0-9.]+
  if (isdigit(gLastChar) || gLastChar == '.') {
    std::string NumStr;
    do {
      NumStr += gLastChar;
      gLastChar = advance();
    } while (isdigit(gLastChar) || gLastChar == '.');

    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  // String literal: "..."
  if (gLastChar == '"') {
    std::string Str;
    while ((gLastChar = advance()) != '"' && gLastChar != EOF)
      Str += gLastChar;

    if (gLastChar == '"')
      gLastChar = advance();

    StringVal = Str;
    return tok_string;
  }

  // Comment until end of line.
  if (gLastChar == '#') {
    do
      gLastChar = advance();
    while (gLastChar != EOF && gLastChar != '\n' && gLastChar != '\r');

    if (gLastChar != EOF)
      return gettok();
  }

  // Check for end of file. Don't eat the EOF.
  if (gLastChar == EOF)
    return tok_eof;

  int ThisChar = gLastChar;
  gLastChar = advance();
  return ThisChar;
}