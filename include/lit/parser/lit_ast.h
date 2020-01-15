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

#endif