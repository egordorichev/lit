#include <lit/parser/lit_parser.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/vm/lit_object.h>
#include <lit/lit_predefines.h>

#include <stdlib.h>

static void init_compiler(LitParser* parser, LitCompiler* compiler) {
	compiler->scope_depth = 0;
	compiler->function = NULL;
	compiler->enclosing = (struct LitCompiler *) parser->compiler;

	parser->compiler = compiler;
}

static void end_compiler(LitParser* parser, LitCompiler* compiler) {
	parser->compiler = (LitCompiler *) compiler->enclosing;
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
static LitExpression* parse_subscript(LitParser* parser, LitExpression* previous, bool can_assign);

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
	return (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, NUMBER_VALUE(strtod(parser->previous.start, NULL)));
}

static LitExpression* parse_lambda(LitParser* parser, LitLambdaExpression* lambda) {
	lambda->body = parse_statement(parser);

	return (LitExpression *) lambda;
}

static LitExpression* parse_grouping_or_lambda(LitParser* parser, bool can_assign) {
	if (match(parser, TOKEN_RIGHT_PAREN)) {
		consume(parser, TOKEN_ARROW, "Expected => after lambda arguments");
		return parse_lambda(parser, lit_create_lambda_expression(parser->state, parser->previous.line));
	}

	const char* start = parser->previous.start;
	uint line = parser->previous.line;

	if (match(parser, TOKEN_IDENTIFIER)) {
		LitState* state = parser->state;

		const char* arg_start = parser->previous.start;
		uint arg_length = parser->previous.length;

		if (match(parser, TOKEN_COMMA) || (match(parser, TOKEN_RIGHT_PAREN) && match(parser, TOKEN_ARROW))) {
			// This is a lambda
			LitLambdaExpression* lambda = lit_create_lambda_expression(state, line);
			lit_parameters_write(state, &lambda->parameters, (LitParameter) { arg_start, arg_length });

			if (parser->previous.type == TOKEN_COMMA) {
				do {
					consume(parser, TOKEN_IDENTIFIER, "Expected argument name");
					lit_parameters_write(state, &lambda->parameters, (LitParameter) { parser->previous.start, parser->previous.length });
				} while (match(parser, TOKEN_COMMA));

			}

			consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' lambda parameters");
			consume(parser, TOKEN_ARROW, "Expected => after lambda arguments");

			return parse_lambda(parser, lambda);
		} else {
			// Ouch, this was a grouping with a single identifier

			LitScanner* scanner = state->scanner;

			scanner->current = start;
			scanner->line = line;

			parser->current = lit_scan_token(scanner);
			advance(parser);
		}
	}

	LitExpression* expression = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after grouping expression");

	return expression;
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
	bool invert = parser->previous.type == TOKEN_BANG;

	if (invert) {
		consume(parser, TOKEN_IS, "Expected 'is' after '!'");
	}

	LitTokenType operator = parser->previous.type;
	uint line = parser->previous.line;

	LitParseRule* rule = get_rule(operator);
	LitExpression* expression = parse_precedence(parser, (LitPrecedence) (rule->precedence + 1));

	expression = (LitExpression*) lit_create_binary_expression(parser->state, line, prev, expression, operator);

	if (invert) {
		expression = (LitExpression*) lit_create_unary_expression(parser->state, line, expression, TOKEN_BANG);
	}

	return expression;
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
		expression = (LitExpression*) lit_create_literal_expression(parser->state, line, NUMBER_VALUE(1));
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
			return (LitExpression*) lit_create_literal_expression(parser->state, line, TRUE_VALUE);
		}

		case TOKEN_FALSE: {
			return (LitExpression*) lit_create_literal_expression(parser->state, line, FALSE_VALUE);
		}

		case TOKEN_NULL: {
			return (LitExpression*) lit_create_literal_expression(parser->state, line, NULL_VALUE);
		}

		case TOKEN_STRING: {
			LitExpression* expression = (LitExpression*) lit_create_literal_expression(parser->state, line, OBJECT_VALUE(lit_copy_string(parser->state, parser->previous.start + 1, parser->previous.length - 2)));

			if (match(parser, TOKEN_LEFT_BRACKET)) {
				return parse_subscript(parser, expression, can_assign);
			}

			return expression;
		}

		default: UNREACHABLE
	}
}

static LitExpression* parse_variable_expression_base(LitParser* parser, bool can_assign, bool new) {
	LitExpression* expression = (LitExpression*) lit_create_var_expression(parser->state, parser->previous.line, parser->previous);

	if (new) {
		if (!check(parser, TOKEN_LEFT_PAREN)) {
			error_at_current(parser, "Expected argument list for instance creation");
		}

		return expression;
	}

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, expression, can_assign);
	}

	if (can_assign && match(parser, TOKEN_EQUAL)) {
		return (LitExpression*) lit_create_assign_expression(parser->state, parser->previous.line, expression, parse_expression(parser));
	}

	return expression;
}

static LitExpression* parse_variable_expression(LitParser* parser, bool can_assign) {
	return parse_variable_expression_base(parser, can_assign, false);
}

static LitExpression* parse_new_expression(LitParser* parser, bool can_assign) {
	consume(parser, TOKEN_IDENTIFIER, "Expected class name after 'new'");
	return parse_variable_expression_base(parser, false, true);
}

static LitExpression* parse_require(LitParser* parser, bool can_assign) {
	uint line = parser->previous.line;
	return (LitExpression*) lit_create_require_expression(parser->state, line, parse_expression(parser));
}

static LitExpression* parse_dot(LitParser* parser, LitExpression* previous, bool can_assign) {
	uint line = parser->previous.line;
	consume(parser, TOKEN_IDENTIFIER, "Expected property name after '.'");

	const char* name = parser->previous.start;
	uint length = parser->previous.length;

	if (can_assign && match(parser, TOKEN_EQUAL)) {
		return (LitExpression *) lit_create_set_expression(parser->state, line, previous, name, length, parse_expression(parser));
	} else {
		LitExpression* expression = (LitExpression*) lit_create_get_expression(parser->state, line, previous, name, length, false);

		if (match(parser, TOKEN_LEFT_BRACKET)) {
			return parse_subscript(parser, expression, can_assign);
		}

		return expression;
	}
}

static LitExpression* parse_range(LitParser* parser, LitExpression* previous, bool can_assign) {
	uint line = parser->previous.line;
	return (LitExpression*) lit_create_range_expression(parser->state, line, previous, parse_expression(parser));
}

static LitExpression* parse_ternary_or_question(LitParser* parser, LitExpression* previous, bool can_assign) {
	uint line = parser->previous.line;

	if (match(parser, TOKEN_DOT)) {
		consume(parser, TOKEN_IDENTIFIER, "Expected property name after '.'");
		return (LitExpression*) lit_create_get_expression(parser->state, line, previous, parser->previous.start, parser->previous.length, true);
	}

	LitExpression* if_branch = parse_expression(parser);
	consume(parser, TOKEN_COLON, "Expected ':' after expression");
	LitExpression* else_branch = parse_expression(parser);

	return (LitExpression *) lit_create_if_experssion(parser->state, line, previous, if_branch, else_branch);
}

static LitExpression* parse_array(LitParser* parser, bool can_assign) {
	LitArrayExpression* array = lit_create_array_expression(parser->state, parser->previous.line);
	ignore_new_lines(parser);

	while (!check(parser, TOKEN_RIGHT_BRACKET)) {
		ignore_new_lines(parser);
		lit_expressions_write(parser->state, &array->values, parse_expression(parser));

		if (!match(parser,  TOKEN_COMMA)) {
			break;
		}
	}

	ignore_new_lines(parser);
	consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array");

	return (LitExpression*) array;
}

static LitExpression* parse_map(LitParser* parser, bool can_assign) {
	LitMapExpression* map = lit_create_map_expression(parser->state, parser->previous.line);
	ignore_new_lines(parser);

	while (!check(parser, TOKEN_RIGHT_BRACE)) {
		ignore_new_lines(parser);
		consume(parser, TOKEN_STRING, "Expected key string after '{'");
		lit_values_write(parser->state, &map->keys, OBJECT_VALUE(lit_copy_string(parser->state, parser->previous.start + 1, parser->previous.length - 2)));

		ignore_new_lines(parser);
		consume(parser, TOKEN_COLON, "Expected ':' after key string");

		ignore_new_lines(parser);
		lit_expressions_write(parser->state, &map->values, parse_expression(parser));

		if (!match(parser,  TOKEN_COMMA)) {
			break;
		}
	}

	ignore_new_lines(parser);
	consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after map");

	return (LitExpression*) map;
}

static LitExpression* parse_subscript(LitParser* parser, LitExpression* previous, bool can_assign) {
	uint line = parser->previous.line;

	LitExpression* index = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after subscript");

	LitExpression* expression = (LitExpression*) lit_create_subscript_expression(parser->state, line, previous, index);

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, expression, can_assign);
	} else if (can_assign && match(parser, TOKEN_EQUAL)) {
		return (LitExpression*) lit_create_assign_expression(parser->state, parser->previous.line, expression, parse_expression(parser));
	}

	return expression;
}

static LitExpression* parse_this(LitParser* parser, bool can_assign) {
	return (LitExpression*) lit_create_this_expression(parser->state, parser->previous.line);
}

static LitExpression* parse_super(LitParser* parser, bool can_assign) {
	uint line = parser->previous.line;

	if (!match(parser, TOKEN_DOT)) {
		LitExpression* expression = (LitExpression*) lit_create_super_expression(parser->state, line, lit_copy_string(parser->state, "constructor", 11));
		consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'super'");

		return parse_call(parser, expression, false);
	}

	consume(parser, TOKEN_IDENTIFIER, "Expected super method name after '.'");

	LitExpression* expression = (LitExpression*) lit_create_super_expression(parser->state, line, lit_copy_string(parser->state, parser->previous.start, parser->previous.length));

	if (match(parser, TOKEN_LEFT_PAREN)) {
		return parse_call(parser, expression, false);
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
	bool export = parser->previous.type == TOKEN_EXPORT;

	if (export) {
		consume(parser, TOKEN_FUNCTION, "'function' after 'export'");
	}

	uint line = parser->previous.line;
	consume(parser, TOKEN_IDENTIFIER, "Expected function name");

	LitFunctionStatement* function = lit_create_function_statement(parser->state, line, parser->previous.start, parser->previous.length);
	function->export = export;

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
		error(parser, "Functions can't have more than 255 arguments");
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


static LitStatement* parse_field(LitParser* parser, LitString* name, bool is_static) {
	uint line = parser->previous.line;

	LitStatement* getter = NULL;
	LitStatement* setter = NULL;

	if (match(parser, TOKEN_ARROW)) {
		getter = parse_statement(parser);
	} else {
		match(parser, TOKEN_LEFT_BRACE); // Will be TOKEN_LEFT_BRACE, otherwise this method won't be called
		ignore_new_lines(parser);

		if (match(parser, TOKEN_GET)) {
			match(parser, TOKEN_ARROW); // Ignore it if it's present
			getter = parse_statement(parser);
		}

		ignore_new_lines(parser);

		if (match(parser, TOKEN_SET)) {
			match(parser, TOKEN_ARROW); // Ignore it if it's present
			setter = parse_statement(parser);
		}

		if (getter == NULL && setter == NULL) {
			error(parser, "Expected declaration of either getter or setter, got none");
		}

		ignore_new_lines(parser);
		consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after field declaration");
	}

	return (LitStatement *) lit_create_field_statement(parser->state, line, name, getter, setter, is_static);
}

static LitTokenType operators[] = {
	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_STAR,
	TOKEN_PERCENT,
	TOKEN_SLASH,

	TOKEN_BANG,
	TOKEN_LESS,
	TOKEN_LESS_EQUAL,
	TOKEN_GREATER,
	TOKEN_GREATER_EQUAL,
	TOKEN_EQUAL_EQUAL,

	TOKEN_LEFT_BRACKET,

	TOKEN_EOF
};

static LitStatement* parse_method(LitParser* parser, bool is_static) {
	if (match(parser, TOKEN_STATIC)) {
		is_static = true;
	}

	LitString* name = NULL;

	if (match(parser, TOKEN_OPERATOR)) {
		if (is_static) {
			error(parser, "Operator methods can't be static or defined in static classes");
		}

		uint i = 0;

		while (operators[i] != TOKEN_EOF) {
			if (match(parser, operators[i])) {
				break;
			}

			i++;
		}

		if (parser->previous.type == TOKEN_LEFT_BRACKET) {
			consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after '[' in operator method declaration");
			name = lit_copy_string(parser->state, "[]", 2);
		} else {
			name = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);
		}
	} else {
		consume(parser, TOKEN_IDENTIFIER, "Expected method name");
		name = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);

		if (check(parser, TOKEN_LEFT_BRACE) || check(parser, TOKEN_ARROW)) {
			return parse_field(parser, name, is_static);
		}
	}

	LitMethodStatement* method = lit_create_method_statement(parser->state, parser->previous.line, name, is_static);

	LitCompiler compiler;
	init_compiler(parser, &compiler);
	begin_scope(parser);

	consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after method name");

	while (!check(parser, TOKEN_RIGHT_PAREN)) {
		consume(parser, TOKEN_IDENTIFIER, "Expected argument name");

		lit_parameters_write(parser->state, &method->parameters, (LitParameter) {
			parser->previous.start, parser->previous.length
		});

		if (!match(parser, TOKEN_COMMA)) {
			break;
		}
	}

	if (method->parameters.count > 255) {
		error(parser, "Methods can't have more than 255 arguments");
	}

	consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after method arguments");

	method->body = parse_statement(parser);

	end_scope(parser);
	end_compiler(parser, &compiler);

	return (LitStatement*) method;
}

static LitStatement* parse_class(LitParser* parser) {
	uint line = parser->previous.line;

	bool is_static = parser->previous.type == TOKEN_STATIC;

	if (is_static) {
		consume(parser, TOKEN_CLASS, "Expected 'class' after 'static'");
	}

	consume(parser, TOKEN_IDENTIFIER, "Expected class name after 'class'");
	LitString* name = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);
	LitString* super = NULL;

	if (match(parser, TOKEN_COLON)) {
		consume(parser, TOKEN_IDENTIFIER, "Expected super class name after ':'");
		super = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);

		if (super == name) {
			error(parser, "Classes can't inherit itselfes");
		}
	}

	LitClassStatement* klass = lit_create_class_statement(parser->state, line, name, super);

	ignore_new_lines(parser);
	consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before class body");
	ignore_new_lines(parser);

	bool finished_parsing_fields = false;

	while (!check(parser, TOKEN_RIGHT_BRACE)) {
		bool field_is_static = false;

		if (match(parser, TOKEN_STATIC)) {
			field_is_static = true;

			if (match(parser, TOKEN_VAR)) {
				if (finished_parsing_fields) {
					error(parser, "All static fields must be defined before the methods");
				}

				LitStatement* var = parse_var_declaration(parser);

				if (var != NULL) {
					lit_stataments_write(parser->state, &klass->fields, var);
				}

				ignore_new_lines(parser);
				continue;
			} else {
				finished_parsing_fields = true;
			}
		}

		LitStatement* method = parse_method(parser, is_static || field_is_static);

		if (method != NULL) {
			lit_stataments_write(parser->state, &klass->fields, method);
		}

		ignore_new_lines(parser);
	}

	consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after class body");

	return (LitStatement*) klass;
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
	} else if (match(parser, TOKEN_FUNCTION) || match(parser, TOKEN_EXPORT)) {
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
			case TOKEN_STATIC:
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
	if (match(parser, TOKEN_CLASS) || match(parser, TOKEN_STATIC)) {
		return parse_class(parser);
	}

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
	rules[TOKEN_LEFT_PAREN] = (LitParseRule) { parse_grouping_or_lambda, parse_call, PREC_CALL };
	rules[TOKEN_PLUS] = (LitParseRule) { NULL, parse_binary, PREC_TERM };
	rules[TOKEN_MINUS] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	rules[TOKEN_BANG] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_STAR_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_SLASH] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_PERCENT] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_IS] = (LitParseRule) { NULL, parse_binary, PREC_IS };
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
	rules[TOKEN_NEW] = (LitParseRule) { parse_new_expression, NULL, PREC_NONE };
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
	rules[TOKEN_REQUIRE] = (LitParseRule) { parse_require, NULL, PREC_NONE };
	rules[TOKEN_DOT] = (LitParseRule) { NULL, parse_dot, PREC_CALL };
	rules[TOKEN_DOT_DOT] = (LitParseRule) { NULL, parse_range, PREC_RANGE };
	rules[TOKEN_LEFT_BRACKET] = (LitParseRule) { parse_array, parse_subscript, PREC_NONE };
	rules[TOKEN_LEFT_BRACE] = (LitParseRule) { parse_map, NULL, PREC_NONE };
	rules[TOKEN_THIS] = (LitParseRule) { parse_this, NULL, PREC_NONE };
	rules[TOKEN_SUPER] = (LitParseRule) { parse_super, NULL, PREC_NONE };
	rules[TOKEN_QUESTION] = (LitParseRule) { NULL, parse_ternary_or_question, PREC_EQUALITY };
}