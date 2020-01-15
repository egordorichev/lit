#ifndef LIT_PARSER_H
#define LIT_PARSER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/parser/lit_ast.h>

typedef struct sLitParser {
	LitState* state;
	bool had_error;
} sLitParser;

void lit_init_parser(LitState* state, LitParser* parser);
void lit_free_parser(LitParser* parser);

bool lit_parse(LitParser* parser, LitStatements* statements);

#endif