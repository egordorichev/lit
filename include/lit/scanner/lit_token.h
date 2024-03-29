#ifndef LIT_TOKEN_H
#define LIT_TOKEN_H

#include "lit/lit_common.h"

typedef enum {
	LTOKEN_NEW_LINE,

	// Single-character tokens.
	LTOKEN_LEFT_PAREN, LTOKEN_RIGHT_PAREN,
	LTOKEN_LEFT_BRACE, LTOKEN_RIGHT_BRACE,
	LTOKEN_LEFT_BRACKET, LTOKEN_RIGHT_BRACKET,
	LTOKEN_COMMA, LTOKEN_SEMICOLON, LTOKEN_COLON,

	// One or two character tokens.
	LTOKEN_BAR_EQUAL, LTOKEN_BAR, LTOKEN_BAR_BAR,
	LTOKEN_AMPERSAND_EQUAL, LTOKEN_AMPERSAND, LTOKEN_AMPERSAND_AMPERSAND,
	LTOKEN_BANG, LTOKEN_BANG_EQUAL,
	LTOKEN_EQUAL, LTOKEN_EQUAL_EQUAL,
	LTOKEN_GREATER, LTOKEN_GREATER_EQUAL, LTOKEN_GREATER_GREATER,
	LTOKEN_LESS, LTOKEN_LESS_EQUAL, LTOKEN_LESS_LESS,
	LTOKEN_PLUS, LTOKEN_PLUS_EQUAL,
	LTOKEN_PLUS_PLUS,
	LTOKEN_MINUS, LTOKEN_MINUS_EQUAL,
	LTOKEN_MINUS_MINUS,
	LTOKEN_STAR, LTOKEN_STAR_EQUAL,
	LTOKEN_STAR_STAR,
	LTOKEN_SLASH, LTOKEN_SLASH_EQUAL,
	LTOKEN_QUESTION, LTOKEN_QUESTION_QUESTION,
	LTOKEN_PERCENT, LTOKEN_PERCENT_EQUAL,
	LTOKEN_ARROW, LTOKEN_SMALL_ARROW, LTOKEN_TILDE,
	LTOKEN_CARET, LTOKEN_CARET_EQUAL,
	LTOKEN_DOT, LTOKEN_DOT_DOT, LTOKEN_DOT_DOT_DOT,
	LTOKEN_SHARP, LTOKEN_SHARP_EQUAL,

	// Literals.
	LTOKEN_IDENTIFIER, LTOKEN_STRING, LTOKEN_INTERPOLATION, LTOKEN_NUMBER,

	// Keywords.
	LTOKEN_CLASS, LTOKEN_ELSE, LTOKEN_FALSE,
	LTOKEN_FOR, LTOKEN_FUNCTION, LTOKEN_IF, LTOKEN_NULL,
	LTOKEN_RETURN, LTOKEN_SUPER, LTOKEN_THIS,
	LTOKEN_TRUE, LTOKEN_VAR, LTOKEN_WHILE,
	LTOKEN_CONTINUE, LTOKEN_BREAK,
	LTOKEN_NEW, LTOKEN_EXPORT, LTOKEN_IS,
	LTOKEN_STATIC, LTOKEN_OPERATOR, LTOKEN_IN,
	LTOKEN_CONST, LTOKEN_REF,

	LTOKEN_ERROR,
	LTOKEN_EOF
} LitTokenType;

typedef struct {
	const char* start;

	LitTokenType type;
	uint length;
	uint line;

	LitValue value;
} LitToken;

#endif