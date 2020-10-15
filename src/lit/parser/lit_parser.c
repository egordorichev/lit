#include "parser/lit_parser.h"
#include "parser/lit_error.h"
#include "scanner/lit_scanner.h"
#include "vm/lit_object.h"
#include "lit_predefines.h"

#include <stdlib.h>
#include <setjmp.h>

static void sync(LitParser* parser);
static jmp_buf jump_buffer;

static void init_compiler(LitParser* parser, LitCompiler* compiler) {
	compiler->scope_depth = 0;
	compiler->function = NULL;
	compiler->enclosing = (struct LitCompiler*) parser->compiler;

	parser->compiler = compiler;
}

static void end_compiler(LitParser* parser, LitCompiler* compiler) {
	parser->compiler = (LitCompiler*) compiler->enclosing;
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
static LitExpression* parse_interpolation(LitParser* parser, bool can_assign);
static LitExpression* parse_string(LitParser* parser, bool can_assign);

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

static void string_error(LitParser* parser, LitToken* token, const char* message) {
	if (parser->panic_mode) {
		return;
	}

	lit_error(parser->state, COMPILE_ERROR, message);
	parser->had_error = true;
	sync(parser);
}

static void error_at(LitParser* parser, LitToken* token, LitError error, va_list args) {
	string_error(parser, token, lit_vformat_error(parser->state, token->line, error, args)->chars);
}

static void error_at_current(LitParser* parser, LitError error, ...) {
	va_list args;
	va_start(args, error);
	error_at(parser, &parser->current, error, args);
	va_end(args);
}

static void error(LitParser* parser, LitError error, ...) {
	va_list args;
	va_start(args, error);
	error_at(parser, &parser->previous, error, args);
	va_end(args);
}

static void advance(LitParser* parser) {
	parser->previous = parser->current;

	while (true) {
		parser->current = lit_scan_token(parser->state->scanner);

		if (parser->current.type != TOKEN_ERROR) {
			break;
		}

		string_error(parser, &parser->current, parser->current.start);
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

static void consume(LitParser* parser, LitTokenType type, const char* error) {
	if (parser->current.type == type) {
		advance(parser);
		return;
	}

	bool line = parser->previous.type == TOKEN_NEW_LINE;
	string_error(parser, &parser->current, lit_format_error(parser->state, parser->current.line, ERROR_EXPECTION_UNMET, error, line ? 8 : parser->previous.length, line ? "new line" : parser->previous.start)->chars);
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

static LitStatement* parse_block(LitParser* parser) {
	begin_scope(parser);
	LitBlockStatement* statement = lit_create_block_statement(parser->state, parser->previous.line);
	ignore_new_lines(parser);

	while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
		lit_stataments_write(parser->state, &statement->statements, parse_statement(parser));
		ignore_new_lines(parser);
	}

	consume(parser, TOKEN_RIGHT_BRACE, "'}'");
	end_scope(parser);

	return (LitStatement*) statement;
}

static LitExpression* parse_precedence(LitParser* parser, LitPrecedence precedence, bool err) {
	LitToken previous = parser->previous;

	advance(parser);
	LitPrefixParseFn prefix_rule = get_rule(parser->previous.type)->prefix;

	if (prefix_rule == NULL) {
		// todo: file start
		bool prev_newline = previous.start != NULL && *previous.start == '\n';
		bool parser_prev_newline = parser->previous.start != NULL && *parser->previous.start == '\n';

		error(parser, ERROR_EXPECTED_EXPRESSION, prev_newline ? 8 : previous.length, prev_newline ? "new line" : previous.start,
			parser_prev_newline ? 8 : parser->previous.length, parser_prev_newline ? "new line" : parser->previous.start);
		return NULL;
	}

	bool can_assign = precedence <= PREC_ASSIGNMENT;
	LitExpression* expr = prefix_rule(parser, can_assign);

	ignore_new_lines(parser);

	while (precedence <= get_rule(parser->current.type)->precedence) {
		advance(parser);
		LitInfixParseFn infix_rule = get_rule(parser->previous.type)->infix;
		expr = infix_rule(parser, expr, can_assign);
	}

	if (err && can_assign && match(parser, TOKEN_EQUAL)) {
		error(parser, ERROR_INVALID_ASSIGMENT_TARGET);
	}

	return expr;
}

static LitExpression* parse_number(LitParser* parser, bool can_assign) {
	return (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, parser->previous.value);
}

static LitExpression* parse_lambda(LitParser* parser, LitLambdaExpression* lambda) {
	lambda->body = parse_statement(parser);

	return (LitExpression*) lambda;
}

static void parse_parameters(LitParser* parser, LitParameters* parameters) {
	bool had_default = false;

	while (!check(parser, TOKEN_RIGHT_PAREN)) {
		// Vararg ...
		if (match(parser, TOKEN_DOT_DOT_DOT)) {
			lit_parameters_write(parser->state, parameters, (LitParameter) {
				"...", 3, NULL
			});

			return;
		}

		consume(parser, TOKEN_IDENTIFIER, "argument name");
		const char* arg_name = parser->previous.start;
		uint arg_length = parser->previous.length;
		LitExpression* default_value = NULL;

		if (match(parser, TOKEN_EQUAL)) {
			had_default = true;
			default_value = parse_expression(parser);
		} else if (had_default) {
			error(parser, ERROR_DEFAULT_ARG_CENTRED);
		}

		lit_parameters_write(parser->state, parameters, (LitParameter) {
			arg_name, arg_length, default_value
		});

		if (!match(parser, TOKEN_COMMA)) {
			break;
		}
	}
}

static LitExpression* parse_grouping_or_lambda(LitParser* parser, bool can_assign) {
	if (match(parser, TOKEN_RIGHT_PAREN)) {
		consume(parser, TOKEN_ARROW, "=> after lambda arguments");
		return parse_lambda(parser, lit_create_lambda_expression(parser->state, parser->previous.line));
	}

	const char* start = parser->previous.start;
	uint line = parser->previous.line;

	if (match(parser, TOKEN_IDENTIFIER) || match(parser, TOKEN_DOT_DOT_DOT)) {
		LitState* state = parser->state;

		const char* first_arg_start = parser->previous.start;
		uint first_arg_length = parser->previous.length;

		if (match(parser, TOKEN_COMMA) || (match(parser, TOKEN_RIGHT_PAREN) && match(parser, TOKEN_ARROW))) {
			bool had_arrow = parser->previous.type == TOKEN_ARROW;
			bool had_vararg = parser->previous.type == TOKEN_DOT_DOT_DOT;

			// This is a lambda
			LitLambdaExpression* lambda = lit_create_lambda_expression(state, line);
			LitExpression* def_value = NULL;

			bool had_default = match(parser, TOKEN_EQUAL);

			if (had_default) {
				def_value = parse_expression(parser);
			}

			lit_parameters_write(state, &lambda->parameters, (LitParameter) { first_arg_start, first_arg_length, def_value });

			if (!had_vararg && parser->previous.type == TOKEN_COMMA) {
				do {
					bool stop = false;

					if (match(parser, TOKEN_DOT_DOT_DOT)) {
						stop = true;
					} else {
						consume(parser, TOKEN_IDENTIFIER, "argument name");
					}

					const char* arg_name = parser->previous.start;
					uint arg_length = parser->previous.length;
					LitExpression* default_value = NULL;

					if (match(parser, TOKEN_EQUAL)) {
						default_value = parse_expression(parser);
						had_default = true;
					} else if (had_default) {
						error(parser, ERROR_DEFAULT_ARG_CENTRED);
					}

					lit_parameters_write(state, &lambda->parameters, (LitParameter) { arg_name, arg_length, default_value });

					if (stop) {
						break;
					}
				} while (match(parser, TOKEN_COMMA));
			}

			if (!had_arrow) {
				consume(parser, TOKEN_RIGHT_PAREN, "')' after lambda parameters");
				consume(parser, TOKEN_ARROW, "=> after lambda parameters");
			}

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
	consume(parser, TOKEN_RIGHT_PAREN, "')' after grouping expression");

	return expression;
}

static LitExpression* parse_call(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitCallExpression* expression = lit_create_call_expression(parser->state, parser->previous.line, prev);

	while (!check(parser, TOKEN_RIGHT_PAREN)) {
		LitExpression* e = parse_expression(parser);
		lit_expressions_write(parser->state, &expression->args, e);

		if (!match(parser, TOKEN_COMMA)) {
			break;
		}

		if (e->type == VAR_EXPRESSION) {
			LitVarExpression* ee = (LitVarExpression*) e;

			// Vararg ...
			if (ee->length == 3 && memcmp(ee->name, "...", 3) == 0) {
				break;
			}
		}
	}

	if (expression->args.count > 255) {
		error(parser, ERROR_TOO_MANY_FUNCTION_ARGS, (int) expression->args.count);
	}

	consume(parser, TOKEN_RIGHT_PAREN, "')' after arguments");
	return (LitExpression*) expression;
}

static LitExpression* parse_unary(LitParser* parser, bool can_assign) {
	LitTokenType op = parser->previous.type;
	uint line = parser->previous.line;
	LitExpression* expression = parse_precedence(parser, PREC_UNARY, true);

	return (LitExpression*) lit_create_unary_expression(parser->state, line, expression, op);
}

static LitExpression* parse_binary(LitParser* parser, LitExpression* prev, bool can_assign) {
	bool invert = parser->previous.type == TOKEN_BANG;

	if (invert) {
		consume(parser, TOKEN_IS, "'is' after '!'");
	}

	LitTokenType op = parser->previous.type;
	uint line = parser->previous.line;

	LitParseRule* rule = get_rule(op);
	LitExpression* expression = parse_precedence(parser, (LitPrecedence) (rule->precedence + 1), true);

	expression = (LitExpression*) lit_create_binary_expression(parser->state, line, prev, expression, op);

	if (invert) {
		expression = (LitExpression*) lit_create_unary_expression(parser->state, line, expression, TOKEN_BANG);
	}

	return expression;
}

static LitExpression* parse_and(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType op = parser->previous.type;
	uint line = parser->previous.line;

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, parse_precedence(parser, PREC_AND, true), op);
}

static LitExpression* parse_or(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType op = parser->previous.type;
	uint line = parser->previous.line;

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, parse_precedence(parser, PREC_OR, true), op);
}

static LitExpression* parse_null_filter(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType op = parser->previous.type;
	uint line = parser->previous.line;

	return (LitExpression*) lit_create_binary_expression(parser->state, line, prev, parse_precedence(parser, PREC_NULL, true), op);
}

static LitTokenType convert_compound_operator(LitTokenType op) {
	switch (op) {
		case TOKEN_PLUS_EQUAL: return TOKEN_PLUS;
		case TOKEN_MINUS_EQUAL: return TOKEN_MINUS;
		case TOKEN_STAR_EQUAL: return TOKEN_STAR;
		case TOKEN_SLASH_EQUAL: return TOKEN_SLASH;
		case TOKEN_SHARP_EQUAL: return TOKEN_SHARP;
		case TOKEN_PERCENT_EQUAL: return TOKEN_PERCENT;
		case TOKEN_CARET_EQUAL: return TOKEN_CARET;
		case TOKEN_BAR_EQUAL: return TOKEN_BAR;
		case TOKEN_AMPERSAND_EQUAL: return TOKEN_AMPERSAND;

		case TOKEN_PLUS_PLUS: return TOKEN_PLUS;
		case TOKEN_MINUS_MINUS: return TOKEN_MINUS;

		default: {
			UNREACHABLE
		}
	}
}

static LitExpression* parse_compound(LitParser* parser, LitExpression* prev, bool can_assign) {
	LitTokenType op = parser->previous.type;
	uint line = parser->previous.line;

	LitParseRule* rule = get_rule(op);
	LitExpression* expression;

	if (op == TOKEN_PLUS_PLUS || op == TOKEN_MINUS_MINUS) {
		expression = (LitExpression*) lit_create_literal_expression(parser->state, line, NUMBER_VALUE(1));
	} else {
		expression = parse_precedence(parser, (LitPrecedence) (rule->precedence + 1), true);
	}

	LitBinaryExpression* binary = lit_create_binary_expression(parser->state, line, prev, expression, convert_compound_operator(op));
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

		default: UNREACHABLE
	}
}

static LitExpression* parse_string(LitParser* parser, bool can_assign) {
	LitExpression* expression = (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, parser->previous.value);

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, expression, can_assign);
	}

	return expression;
}

static LitExpression* parse_interpolation(LitParser* parser, bool can_assign) {
	LitInterpolationExpression* expression = lit_create_interpolation_expression(parser->state, parser->previous.line);

	do {
		if (AS_STRING(parser->previous.value)->length > 0) {
			lit_expressions_write(parser->state, &expression->expressions, (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, parser->previous.value));
		}

		lit_expressions_write(parser->state, &expression->expressions, parse_expression(parser));
	} while (match(parser, TOKEN_INTERPOLATION));

	consume(parser, TOKEN_STRING, "end of interpolation");

	if (AS_STRING(parser->previous.value)->length > 0) {
		lit_expressions_write(parser->state, &expression->expressions, (LitExpression*) lit_create_literal_expression(parser->state, parser->previous.line, parser->previous.value));
	}

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, (LitExpression*) expression, can_assign);
	}

	return (LitExpression*) expression;
}

static LitExpression* parse_object(LitParser* parser, bool can_assign) {
	LitObjectExpression* object = lit_create_object_expression(parser->state, parser->previous.line);
	ignore_new_lines(parser);

	while (!check(parser, TOKEN_RIGHT_BRACE)) {
		ignore_new_lines(parser);
		consume(parser, TOKEN_IDENTIFIER, "key string after '{'");
		lit_values_write(parser->state, &object->keys, OBJECT_VALUE(lit_copy_string(parser->state, parser->previous.start, parser->previous.length)));

		ignore_new_lines(parser);
		consume(parser, TOKEN_EQUAL, "'=' after key string");

		ignore_new_lines(parser);
		lit_expressions_write(parser->state, &object->values, parse_expression(parser));

		if (!match(parser,  TOKEN_COMMA)) {
			break;
		}
	}

	ignore_new_lines(parser);
	consume(parser, TOKEN_RIGHT_BRACE, "'}' after object");

	return (LitExpression*) object;
}

static LitExpression* parse_variable_expression_base(LitParser* parser, bool can_assign, bool new) {
	LitExpression* expression = (LitExpression*) lit_create_var_expression(parser->state, parser->previous.line, parser->previous.start, parser->previous.length);

	if (new) {
		bool had_args = check(parser, TOKEN_LEFT_PAREN);
		LitCallExpression* call = NULL;

		if (had_args) {
			advance(parser);
			call = (LitCallExpression*) parse_call(parser, expression, false);
		}

		if (match(parser, TOKEN_LEFT_BRACE)) {
			if (call == NULL) {
				call = lit_create_call_expression(parser->state, expression->line, expression);
			}

			call->init = parse_object(parser, false);
		} else if (!had_args) {
			error_at_current(parser, ERROR_EXPECTION_UNMET, "argument list for instance creation", parser->previous.length, parser->previous.start);
		}

		return (LitExpression*) call;
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
	consume(parser, TOKEN_IDENTIFIER, "class name after 'new'");
	return parse_variable_expression_base(parser, false, true);
}

static LitExpression* parse_dot(LitParser* parser, LitExpression* previous, bool can_assign) {
	uint line = parser->previous.line;
	bool ignored = parser->previous.type == TOKEN_SMALL_ARROW;

	if (!(match(parser, TOKEN_CLASS) || match(parser, TOKEN_SUPER))) { // class and super are allowed field names
		consume(parser, TOKEN_IDENTIFIER, ignored ? "propety name after '->'" : "property name after '.'");
	}

	const char* name = parser->previous.start;
	uint length = parser->previous.length;

	if (!ignored && can_assign && match(parser, TOKEN_EQUAL)) {
		return (LitExpression*) lit_create_set_expression(parser->state, line, previous, name, length, parse_expression(parser));
	} else {
		LitExpression* expression = (LitExpression*) lit_create_get_expression(parser->state, line, previous, name, length, false, ignored);

		if (!ignored && match(parser, TOKEN_LEFT_BRACKET)) {
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

	if (match(parser, TOKEN_DOT) || match(parser, TOKEN_SMALL_ARROW)) {
		bool ignored = parser->previous.type == TOKEN_SMALL_ARROW;
		consume(parser, TOKEN_IDENTIFIER, ignored ? "property name after '->'" : "property name after '.'");
		return (LitExpression*) lit_create_get_expression(parser->state, line, previous, parser->previous.start, parser->previous.length, true, ignored);
	}

	LitExpression* if_branch = parse_expression(parser);
	consume(parser, TOKEN_COLON, "':' after expression");
	LitExpression* else_branch = parse_expression(parser);

	return (LitExpression*) lit_create_if_experssion(parser->state, line, previous, if_branch, else_branch);
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
	consume(parser, TOKEN_RIGHT_BRACKET, "']' after array");

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, (LitExpression*) array, can_assign);
	}

	return (LitExpression*) array;
}

static LitExpression* parse_subscript(LitParser* parser, LitExpression* previous, bool can_assign) {
	uint line = parser->previous.line;

	LitExpression* index = parse_expression(parser);
	consume(parser, TOKEN_RIGHT_BRACKET, "']' after subscript");

	LitExpression* expression = (LitExpression*) lit_create_subscript_expression(parser->state, line, previous, index);

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, expression, can_assign);
	} else if (can_assign && match(parser, TOKEN_EQUAL)) {
		return (LitExpression*) lit_create_assign_expression(parser->state, parser->previous.line, expression, parse_expression(parser));
	}

	return expression;
}

static LitExpression* parse_this(LitParser* parser, bool can_assign) {
	LitExpression* expression = (LitExpression*) lit_create_this_expression(parser->state, parser->previous.line);

	if (match(parser, TOKEN_LEFT_BRACKET)) {
		return parse_subscript(parser, expression, can_assign);
	}

	return expression;
}

static LitExpression* parse_super(LitParser* parser, bool can_assign) {
	uint line = parser->previous.line;

	if (!(match(parser, TOKEN_DOT) || match(parser, TOKEN_SMALL_ARROW))) {
		LitExpression* expression = (LitExpression*) lit_create_super_expression(parser->state, line, lit_copy_string(parser->state, "constructor", 11), false);
		consume(parser, TOKEN_LEFT_PAREN, "'(' after 'super'");

		return parse_call(parser, expression, false);
	}

	bool ignoring = parser->previous.type == TOKEN_SMALL_ARROW;
	consume(parser, TOKEN_IDENTIFIER, ignoring ? "super method name after '->'" : "super method name after '.'");

	LitExpression* expression = (LitExpression*) lit_create_super_expression(parser->state, line, lit_copy_string(parser->state, parser->previous.start, parser->previous.length), ignoring);

	if (match(parser, TOKEN_LEFT_PAREN)) {
		return parse_call(parser, expression, false);
	}

	return expression;
}

static LitExpression* parse_reference(LitParser* parser, bool can_assign) {
	uint line = parser->previous.line;
	ignore_new_lines(parser);

	LitReferenceExpression* expression = lit_create_reference_expression(parser->state, line, parse_precedence(parser, PREC_CALL, false));

	if (match(parser, TOKEN_EQUAL)) {
		return (LitExpression*) lit_create_assign_expression(parser->state, line, (LitExpression*) expression, parse_expression(parser));
	}

	return (LitExpression*) expression;
}

static LitExpression* parse_expression(LitParser* parser) {
	ignore_new_lines(parser);
	return parse_precedence(parser, PREC_ASSIGNMENT, true);
}

static LitStatement* parse_var_declaration(LitParser* parser) {
	bool constant = parser->previous.type == TOKEN_CONST;
	uint line = parser->previous.line;

	consume(parser, TOKEN_IDENTIFIER, "variable name");

	const char* name = parser->previous.start;
	uint length = parser->previous.length;

	LitExpression* init = NULL;

	if (match(parser, TOKEN_EQUAL)) {
		init = parse_expression(parser);
	}

	return (LitStatement*) lit_create_var_statement(parser->state, line, name, length, init, constant);
}

static LitStatement* parse_if(LitParser* parser) {
	uint line = parser->previous.line;
	bool invert = match(parser, TOKEN_BANG);

	bool had_paren = match(parser, TOKEN_LEFT_PAREN);
	LitExpression* condition = parse_expression(parser);

	if (had_paren) {
		consume(parser, TOKEN_RIGHT_PAREN, "')'");
	}

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
			had_paren = match(parser, TOKEN_LEFT_PAREN);
			LitExpression* e = parse_expression(parser);

			if (had_paren) {
				consume(parser, TOKEN_RIGHT_PAREN, "')'");
			}

			if (invert) {
				e = (LitExpression*) lit_create_unary_expression(parser->state, condition->line, e, TOKEN_BANG);
			}

			lit_expressions_write(parser->state, elseif_conditions, e);

			lit_stataments_write(parser->state, elseif_branches, parse_statement(parser));

			continue;
		}

		// else
		if (else_branch != NULL) {
			error(parser, ERROR_MULTIPLE_ELSE_BRANCHES);
		}

		else_branch = parse_statement(parser);
	}

	return (LitStatement*) lit_create_if_statement(parser->state, line, condition, if_branch, else_branch, elseif_conditions, elseif_branches);
}

static LitStatement* parse_for(LitParser* parser) {
	uint line = parser->previous.line;
	bool had_paren = match(parser, TOKEN_LEFT_PAREN);

	LitStatement* var = NULL;
	LitExpression* init = NULL;

	if (!check(parser, TOKEN_SEMICOLON)) {
		if (match(parser, TOKEN_VAR)) {
			var = parse_var_declaration(parser);
		} else {
			init = parse_expression(parser);
		}
	}

	bool c_style = !match(parser, TOKEN_IN);
	LitExpression* condition = NULL;
	LitExpression* increment = NULL;

	if (c_style) {
		consume(parser, TOKEN_SEMICOLON, "';'");
		condition = check(parser, TOKEN_SEMICOLON) ? NULL : parse_expression(parser);

		consume(parser, TOKEN_SEMICOLON, "';'");
		increment = check(parser, TOKEN_RIGHT_PAREN) ? NULL : parse_expression(parser);
	} else {
		condition = parse_expression(parser);

		if (var == NULL) {
			error(parser, ERROR_VAR_MISSING_IN_FORIN);
		}
	}

	if (had_paren) {
		consume(parser, TOKEN_RIGHT_PAREN, "')'");
	}

	return (LitStatement*) lit_create_for_statement(parser->state, line, init, var, condition, increment, parse_statement(parser), c_style);
}

static LitStatement* parse_while(LitParser* parser) {
	uint line = parser->previous.line;

	bool had_paren = match(parser, TOKEN_LEFT_PAREN);
	LitExpression* condition = parse_expression(parser);

	if (had_paren) {
		consume(parser, TOKEN_RIGHT_PAREN, "')'");
	}

	LitStatement* body = parse_statement(parser);

	return (LitStatement*) lit_create_while_statement(parser->state, line, condition, body);
}

static LitStatement* parse_function(LitParser* parser) {
	bool export = parser->previous.type == TOKEN_EXPORT;

	if (export) {
		consume(parser, TOKEN_FUNCTION, "'function' after 'export'");
	}

	uint line = parser->previous.line;
	consume(parser, TOKEN_IDENTIFIER, "function name");

	const char* function_name = parser->previous.start;
	uint function_length = parser->previous.length;

	if (match(parser, TOKEN_DOT)) {
		consume(parser, TOKEN_IDENTIFIER, "function name");

		LitLambdaExpression* lambda = lit_create_lambda_expression(parser->state, line);
		LitSetExpression* to = lit_create_set_expression(parser->state, line, (LitExpression*) lit_create_var_expression(parser->state, line, function_name, function_length), parser->previous.start, parser->previous.length, (LitExpression*) lambda);

		consume(parser, TOKEN_LEFT_PAREN, "'(' after function name");

		LitCompiler compiler;
		init_compiler(parser, &compiler);
		begin_scope(parser);

		parse_parameters(parser, &lambda->parameters);

		if (lambda->parameters.count > 255) {
			error(parser, ERROR_TOO_MANY_FUNCTION_ARGS, (int) lambda->parameters.count);
		}

		consume(parser, TOKEN_RIGHT_PAREN, "')' after function arguments");
		lambda->body = parse_statement(parser);

		end_scope(parser);
		end_compiler(parser, &compiler);

		return (LitStatement*) lit_create_expression_statement(parser->state, line, (LitExpression*) to);
	}

	LitFunctionStatement* function = lit_create_function_statement(parser->state, line, function_name, function_length);
	function->export = export;

	consume(parser, TOKEN_LEFT_PAREN, "'(' after function name");

	LitCompiler compiler;
	init_compiler(parser, &compiler);
	begin_scope(parser);

	parse_parameters(parser, &function->parameters);

	if (function->parameters.count > 255) {
		error(parser, ERROR_TOO_MANY_FUNCTION_ARGS, (int) function->parameters.count);
	}

	consume(parser, TOKEN_RIGHT_PAREN, "')' after function arguments");

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
			error(parser, ERROR_NO_GETTER_AND_SETTER);
		}

		ignore_new_lines(parser);
		consume(parser, TOKEN_RIGHT_BRACE, "'}' after field declaration");
	}

	return (LitStatement*) lit_create_field_statement(parser->state, line, name, getter, setter, is_static);
}

static LitTokenType operators[] = {
	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_STAR,
	TOKEN_PERCENT,
	TOKEN_SLASH,
	TOKEN_SHARP,

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
			error(parser, ERROR_STATIC_OPERATOR);
		}

		uint i = 0;

		while (operators[i] != TOKEN_EOF) {
			if (match(parser, operators[i])) {
				break;
			}

			i++;
		}

		if (parser->previous.type == TOKEN_LEFT_BRACKET) {
			consume(parser, TOKEN_RIGHT_BRACKET, "']' after '[' in op method declaration");
			name = lit_copy_string(parser->state, "[]", 2);
		} else {
			name = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);
		}
	} else {
		consume(parser, TOKEN_IDENTIFIER, "method name");
		name = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);

		if (check(parser, TOKEN_LEFT_BRACE) || check(parser, TOKEN_ARROW)) {
			return parse_field(parser, name, is_static);
		}
	}

	LitMethodStatement* method = lit_create_method_statement(parser->state, parser->previous.line, name, is_static);

	LitCompiler compiler;
	init_compiler(parser, &compiler);
	begin_scope(parser);

	consume(parser, TOKEN_LEFT_PAREN, "'(' after method name");
	parse_parameters(parser, &method->parameters);

	if (method->parameters.count > 255) {
		error(parser, ERROR_TOO_MANY_FUNCTION_ARGS, (int) method->parameters.count);
	}

	consume(parser, TOKEN_RIGHT_PAREN, "')' after method arguments");

	method->body = parse_statement(parser);

	end_scope(parser);
	end_compiler(parser, &compiler);

	return (LitStatement*) method;
}

static LitStatement* parse_class(LitParser* parser) {
	if (setjmp(jump_buffer)) {
		return NULL;
	}

	uint line = parser->previous.line;

	bool is_static = parser->previous.type == TOKEN_STATIC;

	if (is_static) {
		consume(parser, TOKEN_CLASS, "'class' after 'static'");
	}

	consume(parser, TOKEN_IDENTIFIER, "class name after 'class'");
	LitString* name = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);
	LitString* super = NULL;

	if (match(parser, TOKEN_COLON)) {
		consume(parser, TOKEN_IDENTIFIER, "super class name after ':'");
		super = lit_copy_string(parser->state, parser->previous.start, parser->previous.length);

		if (super == name) {
			error(parser, ERROR_SELF_INHERITED_CLASS);
		}
	}

	LitClassStatement* klass = lit_create_class_statement(parser->state, line, name, super);

	ignore_new_lines(parser);
	consume(parser, TOKEN_LEFT_BRACE, "'{' before class body");
	ignore_new_lines(parser);

	bool finished_parsing_fields = false;

	while (!check(parser, TOKEN_RIGHT_BRACE)) {
		bool field_is_static = false;

		if (match(parser, TOKEN_STATIC)) {
			field_is_static = true;

			if (match(parser, TOKEN_VAR)) {
				if (finished_parsing_fields) {
					error(parser, ERROR_STATIC_FIELDS_AFTER_METHODS);
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

	consume(parser, TOKEN_RIGHT_BRACE, "'}' after class body");
	return (LitStatement*) klass;
}

static void sync(LitParser* parser) {
	parser->panic_mode = false;

	while (parser->current.type != TOKEN_EOF) {
		if (parser->previous.type == TOKEN_NEW_LINE) {
			longjmp(jump_buffer, 1);
			return;
		}

		switch (parser->current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUNCTION:
			case TOKEN_EXPORT:
			case TOKEN_VAR:
			case TOKEN_CONST:
			case TOKEN_FOR:
			case TOKEN_STATIC:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_RETURN: {
				longjmp(jump_buffer, 1);
				return;
			}

			default: {
				advance(parser);
			}
		}
	}
}

static LitStatement* parse_statement(LitParser* parser) {
	if (setjmp(jump_buffer)) {
		return NULL;
	}

	if (match(parser, TOKEN_VAR) || match(parser, TOKEN_CONST)) {
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

static LitStatement* parse_declaration(LitParser* parser) {
	LitStatement* statement = NULL;

	if (match(parser, TOKEN_CLASS) || match(parser, TOKEN_STATIC)) {
		statement = parse_class(parser);
	} else {
		statement = parse_statement(parser);
	}

	return statement;
}

bool lit_parse(LitParser* parser, const char* file_name, const char* source, LitStatements* statements) {
	parser->had_error = false;
	parser->panic_mode = false;

	lit_init_scanner(parser->state, parser->state->scanner, file_name, source);

	LitCompiler compiler;
	init_compiler(parser, &compiler);

	advance(parser);
	ignore_new_lines(parser);

	if (!is_at_end(parser)) {
		do {
			LitStatement* statement = parse_declaration(parser);

			if (statement != NULL) {
				lit_stataments_write(parser->state, statements, statement);
			}

			if (!match_new_line(parser)) {
				if (match(parser, TOKEN_EOF)) {
					break;
				}
			}
		} while (!is_at_end(parser));
	}

	return parser->had_error || parser->state->scanner->had_error;
}

static void setup_rules() {
	rules[TOKEN_LEFT_PAREN] = (LitParseRule) { parse_grouping_or_lambda, parse_call, PREC_CALL };
	rules[TOKEN_PLUS] = (LitParseRule) { NULL, parse_binary, PREC_TERM };
	rules[TOKEN_MINUS] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	rules[TOKEN_BANG] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_STAR_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_SLASH] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_SHARP] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	rules[TOKEN_BAR] = (LitParseRule) { NULL, parse_binary, PREC_BOR };
	rules[TOKEN_AMPERSAND] = (LitParseRule) { NULL, parse_binary, PREC_BAND };
	rules[TOKEN_TILDE] = (LitParseRule) { parse_unary, NULL, PREC_UNARY };
	rules[TOKEN_CARET] = (LitParseRule) { NULL, parse_binary, PREC_BOR };
	rules[TOKEN_LESS_LESS] = (LitParseRule) { NULL, parse_binary, PREC_SHIFT };
	rules[TOKEN_GREATER_GREATER] = (LitParseRule) { NULL, parse_binary, PREC_SHIFT };

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
	rules[TOKEN_STRING] = (LitParseRule) { parse_string, NULL, PREC_NONE };
	rules[TOKEN_INTERPOLATION] = (LitParseRule) { parse_interpolation, NULL, PREC_NONE };
	rules[TOKEN_IDENTIFIER] = (LitParseRule) { parse_variable_expression, NULL, PREC_NONE };
	rules[TOKEN_NEW] = (LitParseRule) { parse_new_expression, NULL, PREC_NONE };
	rules[TOKEN_PLUS_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_MINUS_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_STAR_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_SLASH_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_SHARP_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_PERCENT_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_CARET_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_BAR_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_AMPERSAND_EQUAL] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_PLUS_PLUS] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_MINUS_MINUS] = (LitParseRule) { NULL, parse_compound, PREC_COMPOUND };
	rules[TOKEN_AMPERSAND_AMPERSAND] = (LitParseRule) { NULL, parse_and, PREC_AND };
	rules[TOKEN_BAR_BAR] = (LitParseRule) { NULL, parse_or, PREC_AND };
	rules[TOKEN_QUESTION_QUESTION] = (LitParseRule) { NULL, parse_null_filter, PREC_NULL };
	rules[TOKEN_DOT] = (LitParseRule) { NULL, parse_dot, PREC_CALL };
	rules[TOKEN_SMALL_ARROW] = (LitParseRule) { NULL, parse_dot, PREC_CALL };
	rules[TOKEN_DOT_DOT] = (LitParseRule) { NULL, parse_range, PREC_RANGE };
	rules[TOKEN_DOT_DOT_DOT] = (LitParseRule) { parse_variable_expression, NULL, PREC_ASSIGNMENT };
	rules[TOKEN_LEFT_BRACKET] = (LitParseRule) { parse_array, parse_subscript, PREC_NONE };
	rules[TOKEN_LEFT_BRACE] = (LitParseRule) { parse_object, NULL, PREC_NONE };
	rules[TOKEN_THIS] = (LitParseRule) { parse_this, NULL, PREC_NONE };
	rules[TOKEN_SUPER] = (LitParseRule) { parse_super, NULL, PREC_NONE };
	rules[TOKEN_QUESTION] = (LitParseRule) { NULL, parse_ternary_or_question, PREC_EQUALITY };
	rules[TOKEN_REF] = (LitParseRule) { parse_reference, NULL, PREC_NONE };
}