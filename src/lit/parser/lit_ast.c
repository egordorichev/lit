#include <lit/parser/lit_ast.h>
#include <lit/parser/lit_parser.h>
#include <lit/mem/lit_mem.h>
#include <lit/state/lit_state.h>

DEFINE_ARRAY(LitExpressions, LitExpression*, expressions)
DEFINE_ARRAY(LitStatements, LitStatement*, stataments)
DEFINE_ARRAY(LitParameters, LitParameter, parameters);

#define FREE_EXPRESSION(type) lit_reallocate(state, expression, sizeof(type), 0);

void free_expressions(LitState* state, LitExpressions* expressions) {
	if (expressions == NULL) {
		return;
	}

	for (uint i = 0; i < expressions->count; i++) {
		lit_free_expression(state, expressions->values[i]);
	}

	lit_free_expressions(state, expressions);
}

void free_statements(LitState* state, LitStatements* statements) {
	if (statements == NULL) {
		return;
	}

	for (uint i = 0; i < statements->count; i++) {
		lit_free_statement(state, statements->values[i]);
	}

	lit_free_stataments(state, statements);
}

void lit_free_expression(LitState* state, LitExpression* expression) {
	if (expression == NULL) {
		return;
	}

	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			FREE_EXPRESSION(LitLiteralExpression)
			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			if (!expr->ignore_left) {
				lit_free_expression(state, expr->left);
			}

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

		case ASSIGN_EXPRESSION: {
			LitAssignExpression *expr = (LitAssignExpression *) expression;

			lit_free_expression(state, expr->to);
			lit_free_expression(state, expr->value);

			FREE_EXPRESSION(LitAssignExpression)
			break;
		}

		case CALL_EXPRESSION: {
			LitCallExpression* expr = (LitCallExpression*) expression;
			lit_free_expression(state, expr->callee);
			free_expressions(state, &expr->args);

			FREE_EXPRESSION(LitCallExpression)
			break;
		}

		case REQUIRE_EXPRESSION: {
			lit_free_expression(state, ((LitRequireExpression*) expression)->argument);
			FREE_EXPRESSION(LitRequireExpression)
			break;
		}

		case GET_EXPRESSION: {
			lit_free_expression(state, ((LitGetExpression*) expression)->where);
			FREE_EXPRESSION(LitGetExpression)
			break;
		}

		case SET_EXPRESSION: {
			LitSetExpression* expr = (LitSetExpression*) expression;

			lit_free_expression(state, expr->where);
			lit_free_expression(state, expr->value);

			FREE_EXPRESSION(LitSetExpression)
			break;
		}

		case LAMBDA_EXPRESSION: {
			LitLambdaExpression* expr = (LitLambdaExpression*) expression;

			lit_free_parameters(state, &expr->parameters);
			lit_free_statement(state, expr->body);

			FREE_EXPRESSION(LitLambdaExpression)
			break;
		}

		case ARRAY_EXPRESSION: {
			free_expressions(state, &((LitArrayExpression*) expression)->values);
			FREE_EXPRESSION(LitArrayExpression)

			break;
		}

		case MAP_EXPRESSION: {
			LitMapExpression* map = (LitMapExpression*) expression;

			lit_free_values(state, &map->keys);
			free_expressions(state, &map->values);

			FREE_EXPRESSION(LitMapExpression)
			break;
		}

		case SUBSCRIPT_EXPRESSION: {
			LitSubscriptExpression* expr = (LitSubscriptExpression*) expression;

			lit_free_expression(state, expr->array);
			lit_free_expression(state, expr->index);

			FREE_EXPRESSION(LitSubscriptExpression)
			break;
		}

		case THIS_EXPRESSION: {
			FREE_EXPRESSION(LitThisExpression)
			break;
		}

		case SUPER_EXPRESSION: {
			FREE_EXPRESSION(LitSuperExpression)
			break;
		}

		case RANGE_EXPRESSION: {
			LitRangeExpression* expr = (LitRangeExpression*) expression;

			lit_free_expression(state, expr->from);
			lit_free_expression(state, expr->to);

			FREE_EXPRESSION(LitRangeExpression)
			break;
		}

		case IF_EXPRESSION: {
			LitIfExpression* expr = (LitIfExpression*) expression;

			lit_free_expression(state, expr->condition);
			lit_free_expression(state, expr->if_branch);
			lit_free_expression(state, expr->else_branch);

			FREE_EXPRESSION(LitIfExpression)
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
	expression->ignore_left = false;

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

LitVarExpression *lit_create_var_expression(LitState* state, uint line, LitToken name) {
	LitVarExpression* expression = ALLOCATE_EXPRESSION(state, LitVarExpression, VAR_EXPRESSION);

	expression->name = name.start;
	expression->length = name.length;

	return expression;
}

LitAssignExpression *lit_create_assign_expression(LitState* state, uint line, LitExpression* to, LitExpression* value) {
	LitAssignExpression* expression = ALLOCATE_EXPRESSION(state, LitAssignExpression, ASSIGN_EXPRESSION);

	expression->to = to;
	expression->value = value;

	return expression;
}

LitCallExpression *lit_create_call_expression(LitState* state, uint line, LitExpression* callee) {
	LitCallExpression* expression = ALLOCATE_EXPRESSION(state, LitCallExpression, CALL_EXPRESSION);

	expression->callee = callee;
	lit_init_expressions(&expression->args);

	return expression;
}

LitRequireExpression *lit_create_require_expression(LitState* state, uint line, LitExpression* expression) {
	LitRequireExpression* statement = ALLOCATE_EXPRESSION(state, LitRequireExpression, REQUIRE_EXPRESSION);
	statement->argument = expression;

	return statement;
}

LitGetExpression *lit_create_get_expression(LitState* state, uint line, LitExpression* where, const char* name, uint length, bool questionable) {
	LitGetExpression* expression = ALLOCATE_EXPRESSION(state, LitGetExpression, GET_EXPRESSION);

	expression->where = where;
	expression->name = name;
	expression->length = length;
	expression->ignore_emit = false;
	expression->questionable = questionable;

	return expression;
}

LitSetExpression *lit_create_set_expression(LitState* state, uint line, LitExpression* where, const char* name, uint length, LitExpression* value) {
	LitSetExpression* expression = ALLOCATE_EXPRESSION(state, LitSetExpression, SET_EXPRESSION);

	expression->where = where;
	expression->name = name;
	expression->length = length;
	expression->value = value;

	return expression;
}

LitLambdaExpression *lit_create_lambda_expression(LitState* state, uint line) {
	LitLambdaExpression* expression = ALLOCATE_EXPRESSION(state, LitLambdaExpression, LAMBDA_EXPRESSION);

	expression->body = NULL;
	lit_init_parameters(&expression->parameters);

	return expression;
}

LitArrayExpression *lit_create_array_expression(LitState* state, uint line) {
	LitArrayExpression* expression = ALLOCATE_EXPRESSION(state, LitArrayExpression, ARRAY_EXPRESSION);
	lit_init_expressions(&expression->values);
	return expression;
}

LitMapExpression *lit_create_map_expression(LitState* state, uint line) {
	LitMapExpression* expression = ALLOCATE_EXPRESSION(state, LitMapExpression, MAP_EXPRESSION);

	lit_init_values(&expression->keys);
	lit_init_expressions(&expression->values);

	return expression;
}

LitSubscriptExpression *lit_create_subscript_expression(LitState* state, uint line, LitExpression* array, LitExpression* index) {
	LitSubscriptExpression* expression = ALLOCATE_EXPRESSION(state, LitSubscriptExpression, SUBSCRIPT_EXPRESSION);

	expression->array = array;
	expression->index = index;

	return expression;
}

LitThisExpression *lit_create_this_expression(LitState* state, uint line) {
	return ALLOCATE_EXPRESSION(state, LitThisExpression, THIS_EXPRESSION);
}

LitSuperExpression *lit_create_super_expression(LitState* state, uint line, LitString* method) {
	LitSuperExpression* expression = ALLOCATE_EXPRESSION(state, LitSuperExpression, SUPER_EXPRESSION);

	expression->method = method;
	expression->ignore_emit = false;

	return expression;
}

LitRangeExpression *lit_create_range_expression(LitState* state, uint line, LitExpression* from, LitExpression* to) {
	LitRangeExpression* expression = ALLOCATE_EXPRESSION(state, LitRangeExpression, RANGE_EXPRESSION);

	expression->from = from;
	expression->to = to;

	return expression;
}

LitIfExpression *lit_create_if_experssion(LitState* state, uint line, LitExpression* condition, LitExpression* if_branch, LitExpression* else_branch) {
	LitIfExpression* expression = ALLOCATE_EXPRESSION(state, LitIfExpression, IF_EXPRESSION);

	expression->condition = condition;
	expression->if_branch = if_branch;
	expression->else_branch = else_branch;

	return expression;
}

#define FREE_STATEMENT(type) lit_reallocate(state, statement, sizeof(type), 0);

void lit_free_statement(LitState* state, LitStatement* statement) {
	if (statement == NULL) {
		return;
	}

	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			lit_free_expression(state, ((LitExpressionStatement*) statement)->expression);
			FREE_STATEMENT(LitExpressionStatement)
			break;
		}

		case BLOCK_STATEMENT: {
			free_statements(state, &((LitBlockStatement*) statement)->statements);
			FREE_STATEMENT(LitBlockStatement)
			break;
		}

		case VAR_STATEMENT: {
			lit_free_expression(state, ((LitVarStatement*) statement)->init);
			FREE_STATEMENT(LitVarStatement)
			break;
		}

		case IF_STATEMENT: {
			LitIfStatement* stmt = (LitIfStatement*) statement;

			lit_free_expression(state, stmt->condition);
			lit_free_statement(state, stmt->if_branch);

			lit_free_allocated_expressions(state, stmt->elseif_conditions);
			lit_free_allocated_statements(state, stmt->elseif_branches);

			lit_free_statement(state, stmt->else_branch);

			FREE_STATEMENT(LitIfStatement)
			break;
		}

		case WHILE_STATEMENT: {
			LitWhileStatement* stmt = (LitWhileStatement*) statement;

			lit_free_expression(state, stmt->condition);
			lit_free_statement(state, stmt->body);

			FREE_STATEMENT(LitWhileStatement)
			break;
		}

		case FOR_STATEMENT: {
			LitForStatement* stmt = (LitForStatement*) statement;

			lit_free_expression(state, stmt->increment);
			lit_free_expression(state, stmt->condition);
			lit_free_expression(state, stmt->init);

			lit_free_statement(state, stmt->var);
			lit_free_statement(state, stmt->body);

			FREE_STATEMENT(LitForStatement)
			break;
		}

		case CONTINUE_STATEMENT: {
			FREE_STATEMENT(LitContinueStatement)
			break;
		}

		case BREAK_STATEMENT: {
			FREE_STATEMENT(LitBreakStatement)
			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;

			lit_free_statement(state, stmt->body);
			lit_free_parameters(state, &stmt->parameters);

			FREE_STATEMENT(LitFunctionStatement)
			break;
		}

		case RETURN_STATEMENT: {
			lit_free_expression(state, ((LitReturnStatement*) statement)->expression);
			FREE_STATEMENT(LitReturnStatement)

			break;
		}

		case METHOD_STATEMENT: {
			LitMethodStatement* stmt = (LitMethodStatement*) statement;

			lit_free_parameters(state, &stmt->parameters);
			lit_free_statement(state, stmt->body);

			FREE_STATEMENT(LitMethodStatement)

			break;
		}

		case CLASS_STATEMENT: {
			free_statements(state, &((LitClassStatement*) statement)->fields);
			FREE_STATEMENT(LitClassStatement)
			break;
		}

		case FIELD_STATEMENT: {
			LitFieldStatement* stmt = (LitFieldStatement*) statement;

			lit_free_statement(state, stmt->getter);
			lit_free_statement(state, stmt->setter);

			FREE_STATEMENT(LitFieldStatement)
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
	statement->pop = true;

	return statement;
}

LitBlockStatement *lit_create_block_statement(LitState* state, uint line) {
	LitBlockStatement* statement = ALLOCATE_STATEMENT(state, LitBlockStatement, BLOCK_STATEMENT);
	lit_init_stataments(&statement->statements);
	return statement;
}

LitVarStatement *lit_create_var_statement(LitState* state, uint line, const char* name, uint length, LitExpression* init) {
	LitVarStatement* statement = ALLOCATE_STATEMENT(state, LitVarStatement, VAR_STATEMENT);

	statement->name = name;
	statement->length = length;
	statement->init = init;

	return statement;
}

LitIfStatement *lit_create_if_statement(LitState* state, uint line, LitExpression* condition, LitStatement* if_branch, LitStatement* else_branch, LitExpressions* elseif_conditions, LitStatements* elseif_branches) {
	LitIfStatement* statement = ALLOCATE_STATEMENT(state, LitIfStatement, IF_STATEMENT);

	statement->condition = condition;
	statement->if_branch = if_branch;
	statement->else_branch = else_branch;
	statement->elseif_conditions = elseif_conditions;
	statement->elseif_branches = elseif_branches;

	return statement;
}

LitWhileStatement *lit_create_while_statement(LitState* state, uint line, LitExpression* condition, LitStatement* body) {
	LitWhileStatement* statement = ALLOCATE_STATEMENT(state, LitWhileStatement, WHILE_STATEMENT);

	statement->condition = condition;
	statement->body = body;

	return statement;
}

LitForStatement *lit_create_for_statement(LitState* state, uint line, LitExpression* init, LitStatement* var, LitExpression* condition, LitExpression* increment, LitStatement* body, bool c_style) {
	LitForStatement* statement = ALLOCATE_STATEMENT(state, LitForStatement, FOR_STATEMENT);

	statement->init = init;
	statement->var = var;
	statement->condition = condition;
	statement->increment = increment;
	statement->body = body;
	statement->c_style = c_style;

	return statement;
}

LitContinueStatement *lit_create_continue_statement(LitState* state, uint line) {
	return ALLOCATE_STATEMENT(state, LitContinueStatement, CONTINUE_STATEMENT);
}

LitBreakStatement *lit_create_break_statement(LitState* state, uint line) {
	return ALLOCATE_STATEMENT(state, LitBreakStatement, BREAK_STATEMENT);
}

LitFunctionStatement *lit_create_function_statement(LitState* state, uint line, const char* name, uint length) {
	LitFunctionStatement* function = ALLOCATE_STATEMENT(state, LitFunctionStatement, FUNCTION_STATEMENT);

	function->name = name;
	function->length = length;
	function->body = NULL;

	lit_init_parameters(&function->parameters);

	return function;
}

LitReturnStatement *lit_create_return_statement(LitState* state, uint line, LitExpression* expression) {
	LitReturnStatement* statement = ALLOCATE_STATEMENT(state, LitReturnStatement, RETURN_STATEMENT);
	statement->expression = expression;

	return statement;
}

LitMethodStatement *lit_create_method_statement(LitState* state, uint line, LitString* name, bool is_static) {
	LitMethodStatement* statement = ALLOCATE_STATEMENT(state, LitMethodStatement, METHOD_STATEMENT);

	statement->name = name;
	statement->body = NULL;
	statement->is_static = is_static;

	lit_init_parameters(&statement->parameters);

	return statement;
}

LitClassStatement *lit_create_class_statement(LitState* state, uint line, LitString* name, LitString* parent) {
	LitClassStatement* statement = ALLOCATE_STATEMENT(state, LitClassStatement, CLASS_STATEMENT);

	statement->name = name;
	statement->parent = parent;

	lit_init_stataments(&statement->fields);

	return statement;
}

LitFieldStatement *lit_create_field_statement(LitState* state, uint line, LitString* name, LitStatement* getter, LitStatement* setter, bool is_static) {
	LitFieldStatement* statement = ALLOCATE_STATEMENT(state, LitFieldStatement, FIELD_STATEMENT);

	statement->name = name;
	statement->getter = getter;
	statement->setter = setter;
	statement->is_static = is_static;

	return statement;
}

LitExpressions* lit_allocate_expressions(LitState* state) {
	LitExpressions* expressions = (LitExpressions*) lit_reallocate(state, NULL, 0, sizeof(LitExpressions));
	lit_init_expressions(expressions);
	return expressions;
}

void lit_free_allocated_expressions(LitState* state, LitExpressions* expressions) {
	if (expressions == NULL) {
		return;
	}

	for (uint i = 0; i < expressions->count; i++) {
		lit_free_expression(state, expressions->values[i]);
	}

	lit_free_expressions(state, expressions);
	lit_reallocate(state, expressions, sizeof(LitExpressions), 0);
}

LitStatements* lit_allocate_statements(LitState* state) {
	LitStatements* statements = (LitStatements*) lit_reallocate(state, NULL, 0, sizeof(LitStatements));
	lit_init_stataments(statements);
	return statements;
}

void lit_free_allocated_statements(LitState* state, LitStatements* statements) {
	if (statements == NULL) {
		return;
	}

	for (uint i = 0; i < statements->count; i++) {
		lit_free_statement(state, statements->values[i]);
	}

	lit_free_stataments(state, statements);
	lit_reallocate(state, statements, sizeof(LitStatements), 0);
}