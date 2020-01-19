#include <lit/parser/lit_ast.h>
#include <lit/mem/lit_mem.h>

DEFINE_ARRAY(LitExpressions, LitExpression*, expressions)
DEFINE_ARRAY(LitStatements, LitStatement*, stataments)

#define FREE_EXPRESSION(type) lit_reallocate(state, expression, sizeof(type), 0);

void lit_free_expression(LitState* state, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			FREE_EXPRESSION(LitLiteralExpression)
			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			lit_free_expression(state, expr->left);
			lit_free_expression(state, expr->right);

			FREE_EXPRESSION(LitBinaryExpression)
			break;
		}

		case UNARY_EXPRESSION: {
			lit_free_expression(state, ((LitUnaryExpression*) expression)->right);
			FREE_EXPRESSION(LitUnaryExpression)

			break;
		}

		case GROUPING_EXPRESSION: {
			lit_free_expression(state, ((LitGroupingExpression*) expression)->child);
			FREE_EXPRESSION(LitGroupingExpression)

			break;
		}

		case VAR_EXPRESSION: {
			FREE_EXPRESSION(LitVarExpression)
			break;
		}

		default: {
			lit_error(state, COMPILE_ERROR, 0, "Unknown expression type %d", (int) expression->type);
			break;
		}
	}
}

#define ALLOCATE_EXPRESSION(state, type, object_type) \
    (type*) allocate_expression(state, line, sizeof(type), object_type)

static LitExpression* allocate_expression(LitState* state, uint64_t line, size_t size, LitExpressionType type) {
	LitExpression* object = (LitExpression*) lit_reallocate(state, NULL, 0, size);

	object->type = type;
	object->line = line;

	return object;
}

LitLiteralExpression *lit_create_literal_expression(LitState* state, uint line, LitValue value) {
	LitLiteralExpression* expression = ALLOCATE_EXPRESSION(state, LitLiteralExpression, LITERAL_EXPRESSION);
	expression->value = value;
	return expression;
}

LitBinaryExpression *lit_create_binary_expression(LitState* state, uint line, LitExpression* left, LitExpression* right, LitTokenType operator) {
	LitBinaryExpression* expression = ALLOCATE_EXPRESSION(state, LitBinaryExpression, BINARY_EXPRESSION);

	expression->left = left;
	expression->right = right;
	expression->operator = operator;

	return expression;
}

LitUnaryExpression *lit_create_unary_expression(LitState* state, uint line, LitExpression* right, LitTokenType operator) {
	LitUnaryExpression* expression = ALLOCATE_EXPRESSION(state, LitUnaryExpression, UNARY_EXPRESSION);

	expression->right = right;
	expression->operator = operator;

	return expression;
}

LitGroupingExpression *lit_create_grouping_expression(LitState* state, uint line, LitExpression* child) {
	LitGroupingExpression* expression = ALLOCATE_EXPRESSION(state, LitGroupingExpression, GROUPING_EXPRESSION);
	expression->child = child;
	return expression;
}

LitVarExpression *lit_create_var_expression(LitState* state, uint line, LitString* name) {
	LitVarExpression* expression = ALLOCATE_EXPRESSION(state, LitVarExpression, VAR_EXPRESSION);
	expression->name = name;
	return expression;
}

#define FREE_STATEMENT(type) lit_reallocate(state, statement, sizeof(type), 0);

void lit_free_statement(LitState* state, LitStatement* statement) {
	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			lit_free_expression(state, ((LitExpressionStatement*) statement)->expression);
			FREE_STATEMENT(LitExpressionStatement)
			break;
		}

		case PRINT_STATEMENT: {
			lit_free_expression(state, ((LitPrintStatement*) statement)->expression);
			FREE_STATEMENT(LitPrintStatement)
			break;
		}

		case VAR_STATEMENT: {
			lit_free_expression(state, ((LitVarStatement*) statement)->init);
			FREE_STATEMENT(LitVarStatement)
			break;
		}

		default: {
			lit_error(state, COMPILE_ERROR, 0, "Unknown statement type %d", (int) statement->type);
			break;
		}
	}
}

#define ALLOCATE_STATEMENT(state, type, object_type) \
    (type*) allocate_statement(state, line, sizeof(type), object_type)

static LitStatement* allocate_statement(LitState* state, uint64_t line, size_t size, LitStatementType type) {
	LitStatement* object = (LitStatement*) lit_reallocate(state, NULL, 0, size);

	object->type = type;
	object->line = line;

	return object;
}

LitExpressionStatement *lit_create_expression_statement(LitState* state, uint line, LitExpression* expression) {
	LitExpressionStatement* statement = ALLOCATE_STATEMENT(state, LitExpressionStatement, EXPRESSION_STATEMENT);
	statement->expression = expression;
	return statement;
}

LitPrintStatement *lit_create_print_statement(LitState* state, uint line, LitExpression* expression) {
	LitPrintStatement* statement = ALLOCATE_STATEMENT(state, LitPrintStatement, PRINT_STATEMENT);
	statement->expression = expression;
	return statement;
}

LitVarStatement *lit_create_var_statement(LitState* state, uint line, LitString* name, LitExpression* init) {
	LitVarStatement* statement = ALLOCATE_STATEMENT(state, LitVarStatement, VAR_STATEMENT);

	statement->name = name;
	statement->init = init;

	return statement;

}