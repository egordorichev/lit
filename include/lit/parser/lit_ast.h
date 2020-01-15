#ifndef LIT_AST_H
#define LIT_AST_H

#include <lit/lit_common.h>
#include <lit/vm/lit_value.h>
#include <lit/scanner/lit_token.h>

/*
 * Expressions
 */

typedef enum {
	LITERAL_EXPRESSION,
	BINARY_EXPRESSION,
	UNARY_EXPRESSION,
	CALL_EXPRESSION,
	GROUPING_EXPRESSION,
	VAR_EXPRESSION,
	ASSIGN_EXPRESSION,
	LOGICAL_EXPRESSION,
	GET_EXPRESSION,
	SET_EXPRESSION,
	THIS_EXPRESSION,
	SUPER_EXPRESSION
} LitExpressionType;

typedef struct LitExpression {
	LitExpressionType type;
	uint line;
} LitExpression;

DECLARE_ARRAY(LitExpressions, LitExpression*, expressions)
void lit_free_expression(LitState* state, LitExpression* expression);

typedef struct {
	LitExpression expression;
	LitValue value;
} LitLiteralExpression;

LitLiteralExpression *lit_create_literal_expression(LitState* state, uint line, LitValue value);

typedef struct {
	LitExpression expression;

	LitExpression* left;
	LitExpression* right;
	LitTokenType operator;
} LitBinaryExpression;

LitBinaryExpression *lit_create_binary_expression(LitState* state, uint line, LitExpression* left, LitExpression* right, LitTokenType operator);

typedef struct {
	LitExpression expression;

	LitExpression* right;
	LitTokenType operator;
} LitUnaryExpression;

LitUnaryExpression *lit_create_unary_expression(LitState* state, uint line, LitExpression* right, LitTokenType operator);

/*
 * Statements
 */

typedef enum {
	EXPRESSION_STATEMENT,
	BLOCK_STATEMENT,
	IF_STATEMENT,
	WHILE_STATEMENT,
	FOR_STATEMENT,
	CLASS_STATEMENT,
	VAR_STATEMENT
} LitStatementType;

typedef struct LitStatement {
	LitStatementType type;
	uint line;
} LitStatement;

DECLARE_ARRAY(LitStatements, LitStatement*, stataments)
void lit_free_statement(LitState* state, LitStatement* statement);

typedef struct {
	LitStatement statement;
	LitExpression* expression;
} LitExpressionStatement;

LitExpressionStatement *lit_create_expression_statement(LitState* state, uint line, LitExpression* expression);

#endif