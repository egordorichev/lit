#include <lit/optimizer/lit_optimizer.h>
#include <lit/lit.h>

void lit_init_optimizer(LitState* state, LitOptimizer* optimizer) {
	optimizer->state = state;
}

static LitValue evaluate_unary_op(LitValue value, LitTokenType operator) {
	switch (operator) {
		case TOKEN_MINUS: {
			if (IS_NUMBER(value)) {
				return NUMBER_VALUE(-AS_NUMBER(value));
			}

			break;
		}

		case TOKEN_BANG: {
			return BOOL_VALUE(lit_is_falsey(value));
		}

		case TOKEN_TILDE: {
			if (IS_NUMBER(value)) {
				return NUMBER_VALUE(~((int) AS_NUMBER(value)));
			}

			break;
		}

		default: {
			break;
		}
	}

	return NULL_VALUE;
}

static LitValue evaluate_binary_op(LitValue a, LitValue b, LitTokenType operator) {
	#define BINARY_OP(op) \
		if (IS_NUMBER(a) && IS_NUMBER(b)) { \
			return NUMBER_VALUE(AS_NUMBER(a) op AS_NUMBER(b)); \
	  } \
		return NULL_VALUE;

	switch (operator) {
		case TOKEN_PLUS: BINARY_OP(+)
		case TOKEN_MINUS: BINARY_OP(-)
		case TOKEN_STAR: BINARY_OP(*)
		case TOKEN_SLASH: BINARY_OP(/)

		case TOKEN_STAR_STAR: {
			break;
		}

		case TOKEN_SHARP: {
			break;
		}

		case TOKEN_PERCENT: {
			break;
		}

		case TOKEN_IS: {
			break;
		}

		case TOKEN_EQUAL_EQUAL: {
			break;
		}

		case TOKEN_BANG_EQUAL: {
			break;
		}

		case TOKEN_GREATER: {
			break;
		}

		case TOKEN_GREATER_EQUAL: {
			break;
		}

		case TOKEN_LESS: {
			break;
		}

		case TOKEN_LESS_EQUAL: {
			break;
		}

		case TOKEN_LESS_LESS: {
			break;
		}

		case TOKEN_GREATER_GREATER: {
			break;
		}

		case TOKEN_BAR: {
			break;
		}

		case TOKEN_AMPERSAND: {
			break;
		}

		case TOKEN_CARET: {
			break;
		}

		default: {
			return NULL_VALUE;
		}
	}

	#undef BINARY_OP
}

static LitValue evaluate_expression(LitOptimizer* optimizer, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			return ((LitLiteralExpression*) expression)->value;
		}

		case UNARY_EXPRESSION: {
			LitUnaryExpression* expr = (LitUnaryExpression*) expression;
			LitValue branch = evaluate_expression(optimizer, expr->right);

			if (branch != NULL_VALUE) {
				return evaluate_unary_op(branch, expr->operator);
			}

			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			LitValue a = evaluate_expression(optimizer, expr->left);
			LitValue b = evaluate_expression(optimizer, expr->right);

			if (a != NULL_VALUE && b != NULL_VALUE) {
				return evaluate_binary_op(a, b, expr->operator);
			}

			break;
		}

		default: {
			return NULL_VALUE;
		}
	}

	return NULL_VALUE;
}

static void optimize_expression(LitOptimizer* optimizer, LitExpression** slot) {
	LitExpression* expression = *slot;

	if (expression == NULL) {
		return;
	}

	LitState* state = optimizer->state;

	switch (expression->type) {
		case UNARY_EXPRESSION:
		case GROUPING_EXPRESSION:
		case BINARY_EXPRESSION: {
			LitValue optimized = evaluate_expression(optimizer, expression);

			if (optimized != NULL_VALUE) {
				*slot = (LitExpression *) lit_create_literal_expression(state, expression->line, optimized);
				lit_free_expression(state, expression);
			}

			break;
		}

		case LITERAL_EXPRESSION:
			break;
			break;
		case VAR_EXPRESSION:
			break;
		case ASSIGN_EXPRESSION:
			break;
		case CALL_EXPRESSION:
			break;
		case SET_EXPRESSION:
			break;
		case GET_EXPRESSION:
			break;
		case LAMBDA_EXPRESSION:
			break;
		case ARRAY_EXPRESSION:
			break;
		case MAP_EXPRESSION:
			break;
		case SUBSCRIPT_EXPRESSION:
			break;
		case THIS_EXPRESSION:
			break;
		case SUPER_EXPRESSION:
			break;
		case RANGE_EXPRESSION:
			break;
		case IF_EXPRESSION:
			break;
		case INTERPOLATION_EXPRESSION:
			break;
	}
}

static void optimize_statement(LitOptimizer* optimizer, LitStatement* statement) {
	if (statement == NULL) {
		return;
	}

	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			optimize_expression(optimizer, &((LitExpressionStatement*) statement)->expression);
			break;
		}

		case BLOCK_STATEMENT: {
			break;
		}

		case IF_STATEMENT: {
			break;
		}

		case WHILE_STATEMENT: {
			break;
		}

		case FOR_STATEMENT: {
			break;
		}

		case VAR_STATEMENT: {
			optimize_expression(optimizer, &((LitVarStatement *) statement)->init);
			break;
		}

		case FUNCTION_STATEMENT: {
			break;
		}

		case RETURN_STATEMENT: {
			break;
		}

		case METHOD_STATEMENT: {
			break;
		}

		case CLASS_STATEMENT: {
			break;
		}

		case FIELD_STATEMENT: {
			break;
		}

		case CONTINUE_STATEMENT:
		case BREAK_STATEMENT: {
			break;
		}
	}
}

static void walk_statements(LitOptimizer* optimizer, LitStatements* statements) {
	for (uint i = 0; i < statements->count; i++) {
		optimize_statement(optimizer, statements->values[i]);
	}
}

void lit_optimize(LitOptimizer* optimizer, LitStatements* statements) {
	walk_statements(optimizer, statements);
}