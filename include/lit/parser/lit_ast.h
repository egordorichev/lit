#ifndef LIT_AST_H
#define LIT_AST_H

#include <lit/lit_common.h>
#include <lit/vm/lit_value.h>
#include <lit/scanner/lit_token.h>

typedef enum {
	LITERAL_EXPRESSION,
	BINARY_EXPRESSION,
	UNARY_EXPRESSION,
	GROUPING_EXPRESSION,
	VAR_EXPRESSION,
	ASSIGN_EXPRESSION,
	CALL_EXPRESSION,
	REQUIRE_EXPRESSION,
	SET_EXPRESSION,
	GET_EXPRESSION,
	LAMBDA_EXPRESSION,
	ARRAY_EXPRESSION,
	MAP_EXPRESSION,
	SUBSCRIPT_EXPRESSION,
	THIS_EXPRESSION,
	SUPER_EXPRESSION,
	RANGE_EXPRESSION
} LitExpressionType;

typedef struct LitExpression {
	LitExpressionType type;
	uint line;
} LitExpression;

typedef enum {
	EXPRESSION_STATEMENT,
	BLOCK_STATEMENT,
	IF_STATEMENT,
	WHILE_STATEMENT,
	FOR_STATEMENT,
	VAR_STATEMENT,
	CONTINUE_STATEMENT,
	BREAK_STATEMENT,
	FUNCTION_STATEMENT,
	RETURN_STATEMENT,
	METHOD_STATEMENT,
	CLASS_STATEMENT
} LitStatementType;

typedef struct LitStatement {
	LitStatementType type;
	uint line;
} LitStatement;

/*
 * Expressions
 */

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

	bool ignore_left;
} LitBinaryExpression;

LitBinaryExpression *lit_create_binary_expression(LitState* state, uint line, LitExpression* left, LitExpression* right, LitTokenType operator);

typedef struct {
	LitExpression expression;

	LitExpression* right;
	LitTokenType operator;
} LitUnaryExpression;

LitUnaryExpression *lit_create_unary_expression(LitState* state, uint line, LitExpression* right, LitTokenType operator);

typedef struct {
	LitExpression expression;
	const char* name;
	uint length;
} LitVarExpression;

LitVarExpression *lit_create_var_expression(LitState* state, uint line, LitToken name);

typedef struct {
	LitExpression expression;
	LitExpression* child;
} LitGroupingExpression;

LitGroupingExpression *lit_create_grouping_expression(LitState* state, uint line, LitExpression* child);

typedef struct {
	LitExpression expression;

	LitExpression* to;
	LitExpression* value;
} LitAssignExpression;

LitAssignExpression *lit_create_assign_expression(LitState* state, uint line, LitExpression* to, LitExpression* value);

typedef struct {
	LitExpression expression;

	LitExpression* callee;
	LitExpressions args;
} LitCallExpression;

LitCallExpression *lit_create_call_expression(LitState* state, uint line, LitExpression* callee);

typedef struct {
	LitExpression expression;
	LitExpression* argument;
} LitRequireExpression;

LitRequireExpression *lit_create_require_expression(LitState* state, uint line, LitExpression* expression);

typedef struct {
	LitExpression expression;

	LitExpression* where;
	const char* name;
	uint length;

	bool ignore_emit;
	bool questionable;
} LitGetExpression;

LitGetExpression *lit_create_get_expression(LitState* state, uint line, LitExpression* where, const char* name, uint length, bool questionable);

typedef struct {
	LitExpression expression;

	LitExpression* where;
	const char* name;
	uint length;

	LitExpression* value;
} LitSetExpression;

LitSetExpression *lit_create_set_expression(LitState* state, uint line, LitExpression* where, const char* name, uint length, LitExpression* value);

typedef struct {
	const char* name;
	uint length;
} LitParameter;

DECLARE_ARRAY(LitParameters, LitParameter, parameters);

typedef struct {
	LitExpression expression;

	LitParameters parameters;
	LitStatement* body;
} LitLambdaExpression;

LitLambdaExpression *lit_create_lambda_expression(LitState* state, uint line);

typedef struct {
	LitExpression expression;
	LitExpressions values;
} LitArrayExpression;

LitArrayExpression *lit_create_array_expression(LitState* state, uint line);

typedef struct {
	LitExpression expression;

	LitValues keys;
	LitExpressions values;
} LitMapExpression;

LitMapExpression *lit_create_map_expression(LitState* state, uint line);

typedef struct {
	LitExpression expression;

	LitExpression* array;
	LitExpression* index;
} LitSubscriptExpression;

LitSubscriptExpression *lit_create_subscript_expression(LitState* state, uint line, LitExpression* array, LitExpression* index);

typedef struct {
	LitExpression expression;
} LitThisExpression;

LitThisExpression *lit_create_this_expression(LitState* state, uint line);

typedef struct {
	LitExpression expression;

	LitString* method;
	bool ignore_emit;
} LitSuperExpression;

LitSuperExpression *lit_create_super_expression(LitState* state, uint line, LitString* method);

typedef struct {
	LitExpression expression;

	LitExpression* from;
	LitExpression* to;
} LitRangeExpression;

LitRangeExpression *lit_create_range_expression(LitState* state, uint line, LitExpression* from, LitExpression* to);

/*
 * Statements
 */

DECLARE_ARRAY(LitStatements, LitStatement*, stataments)
void lit_free_statement(LitState* state, LitStatement* statement);

typedef struct {
	LitStatement statement;
	LitExpression* expression;
	bool pop;
} LitExpressionStatement;

LitExpressionStatement *lit_create_expression_statement(LitState* state, uint line, LitExpression* expression);

typedef struct {
	LitStatement statement;
	LitStatements statements;
} LitBlockStatement;

LitBlockStatement *lit_create_block_statement(LitState* state, uint line);

typedef struct {
	LitStatement statement;
	const char* name;
	uint length;
	LitExpression* init;
} LitVarStatement;

LitVarStatement *lit_create_var_statement(LitState* state, uint line, const char* name, uint length, LitExpression* init);

typedef struct {
	LitStatement statement;

	LitExpression* condition;
	LitStatement* if_branch;
	LitStatement* else_branch;

	LitExpressions* elseif_conditions;
	LitStatements* elseif_branches;
} LitIfStatement;

LitIfStatement *lit_create_if_statement(LitState* state, uint line, LitExpression* condition, LitStatement* if_branch, LitStatement* else_branch, LitExpressions* elseif_conditions, LitStatements* elseif_branches);

typedef struct {
	LitStatement statement;

	LitExpression* condition;
	LitStatement* body;
} LitWhileStatement;

LitWhileStatement *lit_create_while_statement(LitState* state, uint line, LitExpression* condition, LitStatement* body);

typedef struct {
	LitStatement statement;

	LitExpression* init;
	LitStatement* var;

	LitExpression* condition;
	LitExpression* increment;
	LitStatement* body;
} LitForStatement;

LitForStatement *lit_create_for_statement(LitState* state, uint line, LitExpression* init, LitStatement* var, LitExpression* condition, LitExpression* increment, LitStatement* body);

typedef struct {
	LitStatement statement;
} LitContinueStatement;

LitContinueStatement *lit_create_continue_statement(LitState* state, uint line);

typedef struct {
	LitStatement statement;
} LitBreakStatement;

LitBreakStatement *lit_create_break_statement(LitState* state, uint line);

typedef struct {
	LitStatement statement;

	const char* name;
	uint length;

	LitParameters parameters;
	LitStatement* body;

	bool export;
} LitFunctionStatement;

LitFunctionStatement *lit_create_function_statement(LitState* state, uint line, const char* name, uint length);

typedef struct {
	LitStatement statement;
	LitExpression* expression;
} LitReturnStatement;

LitReturnStatement *lit_create_return_statement(LitState* state, uint line, LitExpression* expression);

typedef struct {
	LitStatement statement;

	LitString* name;
	LitParameters parameters;
	LitStatement* body;

	bool is_static;
} LitMethodStatement;

LitMethodStatement *lit_create_method_statement(LitState* state, uint line, LitString* name, bool is_static);

typedef struct {
	LitStatement statement;

	LitString* name;
	LitString* parent;

	LitStatements fields;
} LitClassStatement;

LitClassStatement *lit_create_class_statement(LitState* state, uint line, LitString* name, LitString* parent);

LitExpressions* lit_allocate_expressions(LitState* state);
void lit_free_allocated_expressions(LitState* state, LitExpressions* expressions);

LitStatements* lit_allocate_statements(LitState* state);
void lit_free_allocated_statements(LitState* state, LitStatements* statements);

#endif