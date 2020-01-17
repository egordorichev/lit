#ifndef LIT_PARSER_H
#define LIT_PARSER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/parser/lit_ast.h>

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_EQUALITY,    // == !=
	PREC_COMPARISON,  // < > <= >=
	PREC_TERM,        // + -
	PREC_FACTOR,      // * /
	PREC_UNARY,       // ! -
	PREC_CALL,        // . ()
	PREC_PRIMARY
} LitPrecedence;

typedef LitExpression* (*LitPrefixParseFn)(LitParser*);
typedef LitExpression* (*LitInfixParseFn)(LitParser*, LitExpression*);

typedef struct {
	LitPrefixParseFn prefix;
	LitInfixParseFn infix;
	LitPrecedence precedence;
} LitParseRule;

typedef struct sLitParser {
	LitState* state;
	bool had_error;
	bool panic_mode;

	LitToken previous;
	LitToken current;
} sLitParser;

void lit_init_parser(LitState* state, LitParser* parser);
void lit_free_parser(LitParser* parser);

bool lit_parse(LitParser* parser, const char* source, LitStatements* statements);

#endif