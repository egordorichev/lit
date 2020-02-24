#ifndef LIT_PARSER_H
#define LIT_PARSER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/parser/lit_ast.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/lit_config.h>

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
	PREC_IS,          // is
	PREC_UNARY,       // ! -
	PREC_NULL,        // ??
	PREC_CALL,        // . ()
	PREC_RANGE,       // ..
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
	LitStatements* statements;

	LitExpression* expression_roots[LIT_ROOT_MAX];
	LitStatement* statement_roots[LIT_ROOT_MAX];
	uint8_t expression_root_count;
	uint8_t statement_root_count;
} sLitParser;

void lit_init_parser(LitState* state, LitParser* parser);
void lit_free_parser(LitParser* parser);

bool lit_parse(LitParser* parser, const char* file_name, const char* source, LitStatements* statements);

#endif