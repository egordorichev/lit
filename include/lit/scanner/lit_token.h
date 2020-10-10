#ifndef LIT_TOKEN_H
#define LIT_TOKEN_H

#include "lit_common.h"

typedef enum {
	TOKEN_NEW_LINE,

	// Single-character tokens.
	TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
	TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
	TOKEN_COMMA, TOKEN_SEMICOLON, TOKEN_COLON,

	// One or two character tokens.
	TOKEN_BAR_EQUAL, TOKEN_BAR, TOKEN_BAR_BAR,
	TOKEN_AMPERSAND_EQUAL, TOKEN_AMPERSAND, TOKEN_AMPERSAND_AMPERSAND,
	TOKEN_BANG, TOKEN_BANG_EQUAL,
	TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
	TOKEN_GREATER, TOKEN_GREATER_EQUAL, TOKEN_GREATER_GREATER,
	TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_LESS_LESS,
	TOKEN_PLUS, TOKEN_PLUS_EQUAL,
	TOKEN_PLUS_PLUS,
	TOKEN_MINUS, TOKEN_MINUS_EQUAL,
	TOKEN_MINUS_MINUS,
	TOKEN_STAR, TOKEN_STAR_EQUAL,
	TOKEN_STAR_STAR,
	TOKEN_SLASH, TOKEN_SLASH_EQUAL,
	TOKEN_QUESTION, TOKEN_QUESTION_QUESTION,
	TOKEN_PERCENT, TOKEN_PERCENT_EQUAL,
	TOKEN_ARROW, TOKEN_SMALL_ARROW, TOKEN_TILDE,
	TOKEN_CARET, TOKEN_CARET_EQUAL,
	TOKEN_DOT, TOKEN_DOT_DOT, TOKEN_DOT_DOT_DOT,
	TOKEN_SHARP, TOKEN_SHARP_EQUAL,

	// Literals.
	TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_INTERPOLATION, TOKEN_NUMBER,

	// Keywords.
	TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
	TOKEN_FOR, TOKEN_FUNCTION, TOKEN_IF, TOKEN_NULL,
	TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
	TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,
	TOKEN_CONTINUE, TOKEN_BREAK,
	TOKEN_NEW, TOKEN_EXPORT, TOKEN_IS,
	TOKEN_STATIC, TOKEN_OPERATOR,
	TOKEN_GET, TOKEN_SET, TOKEN_IN,
	TOKEN_CONST, TOKEN_REF,

	TOKEN_ERROR,
	TOKEN_EOF
} LitTokenType;

typedef struct {
	const char* start;

	LitTokenType type;
	uint length;
	uint line;

	LitValue value;
} LitToken;

#endif