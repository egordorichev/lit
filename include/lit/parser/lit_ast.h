#ifndef LIT_AST_H
#define LIT_AST_H

#include <lit/lit_common.h>
#include <lit/vm/lit_value.h>

typedef enum {
	LITERAL
} LitExpressionType;

typedef struct {
	LitExpressionType type;
	uint line;
} LitExpression;

DECLARE_ARRAY(LitExpressions, LitExpression, expressions)
void lit_free_expression(LitState* state, LitExpression* expression);

typedef struct {
	LitExpression expression;
	LitValue value;
} LitLiteralExpression;

LitLiteralExpression *lit_create_literal_expression(LitState* state, uint line, LitValue value);

typedef enum {
	EXPRESSION
} LitStatementType;

typedef struct {
	LitStatementType type;
	uint line;
} LitStatement;

DECLARE_ARRAY(LitStatements, LitStatement, stataments)
void lit_free_statement(LitState* state, LitStatement* statement);

typedef struct {
	LitStatement statement;
	LitExpression* expression;
} LitExpressionStatement;

LitExpressionStatement *lit_create_expression_statement(LitState* state, uint line, LitExpression* expression);

#endif