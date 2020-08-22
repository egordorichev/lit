#include <lit/optimizer/lit_optimizer.h>
#include <lit/lit.h>

#include <math.h>

DEFINE_ARRAY(LitVariables, LitVariable, variables)

static void optimize_expression(LitOptimizer* optimizer, LitExpression** slot);
static void optimize_expressions(LitOptimizer* optimizer, LitExpressions* expressions);
static void optimize_statements(LitOptimizer* optimizer, LitStatements* statements);
static void optimize_statement(LitOptimizer* optimizer, LitStatement** slot);

static const char* optimization_names[OPTIMIZATION_TOTAL];
static const char* optimization_descriptions[OPTIMIZATION_TOTAL];
static bool optimization_states[OPTIMIZATION_TOTAL];

static bool optimization_names_setup;
static bool optimization_descriptions_setup;
static bool optimization_states_setup;

static void setup_optimization_states();
static void setup_optimization_names();
static void setup_optimization_descriptions();

void lit_init_optimizer(LitState* state, LitOptimizer* optimizer) {
	optimizer->state = state;
	optimizer->depth = -1;
	optimizer->mark_used = false;

	lit_init_variables(&optimizer->variables);
}

static void begin_scope(LitOptimizer* optimizer) {
	optimizer->depth++;
}

static void end_scope(LitOptimizer* optimizer) {
	optimizer->depth--;
	LitVariables* variables = &optimizer->variables;

	bool remove_unused = lit_is_optimization_enabled(OPTIMIZATION_UNUSED_VAR);

	while (variables->count > 0 && variables->values[variables->count - 1].depth > optimizer->depth) {
		if (remove_unused && !variables->values[variables->count - 1].used) {
			LitVariable* variable = &variables->values[variables->count - 1];

			lit_free_statement(optimizer->state, *variable->declaration);
			*variable->declaration = NULL;
		}

		variables->count--;
	}
}

static LitVariable* add_variable(LitOptimizer* optimizer, const char* name, uint length, bool constant, LitStatement** declaration) {
	lit_variables_write(optimizer->state, &optimizer->variables, (LitVariable) {
		name, length, optimizer->depth, constant, optimizer->mark_used, NULL_VALUE, declaration
	});

	return &optimizer->variables.values[optimizer->variables.count - 1];
}

static LitVariable* resolve_variable(LitOptimizer* optimizer, const char* name, uint length) {
	LitVariables* variables = &optimizer->variables;

	for (int i = variables->count - 1; i >= 0; i--) {
		LitVariable* variable = &variables->values[i];

		if (length == variable->length && memcmp(variable->name, name, length) == 0) {
			return variable;
		}
	}

	return NULL;
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

	#define BITWISE_OP(op) \
		if (IS_NUMBER(a) && IS_NUMBER(b)) { \
			return NUMBER_VALUE((int) AS_NUMBER(a) op (int) AS_NUMBER(b)); \
	  } \
		return NULL_VALUE;

	#define FN_OP(fn) \
		if (IS_NUMBER(a) && IS_NUMBER(b)) { \
			return NUMBER_VALUE(fn(AS_NUMBER(a), AS_NUMBER(b))); \
	  } \
		return NULL_VALUE;

	switch (operator) {
		case TOKEN_PLUS: BINARY_OP(+)
		case TOKEN_MINUS: BINARY_OP(-)
		case TOKEN_STAR: BINARY_OP(*)
		case TOKEN_SLASH: BINARY_OP(/)
		case TOKEN_STAR_STAR: FN_OP(pow)
		case TOKEN_PERCENT: FN_OP(fmod)

		case TOKEN_GREATER: BINARY_OP(>)
		case TOKEN_GREATER_EQUAL: BINARY_OP(>=)
		case TOKEN_LESS: BINARY_OP(<)
		case TOKEN_LESS_EQUAL: BINARY_OP(<=)
		case TOKEN_LESS_LESS: BITWISE_OP(<<)
		case TOKEN_GREATER_GREATER: BITWISE_OP(>>)
		case TOKEN_BAR: BITWISE_OP(|)
		case TOKEN_AMPERSAND: BITWISE_OP(&)
		case TOKEN_CARET: BITWISE_OP(^)

		case TOKEN_SHARP: {
			if (IS_NUMBER(a) && IS_NUMBER(b)) {
				return NUMBER_VALUE(floor(AS_NUMBER(a) / AS_NUMBER(b)));
		  }

			return NULL_VALUE;
		}

		case TOKEN_EQUAL_EQUAL: {
			return BOOL_VALUE(a == b);
		}

		case TOKEN_BANG_EQUAL: {
			return BOOL_VALUE(a != b);
		}

		case TOKEN_IS:
		default: {
			break;
		}
	}

	#undef FN_OP
	#undef BITWISE_OP
	#undef BINARY_OP

	return NULL_VALUE;
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
			} else {
				optimize_expression(optimizer, &expr->left);
				optimize_expression(optimizer, &expr->right);
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
			if (lit_is_optimization_enabled(OPTIMIZATION_LITERAL_FOLDING)) {
				LitValue optimized = evaluate_expression(optimizer, expression);

				if (optimized != NULL_VALUE) {
					*slot = (LitExpression*) lit_create_literal_expression(state, expression->line, optimized);
					lit_free_expression(state, expression);
				}
			}

			break;
		}

		case ASSIGN_EXPRESSION: {
			LitAssignExpression* expr = (LitAssignExpression*) expression;

			optimize_expression(optimizer, &expr->to);
			optimize_expression(optimizer, &expr->value);

			break;
		}

		case CALL_EXPRESSION: {
			LitCallExpression* expr = (LitCallExpression*) expression;

			optimize_expression(optimizer, &expr->callee);
			optimize_expressions(optimizer, &expr->args);

			break;
		}

		case SET_EXPRESSION: {
			LitSetExpression* expr = (LitSetExpression*) expression;

			optimize_expression(optimizer, &expr->where);
			optimize_expression(optimizer, &expr->value);

			break;
		}

		case GET_EXPRESSION: {
			optimize_expression(optimizer, &((LitGetExpression*) expression)->where);
			break;
		}

		case LAMBDA_EXPRESSION: {
			begin_scope(optimizer);
			optimize_statement(optimizer, &((LitLambdaExpression*) expression)->body);
			end_scope(optimizer);

			break;
		}

		case ARRAY_EXPRESSION: {
			optimize_expressions(optimizer, &((LitArrayExpression*) expression)->values);
			break;
		}

		case MAP_EXPRESSION: {
			optimize_expressions(optimizer, &((LitMapExpression*) expression)->values);
			break;
		}

		case SUBSCRIPT_EXPRESSION: {
			LitSubscriptExpression* expr = (LitSubscriptExpression*) expression;

			optimize_expression(optimizer, &expr->array);
			optimize_expression(optimizer, &expr->index);

			break;
		}

		case RANGE_EXPRESSION: {
			LitRangeExpression* expr = (LitRangeExpression*) expression;

			optimize_expression(optimizer, &expr->from);
			optimize_expression(optimizer, &expr->to);

			break;
		}

		case IF_EXPRESSION: {
			LitIfExpression* expr = (LitIfExpression*) expression;
			LitValue optimized = evaluate_expression(optimizer, expr->condition);

			if (optimized != NULL_VALUE) {
				if (lit_is_falsey(optimized)) {
					*slot = expr->else_branch;
					expr->else_branch = NULL; // So that it doesn't get freed
				} else {
					*slot = expr->if_branch;
					expr->if_branch = NULL; // So that it doesn't get freed
				}

				optimize_expression(optimizer, slot);
				lit_free_expression(state, expression);
			} else {
				optimize_expression(optimizer, &expr->if_branch);
				optimize_expression(optimizer, &expr->else_branch);
			}

			break;
		}

		case INTERPOLATION_EXPRESSION: {
			optimize_expressions(optimizer, &((LitInterpolationExpression*) expression)->expressions);
			break;
		}

		case VAR_EXPRESSION: {
			LitVarExpression* expr = (LitVarExpression*) expression;
			LitVariable* variable = resolve_variable(optimizer, expr->name, expr->length);

			if (variable != NULL) {
				variable->used = true;

				// Not checking here for the enable-ness of constant-folding, since if its off
				// the constant_value would be NULL_VALUE anyway (:thinkaboutit:)
				if (variable->constant && variable->constant_value != NULL_VALUE) {
					*slot = (LitExpression*) lit_create_literal_expression(state, expression->line, variable->constant_value);
					lit_free_expression(state, expression);
				}
			}

			break;
		}

		case LITERAL_EXPRESSION:
		case THIS_EXPRESSION:
		case SUPER_EXPRESSION: {
			// Nothing, that we can do here
			break;
		}
	}
}

static void optimize_expressions(LitOptimizer* optimizer, LitExpressions* expressions) {
	for (uint i = 0; i < expressions->count; i++) {
		optimize_expression(optimizer, &expressions->values[i]);
	}
}

static void optimize_statement(LitOptimizer* optimizer, LitStatement** slot) {
	LitStatement* statement = *slot;

	if (statement == NULL) {
		return;
	}

	LitState* state = optimizer->state;

	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			optimize_expression(optimizer, &((LitExpressionStatement*) statement)->expression);
			break;
		}

		case BLOCK_STATEMENT: {
			LitBlockStatement* stmt = (LitBlockStatement*) statement;

			if (stmt->statements.count == 0) {
				*slot = NULL;
				lit_free_statement(state, statement);

				break;
			}

			begin_scope(optimizer);
			optimize_statements(optimizer, &stmt->statements);
			end_scope(optimizer);

			bool found = false;

			for (uint i = 0; i < stmt->statements.count; i++) {
				LitStatement* step = stmt->statements.values[i];

				if (step != NULL) {
					found = true;

					if (step->type == RETURN_STATEMENT) {
						// Remove all the statements post return
						for (uint j = i + 1; j < stmt->statements.count; j++) {
							step = stmt->statements.values[j];

							if (step != NULL) {
								lit_free_statement(state, step);
								stmt->statements.values[j] = NULL;
							}
						}

						stmt->statements.count = i + 1;
						break;
					}
				}
			}

			break;
		}

		case IF_STATEMENT: {
			break;
		}

		case WHILE_STATEMENT: {
			break;
		}

		case FOR_STATEMENT: {
			LitForStatement* stmt = (LitForStatement*) statement;

			begin_scope(optimizer);
			// This is required, so that optimizer doesn't optimize out our i variable (and such)
			optimizer->mark_used = true;

			optimize_expression(optimizer, &stmt->init);
			optimize_expression(optimizer, &stmt->condition);
			optimize_expression(optimizer, &stmt->increment);

			optimize_statement(optimizer, &stmt->var);
			optimizer->mark_used = false;

			optimize_statement(optimizer, &stmt->body);
			end_scope(optimizer);

			break;
		}

		case VAR_STATEMENT: {
			LitVarStatement* stmt = (LitVarStatement*) statement;
			LitVariable* variable = add_variable(optimizer, stmt->name, stmt->length, stmt->constant, slot);

			optimize_expression(optimizer, &stmt->init);

			if (stmt->constant && lit_is_optimization_enabled(OPTIMIZATION_CONSTANT_FOLDING)) {
				LitValue value = evaluate_expression(optimizer, stmt->init);

				if (value != NULL_VALUE) {
					variable->constant_value = value;
				}
			}

			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;
			LitVariable* variable = add_variable(optimizer, stmt->name, stmt->length, false, slot);

			if (stmt->export) {
				// Otherwise it will get optimized-out with a big chance
				variable->used = true;
			}

			begin_scope(optimizer);
			optimize_statement(optimizer, &stmt->body);
			end_scope(optimizer);

			break;
		}

		case RETURN_STATEMENT: {
			optimize_expression(optimizer, &((LitReturnStatement*) statement)->expression);
			break;
		}

		case METHOD_STATEMENT: {
			begin_scope(optimizer);
			optimize_statement(optimizer, &((LitMethodStatement*) statement)->body);
			end_scope(optimizer);

			break;
		}

		case CLASS_STATEMENT: {
			optimize_statements(optimizer, &((LitClassStatement*) statement)->fields);
			break;
		}

		case FIELD_STATEMENT: {
			break;
		}

		// Nothing to optimize there
		case CONTINUE_STATEMENT:
		case BREAK_STATEMENT: {
			break;
		}
	}
}

static void optimize_statements(LitOptimizer* optimizer, LitStatements* statements) {
	for (uint i = 0; i < statements->count; i++) {
		optimize_statement(optimizer, &statements->values[i]);
	}
}

void lit_optimize(LitOptimizer* optimizer, LitStatements* statements) {
	begin_scope(optimizer);
	optimize_statements(optimizer, statements);
	end_scope(optimizer);

	lit_free_variables(optimizer->state, &optimizer->variables);
}

static void setup_optimization_states() {
	optimization_states_setup = true;

	for (uint i = 0; i < OPTIMIZATION_TOTAL; i++) {
		optimization_states[i] = true;
	}
}

bool lit_is_optimization_enabled(LitOptimization optimization) {
	if (!optimization_states_setup) {
		setup_optimization_states();
	}

	return optimization_states[(int) optimization];
}

void lit_set_optimization_enabled(LitOptimization optimization, bool enabled) {
	if (!optimization_states_setup) {
		setup_optimization_states();
	}

	optimization_states[(int) optimization] = enabled;
}

void lit_set_all_optimization_enabled(bool enabled) {
	optimization_states_setup = true;

	for (uint i = 0; i < OPTIMIZATION_TOTAL; i++) {
		optimization_states[i] = enabled;
	}
}

const char* lit_get_optimization_name(LitOptimization optimization) {
	if (!optimization_names_setup) {
		setup_optimization_names();
	}

	return optimization_names[(int) optimization];
}

const char* lit_get_optimization_description(LitOptimization optimization) {
	if (!optimization_descriptions_setup) {
		setup_optimization_descriptions();
	}

	return optimization_descriptions[(int) optimization];
}

static void setup_optimization_names() {
	optimization_names_setup = true;

	optimization_names[OPTIMIZATION_CONSTANT_FOLDING] = "constant-folding";
	optimization_names[OPTIMIZATION_LITERAL_FOLDING] = "literal-folding";
	optimization_names[OPTIMIZATION_UNUSED_VAR] = "unused-var";
}

static void setup_optimization_descriptions() {
	optimization_descriptions_setup = true;

	optimization_descriptions[OPTIMIZATION_CONSTANT_FOLDING] = "Replaces constants in code with their values.";
	optimization_descriptions[OPTIMIZATION_LITERAL_FOLDING] = "Precalculates literal expressions (3 + 4 is replaced with 7).";
	optimization_descriptions[OPTIMIZATION_UNUSED_VAR] = "Removes user-declared all variables, that were not used.";
}