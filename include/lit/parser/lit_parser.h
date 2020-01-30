#ifndef LIT_PARSER_H
#define LIT_PARSER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/parser/lit_ast.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/lit.h>

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_OR,          // ||
	PREC_AND,         // &&
	PREC_EQUALITY,    // == !=
	PREC_COMPARISON,  // < > <= >=
	PREC_COMPOUND,    // += -= *= /= ++ --
	PREC_TERM,        // + -
	PREC_FACTOR,      // * /
	PREC_UNARY,       // ! -
	PREC_NULL,        // ??
	PREC_CALL,        // . ()
	PREC_PRIMARY
} LitPrecedence;

typedef LitExpression* (*LitPrefixParseFn)(LitParser*, bool);
typedef LitExpression* (*LitInfixParseFn)(LitParser*, LitExpression*, bool);

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

	LitCompiler* compiler;
} sLitParser;

void lit_init_parser(LitState* state, LitParser* parser);
void lit_free_parser(LitParser* parser);

bool lit_parse(LitParser* parser, const char* source, LitStatements* statements);

#endif