#include <lit/parser/lit_parser.h>

void lit_init_parser(LitState* state, LitParser* parser) {
	parser->state = state;
	parser->had_error = false;
}

void lit_free_parser(LitParser* parser) {

}

bool lit_parse(LitParser* parser, LitStatements* statements) {
	parser->had_error = false;
	LitState* state = parser->state;

	lit_stataments_write(state, statements, (LitStatement*)
		lit_create_expression_statement(state, 1, (LitExpression*)
		lit_create_literal_expression(state, 1, 69)));

	return parser->had_error;
}