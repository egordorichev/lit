#include <lit/parser/lit_ast.h>
#include <lit/mem/lit_mem.h>

DEFINE_ARRAY(LitExpressions, LitExpression*, expressions)
DEFINE_ARRAY(LitStatements, LitStatement*, stataments)

#define FREE_EXPRESSION(type) lit_reallocate(state, expression, sizeof(type), 0);

void lit_free_expression(LitState* state, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL: {
			FREE_EXPRESSION(LitLiteralExpression)
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
	LitLiteralExpression* expression = ALLOCATE_EXPRESSION(state, LitLiteralExpression, LITERAL);
	expression->value = value;
	return expression;
}

#define FREE_STATEMENT(type) lit_reallocate(state, statement, sizeof(type), 0);

void lit_free_statement(LitState* state, LitStatement* statement) {
	switch (statement->type) {
		case EXPRESSION: {
			lit_free_expression(state, ((LitExpressionStatement*) statement)->expression);
			FREE_STATEMENT(LitExpressionStatement)
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
	LitExpressionStatement* statement = ALLOCATE_STATEMENT(state, LitExpressionStatement, EXPRESSION);
	statement->expression = expression;
	return statement;
}
