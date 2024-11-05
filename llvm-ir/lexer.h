#pragma once


#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>

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

struct source_location_t {
  int line;
  int col;
};

struct lexer_t {
  int advance() {
    int last_char = code_input[index++];
    if (last_char == '\n' || last_char == '\r') {
      lex_location.line++;
      lex_location.col = 0;
    }
    else if (last_char == '\t') { // Assuming a tab width of 2 columns 
      lex_location.col += tab_size - (lex_location.col % tab_size);
    }
    else {
      lex_location.col++;
    }

    if (index >= code_input.size()) {
      index = 0;
      code_input.clear();
    }
    return last_char;
  }

  std::string parser_errors;

  int gettok() {

    // Skip any whitespace.
    while (isspace(last_char))
      last_char = advance();

    cursor_location = lex_location;

    // Check for multi-line comment start
    if (last_char == '\'') {
      last_char = advance();
      if (last_char == '\'') {
        last_char = advance();
        if (last_char == '\'') {
          if (handle_multiline_comment() == false) {
            parser_errors += "Unterminated multiline comment at " + std::to_string(lex_location.line) + ":" + std::to_string(lex_location.col) + "\n";
          }
          return gettok();
        }
      }
    }

    // Skip any whitespace.
    while (isspace(last_char))
      last_char = advance();


    // Identifier: [a-zA-Z_][a-zA-Z0-9_]*
    if (isalpha(last_char) || last_char == '_') {
      identifier_string = last_char;
      while (isalnum(last_char = advance()) || last_char == '_')
        identifier_string += last_char;

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
    if (isdigit(last_char) || last_char == '.') {
      std::string num_str;
      do {
        num_str += last_char;
        last_char = advance();
      } while (isdigit(last_char) || last_char == '.');

      double_value = strtod(num_str.c_str(), nullptr);
      return tok_number;
    }

    // String literal: "..."
    if (last_char == '"') {
      std::string str;
      while ((last_char = advance()) != '"' && last_char != EOF)
        str += last_char;

      if (last_char == '"')
        last_char = advance();

      string_value = str;
      return tok_literal_string;
    }

    // Comment until end of line.
    if (last_char == '#') {
      do
        last_char = advance();
      while (last_char != EOF && last_char != '\n' && last_char != '\r');

      if (last_char != EOF)
        return gettok();
    }

    // Check for end of file. Don't eat the EOF.
    if (last_char == EOF)
      return tok_eof;

    int this_char = last_char;
    last_char = advance();
    return this_char;
  }
  bool handle_multiline_comment() {
    while (true) {
      int ch = advance();
      if (ch == EOF) return false; // this should never come

      if (ch == '\'') {
        ch = advance();
        if (ch == '\'') {
          ch = advance();
          if (ch == '\'') {
            return true;
          }
          else {
            return false;
          }
        }
        else {
          return false;
        }
      }
    }
  }
  source_location_t cursor_location;
  source_location_t lex_location = { 1, 0 };
  std::string code_input = "";
  std::string identifier_string;
  std::string string_value;
  double double_value;
  std::size_t index = 0;
  int last_char = ' ';
  int tab_size = 4;
};