#include <lit/parser/lit_parser.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/lit_predefines.h>

#include <stdlib.h>

static LitExpression* parse_expression(LitParser* parser);
static LitStatement* parse_statement(LitParser* parser);
static LitParseRule rules[TOKEN_EOF + 1];
static bool did_setup_rules;
static void setup_rules();

static LitParseRule* get_rule(LitTokenType type) {
	return &rules[type];
}

static inline bool is_at_end(LitParser* parser) {
	return parser->current.type == TOKEN_EOF;
}

void lit_init_parser(LitState* state, LitParser* parser) {
	if (!did_setup_rules) {
		did_setup_rules = true;
		setup_rules();
	}

	parser->state = state;
	parser->had_error = false;
	parser->panic_mode = false;
}

void lit_free_parser(LitParser* parser) {

}

static void error_at(LitParser* parser, LitToken* token, const char* message) {
	if (parser->panic_mode) {
		return;
	}

	lit_error(parser->state, COMPILE_ERROR, token->line, message);
	parser->had_error = true;
}

static void error_at_current(LitParser* parser, const char* message) {
	error_at(parser, &parser->current, message);
}

static void error(LitParser* parser, const char* message) {
	error_at(parser, &parser->previous, message);
}

static void advance(LitParser* parser) {
	parser->previous = parser->current;

	while (true) {
		parser->current = lit_scan_token(parser->state->scanner);

		if (parser->current.type != TOKEN_ERROR) {
			break;
		}

		error_at_current(parser, parser->current.start);
	}
}


static bool match(LitParser* parser, LitTokenType type) {
	if (parser->current.type == type) {
		advance(parser);
		return true;
	}

	return false;
}

static void consume(LitParser* parser, LitTokenType type, const char* message) {
	if (parser->current.type == type) {
		advance(parser);
		return;
	}

	error_at_current(parser, message);
}

static LitExpression* parse_precedence(LitParser* parser, LitPrecedence precedence) {
	advance(parser);
	LitPrefixParseFn prefix_rule = get_rule(parser->previous.type)->prefix;

	if (prefix_rule == NULL) {
		error(parser, "Expected expression");
		return NULL;
	}

	LitExpression* expr = prefix_rule(parser);

	while (precedence <= get_rule(parser->current.type)->precedence) {
		advance(parser);
		LitInfixParseFn infix_rule = get_rule(parser->previous.type)->infix;
		expr = infix_rule(parser, expr);
	}

	return expr;
}

static LitExpression* parse_number(LitParser* parser) {
	return (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, NUMBER_VAL(strtod(parser->previous.start, NULL)));
}

static LitExpression* parse_grouping(LitParser* parser) {
	LitExpression* expression = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression");

	return (LitExpression*) lit_create_grouping_expression(parser->state, parser->previous.line, expression);
}

static LitExpression* parse_unary(LitParser* parser) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;
	LitExpression* expression = parse_precedence(parser, PREC_UNARY);

	return (LitExpression*) lit_create_unary_expression(parser->state, line, expression, operator);
}

static LitExpression* parse_binary(LitParser* parser, LitExpression* prev) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	LitParseRule* rule = get_rule(operator);
	LitExpression* expression = parse_precedence(parser, (LitPrecedence) (rule->precedence + 1));

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, expression, operator);
}

static LitExpression* parse_literal(LitParser* parser) {
	uint line = parser->previous.line;

	switch (parser->previous.type) {
		case TOKEN_TRUE: {
			return (LitExpression*) lit_create_literal_expression(parser->state, line, TRUE_VAL);
		}

		case TOKEN_FALSE: {
			return (LitExpression*) lit_create_literal_expression(parser->state, line, FALSE_VAL);
		}

		case TOKEN_NULL: {
			return (LitExpression*) lit_create_literal_expression(parser->state, line, NULL_VAL);
		}

		default: UNREACHABLE
	}
}

static LitExpression* parse_expression(LitParser* parser) {
	return parse_precedence(parser, PREC_ASSIGNMENT);

	/*
	if (match(parser, TOKEN_NUMBER)) {
		return parse_number(parser);
	}

	if (match(parser, TOKEN_LEFT_PAREN)) {
		return parse_grouping(parser);
	}

	if (match(parser, TOKEN_MINUS)) {
		return parse_unary(parser);
	}

	error_at_current(parser, "Unexpected token");
	return NULL;*/
}

static LitStatement* parse_statement(LitParser* parser) {
	LitExpression* expression = parse_expression(parser);
	return expression == NULL ? NULL : (LitStatement*) lit_create_expression_statement(parser->state, parser->previous.line, expression);
}

bool lit_parse(LitParser* parser, const char* source, LitStatements* statements) {
	parser->had_error = false;
	parser->panic_mode = false;

	lit_setup_scanner(parser->state->scanner, source);
	advance(parser);

	if (is_at_end(parser)) {
		error_at_current(parser, "Expected statement but got nothing");
	} else {
		do {
			LitStatement* statement = parse_statement(parser);

			if (statement != NULL) {
				lit_stataments_write(parser->state, statements, statement);
			}
		} while (!is_at_end(parser));

		consume(parser, TOKEN_EOF, "Expected end of expression");
	}

	return parser->had_error;
}

static void setup_rules() {
	rules[TOKEN_LEFT_PAREN] = (LitParseRule) { parse_grouping, NULL, PREC_NONE };
	rules[TOKEN_PLUS] = (LitParseRule) { NULL, parse_binary, PREC_TERM };
	rules[TOKEN_MINUS] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_SLASH] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_NUMBER] = (LitParseRule) { parse_number, NULL, PREC_NONE };

	rules[TOKEN_TRUE] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	rules[TOKEN_FALSE] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	rules[TOKEN_NULL] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
}