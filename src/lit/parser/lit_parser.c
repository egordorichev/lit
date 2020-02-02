#include <lit/parser/lit_parser.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/vm/lit_object.h>
#include <lit/lit_predefines.h>

#include <stdlib.h>

static void init_compiler(LitParser* parser, LitCompiler* compiler) {
	compiler->local_count = 0;
	compiler->scope_depth = 0;
	compiler->function = NULL;
	compiler->enclosing = parser->compiler;

	parser->compiler = compiler;
}

static void end_compiler(LitParser* parser, LitCompiler* compiler) {
	parser->compiler = compiler->enclosing;
}

static void begin_scope(LitParser* parser) {
	parser->compiler->scope_depth++;
}

static void end_scope(LitParser* parser) {
	parser->compiler->scope_depth--;
}

static LitExpression* parse_expression(LitParser* parser);
static LitStatement* parse_statement(LitParser* parser);
static LitStatement* parse_declaration(LitParser* parser);

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

static bool check(LitParser* parser, LitTokenType type) {
	return parser->current.type == type;
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

static bool match_new_line(LitParser* parser) {
	if (!match(parser, TOKEN_NEW_LINE)) {
		return false;
	}

	while (match(parser, TOKEN_NEW_LINE)) {

	}

	return true;
}

static void ignore_new_lines(LitParser* parser) {
	match_new_line(parser);
}

static void consume_new_line(LitParser* parser, const char* error) {
	consume(parser, TOKEN_NEW_LINE, error);
	ignore_new_lines(parser);
}

static LitStatement* parse_block(LitParser* parser) {
	begin_scope(parser);
	LitBlockStatement* statement = lit_create_block_statement(parser->state, parser->previous.line);
	ignore_new_lines(parser);

	while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
		lit_stataments_write(parser->state, &statement->statements, parse_statement(parser));
		ignore_new_lines(parser);
	}

	consume(parser, TOKEN_RIGHT_BRACE, "'}' expected");
	end_scope(parser);

	return (LitStatement*) statement;
}

static LitExpression* parse_precedence(LitParser* parser, LitPrecedence precedence) {
	advance(parser);
	LitPrefixParseFn prefix_rule = get_rule(parser->previous.type)->prefix;

	if (prefix_rule == NULL) {
		error(parser, "Expected expression");
		return NULL;
	}

	bool can_assign = precedence <= PREC_ASSIGNMENT;
	LitExpression* expr = prefix_rule(parser, can_assign);

	while (precedence <= get_rule(parser->current.type)->precedence) {
		advance(parser);
		LitInfixParseFn infix_rule = get_rule(parser->previous.type)->infix;
		expr = infix_rule(parser, expr, can_assign);
	}

	if (can_assign && match(parser, TOKEN_EQUAL)) {
		error(parser, "Invalid assigment target");
	}

	return expr;
}

static LitExpression* parse_number(LitParser* parser, bool can_assign) {
	return (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, NUMBER_VAL(strtod(parser->previous.start, NULL)));
}

static LitExpression* parse_grouping(LitParser* parser, bool can_assign) {
	LitExpression* expression = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression");

	return (LitExpression*) lit_create_grouping_expression(parser->state, parser->previous.line, expression);
}

static LitExpression* parse_call(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitCallExpression* expression = lit_create_call_expression(parser->state, parser->previous.line, prev);

	while (!check(parser, TOKEN_RIGHT_PAREN)) {
		lit_expressions_write(parser->state, &expression->args, parse_expression(parser));

		if (!match(parser, TOKEN_COMMA)) {
			break;
		}
	}

	if (expression->args.count > 255) {
		error(parser, "Function can't be invoked with more than 255 arguments");
	}

	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after arguments");
	return (LitExpression*) expression;
}

static LitExpression* parse_unary(LitParser* parser, bool can_assign) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;
	LitExpression* expression = parse_precedence(parser, PREC_UNARY);

	return (LitExpression*) lit_create_unary_expression(parser->state, line, expression, operator);
}

static LitExpression* parse_binary(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	LitParseRule* rule = get_rule(operator);
	LitExpression* expression = parse_precedence(parser, (LitPrecedence) (rule->precedence + 1));

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, expression, operator);
}

static LitExpression* parse_and(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, parse_precedence(parser, PREC_AND), operator);
}

static LitExpression* parse_or(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, parse_precedence(parser, PREC_OR), operator);
}

static LitExpression* parse_null_filter(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, parse_precedence(parser, PREC_NULL), operator);
}

static LitTokenType convert_compound_operator(LitTokenType operator) {
	switch (operator) {
		case TOKEN_PLUS_EQUAL: return TOKEN_PLUS;
		case TOKEN_MINUS_EQUAL: return TOKEN_MINUS;
		case TOKEN_STAR_EQUAL: return TOKEN_STAR;
		case TOKEN_SLASH_EQUAL: return TOKEN_SLASH;
		case TOKEN_PERCENT_EQUAL: return TOKEN_PERCENT;

		case TOKEN_PLUS_PLUS: return TOKEN_PLUS;
		case TOKEN_MINUS_MINUS: return TOKEN_MINUS;

		default: {
			UNREACHABLE
		}
	}
}

static LitExpression* parse_compound(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	LitParseRule* rule = get_rule(operator);
	LitExpression* expression;

	if (operator == TOKEN_PLUS_PLUS || operator == TOKEN_MINUS_MINUS) {
		expression = (LitExpression*) lit_create_literal_expression(parser->state, line, NUMBER_VAL(1));
	} else {
		expression = parse_precedence(parser, (LitPrecedence) (rule->precedence + 1));
	}

	LitBinaryExpression* binary = lit_create_binary_expression(parser->state, line, prev, expression, convert_compound_operator(operator));
	binary->ignore_left = true; // To make sure we don't free it twice

	return (LitExpression*) lit_create_assign_expression(parser->state, line, prev, (LitExpression*) binary);
}

static LitExpression* parse_literal(LitParser* parser, bool can_assign) {
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

		case TOKEN_STRING: {
			return (LitExpression*) lit_create_literal_expression(parser->state, line, OBJECT_VAL(lit_copy_string(parser->state, parser->previous.start + 1, parser->previous.length - 2)));
		}

		default: UNREACHABLE
	}
}

static LitExpression* parse_variable_expression(LitParser* parser, bool can_assign) {
	LitExpression* expression = (LitExpression*) lit_create_local_var_expression(parser->state, parser->previous.line, parser->previous);

	if (can_assign && match(parser, TOKEN_EQUAL)) {
		return (LitExpression*) lit_create_assign_expression(parser->state, parser->previous.line, expression, parse_expression(parser));
	}

	return expression;
}

static LitExpression* parse_expression(LitParser* parser) {
	ignore_new_lines(parser);
	return parse_precedence(parser, PREC_ASSIGNMENT);
}

static LitStatement* parse_var_declaration(LitParser* parser) {
	uint line = parser->previous.line;
	consume(parser, TOKEN_IDENTIFIER, "Expected variable name");

	const char* name = parser->previous.start;
	uint length = parser->previous.length;

	LitExpression* init = NULL;

	if (match(parser, TOKEN_EQUAL)) {
		init = parse_expression(parser);
	}

	return (LitStatement*) lit_create_var_statement(parser->state, line, name, length, init);
}

static LitStatement* parse_if(LitParser* parser) {
	uint line = parser->previous.line;
	bool invert = match(parser, TOKEN_BANG);

	consume(parser, TOKEN_LEFT_PAREN, "Expected '('");
	LitExpression* condition = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')'");

	if (invert) {
		condition = (LitExpression*) lit_create_unary_expression(parser->state, condition->line, condition, TOKEN_BANG);
	}

	LitStatement* if_branch = parse_statement(parser);

	LitExpressions* elseif_conditions = NULL;
	LitStatements* elseif_branches = NULL;
	LitStatement* else_branch = NULL;

	while (match(parser, TOKEN_ELSE)) {
		// else if
		if (match(parser, TOKEN_IF)) {
			if (elseif_conditions == NULL) {
				elseif_conditions = lit_allocate_expressions(parser->state);
				elseif_branches = lit_allocate_statements(parser->state);
			}

			invert = match(parser, TOKEN_BANG);
			consume(parser, TOKEN_LEFT_PAREN, "Expected '('");
			LitExpression* e = parse_expression(parser);
			consume(parser, TOKEN_RIGHT_PAREN, "Expected ')'");

			if (invert) {
				e = (LitExpression*) lit_create_unary_expression(parser->state, condition->line, e, TOKEN_BANG);
			}

			lit_expressions_write(parser->state, elseif_conditions, e);

			lit_stataments_write(parser->state, elseif_branches, parse_statement(parser));

			continue;
		}

		// else
		if (else_branch != NULL) {
			error_at_current(parser, "If statement can only have one else branch");
		}

		else_branch = parse_statement(parser);
	}

	return (LitStatement*) lit_create_if_statement(parser->state, line, condition, if_branch, else_branch, elseif_conditions, elseif_branches);
}

static LitStatement* parse_for(LitParser* parser) {
	uint line = parser->previous.line;

	consume(parser, TOKEN_LEFT_PAREN, "Expected '('");

	LitStatement* var = NULL;
	LitExpression* init = NULL;

	if (!check(parser, TOKEN_SEMICOLON)) {
		if (match(parser, TOKEN_VAR)) {
			var = parse_var_declaration(parser);
		} else {
			init = parse_expression(parser);
		}
	}

	consume(parser, TOKEN_SEMICOLON, "Expected ';'");
	LitExpression* condition = check(parser, TOKEN_SEMICOLON) ? NULL : parse_expression(parser);

	consume(parser, TOKEN_SEMICOLON, "Expected ';'");
	LitExpression* increment = check(parser, TOKEN_RIGHT_PAREN) ? NULL : parse_expression(parser);

	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')'");

	LitStatement* body = parse_statement(parser);

	return (LitStatement*) lit_create_for_statement(parser->state, line, init, var, condition, increment, body);
}

static LitStatement* parse_while(LitParser* parser) {
	uint line = parser->previous.line;

	consume(parser, TOKEN_LEFT_PAREN, "Expected '('");
	LitExpression* condition = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')'");

	LitStatement* body = parse_statement(parser);

	return (LitStatement*) lit_create_while_statement(parser->state, line, condition, body);
}

static LitStatement* parse_function(LitParser* parser) {
	uint line = parser->previous.line;
	consume(parser, TOKEN_IDENTIFIER, "Expected function name");

	LitFunctionStatement* function = lit_create_function_statement(parser->state, line, parser->previous.start, parser->previous.length);

	LitCompiler compiler;
	init_compiler(parser, &compiler);
	begin_scope(parser);

	consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name");

	while (!check(parser, TOKEN_RIGHT_PAREN)) {
		consume(parser, TOKEN_IDENTIFIER, "Expected argument name");

		lit_parameters_write(parser->state, &function->parameters, (LitParameter) {
			parser->previous.start, parser->previous.length
		});

		if (!match(parser, TOKEN_COMMA)) {
			break;
		}
	}

	if (function->parameters.count > 255) {
		error(parser, "Function can't have more than 255 arguments");
	}

	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after function arguments");

	function->body = parse_statement(parser);

	end_scope(parser);
	end_compiler(parser, &compiler);

	return (LitStatement*) function;
}

static LitStatement* parse_return(LitParser* parser) {
	uint line = parser->previous.line;
	LitExpression* expression = NULL;

	if (!check(parser, TOKEN_NEW_LINE) && !check(parser, TOKEN_RIGHT_BRACE)) {
		expression = parse_expression(parser);
	}

	return (LitStatement*) lit_create_return_statement(parser->state, line, expression);
}

static LitStatement* parse_statement(LitParser* parser) {
	if (match(parser, TOKEN_VAR)) {
		return parse_var_declaration(parser);
	} else if (match(parser, TOKEN_IF)) {
		return parse_if(parser);
	} else if (match(parser, TOKEN_FOR)) {
		return parse_for(parser);
	} else if (match(parser, TOKEN_WHILE)) {
		return parse_while(parser);
	} else if (match(parser, TOKEN_CONTINUE)) {
		return (LitStatement*) lit_create_continue_statement(parser->state, parser->previous.line);
	} else if (match(parser, TOKEN_BREAK)) {
		return (LitStatement*) lit_create_break_statement(parser->state, parser->previous.line);
	} else if (match(parser, TOKEN_FUNCTION)) {
		return parse_function(parser);
	} else if (match(parser, TOKEN_RETURN)) {
		return parse_return(parser);
	} else if (match(parser, TOKEN_LEFT_BRACE)) {
		return parse_block(parser);
	}

	LitExpression* expression = parse_expression(parser);
	return expression == NULL ? NULL : (LitStatement*) lit_create_expression_statement(parser->state, parser->previous.line, expression);
}

static void sync(LitParser* parser) {
	parser->panic_mode = false;

	while (parser->current.type != TOKEN_EOF) {
		if (parser->previous.type == TOKEN_NEW_LINE) {
			return;
		}

		switch (parser->current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUNCTION:
			case TOKEN_VAR:
			case TOKEN_FOR:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_RETURN: {
				return;
			}

			default: {
				advance(parser);
			}
		}
	}
}

static LitStatement* parse_declaration(LitParser* parser) {
	LitStatement* statement = parse_statement(parser);

	if (parser->panic_mode) {
		sync(parser);
	}

	return statement;
}

bool lit_parse(LitParser* parser, const char* file_name, const char* source, LitStatements* statements) {
	parser->had_error = false;
	parser->panic_mode = false;

	lit_setup_scanner(parser->state->scanner, file_name, source);

	LitCompiler compiler;
	init_compiler(parser, &compiler);

	advance(parser);
	ignore_new_lines(parser);

	if (is_at_end(parser)) {
		error_at_current(parser, "Expected statement but got nothing");
	} else {
		do {
			LitStatement* statement = parse_declaration(parser);

			if (statement != NULL) {
				lit_stataments_write(parser->state, statements, statement);
			}

			if (!match_new_line(parser)) {
				consume(parser, TOKEN_EOF, "Expected end of file");
				break;
			}
		} while (!is_at_end(parser));
	}

	return parser->had_error;
}

static void setup_rules() {
	rules[TOKEN_LEFT_PAREN] = (LitParseRule) { parse_grouping, parse_call, PREC_CALL };
	rules[TOKEN_PLUS] = (LitParseRule) { NULL, parse_binary, PREC_TERM };
	rules[TOKEN_MINUS] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	rules[TOKEN_BANG] = (LitParseRule) { parse_unary, NULL, PREC_TERM };
	rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_SLASH] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_PERCENT] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_NUMBER] = (LitParseRule) { parse_number, NULL, PREC_NONE };
	rules[TOKEN_TRUE] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	rules[TOKEN_FALSE] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	rules[TOKEN_NULL] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	rules[TOKEN_BANG_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_EQUALITY };
	rules[TOKEN_EQUAL_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_EQUALITY };
	rules[TOKEN_GREATER] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	rules[TOKEN_GREATER_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	rules[TOKEN_LESS] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	rules[TOKEN_LESS_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	rules[TOKEN_STRING] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	rules[TOKEN_IDENTIFIER] = (LitParseRule) { parse_variable_expression, NULL, PREC_NONE };
	rules[TOKEN_PLUS_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_MINUS_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_STAR_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_SLASH_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_PERCENT_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_PLUS_PLUS] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_MINUS_MINUS] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_AMPERSAND_AMPERSAND] = (LitParseRule) { NULL, parse_and, PREC_AND };
	rules[TOKEN_BAR_BAR] = (LitParseRule) { NULL, parse_or, PREC_AND };
	rules[TOKEN_QUESTION_QUESTION] = (LitParseRule) { NULL, parse_null_filter, PREC_NULL };
}