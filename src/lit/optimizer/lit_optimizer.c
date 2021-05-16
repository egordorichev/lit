#include "optimizer/lit_optimizer.h"
#include "lit.h"

#include <math.h>

DEFINE_ARRAY(LitVariables, LitVariable, variables)

static void optimize_expression(LitOptimizer* optimizer, LitExpression** slot);
static void optimize_expressions(LitOptimizer* optimizer, LitExpressions* expressions);
static void optimize_statements(LitOptimizer* optimizer, LitStatements* statements);
static void optimize_statement(LitOptimizer* optimizer, LitStatement** slot);

static const char* optimization_level_descriptions[OPTIMIZATION_LEVEL_TOTAL] = {
	"No optimizations (same as -Ono-all)",
	"Super light optimizations, sepcific to interactive shell.",
	"(default) Recommended optimization level for the development.",
	"Medium optimization, recommended for the release.",
	"(default for bytecode) Extreme optimization, throws out most of the variable/function names, used for bytecode compilation."
};

static const char* optimization_names[OPTIMIZATION_TOTAL] = {
	"constant-folding",
	"literal-folding",
	"unused-var",
	"unreachable-code",
	"empty-body",
	"line-info",
	"private-names",
	"c-for"
};

static const char* optimization_descriptions[OPTIMIZATION_TOTAL] = {
	"Replaces constants in code with their values.",
	"Precalculates literal expressions (3 + 4 is replaced with 7).",
	"Removes user-declared all variables, that were not used.",
	"Removes code that will never be reached.",
	"Removes loops with empty bodies.",
	"Removes line information from chunks to save on space.",
	"Removes names of the private locals from modules (they are indexed by id at runtime).",
	"Replaces for-in loops with c-style for loops where it can."
};

static bool optimization_states[OPTIMIZATION_TOTAL];

static bool optimization_states_setup = false;
static bool any_optimization_enabled = false;

static void setup_optimization_states();

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

static bool is_empty(LitStatement* statement) {
	return statement == NULL || (statement->type == BLOCK_STATEMENT && ((LitBlockStatement*) statement)->statements.count == 0);
}

static LitValue evaluate_unary_op(LitValue value, LitTokenType op) {
	switch (op) {
		case LTOKEN_MINUS: {
			if (IS_NUMBER(value)) {
				return NUMBER_VALUE(-AS_NUMBER(value));
			}

			break;
		}

		case LTOKEN_BANG: {
			return BOOL_VALUE(lit_is_falsey(value));
		}

		case LTOKEN_TILDE: {
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

static LitValue evaluate_binary_op(LitValue a, LitValue b, LitTokenType op) {
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

	switch (op) {
		case LTOKEN_PLUS: BINARY_OP(+)
		case LTOKEN_MINUS: BINARY_OP(-)
		case LTOKEN_STAR: BINARY_OP(*)
		case LTOKEN_SLASH: BINARY_OP(/)
		case LTOKEN_STAR_STAR: FN_OP(pow)
		case LTOKEN_PERCENT: FN_OP(fmod)

		case LTOKEN_GREATER: BINARY_OP(>)
		case LTOKEN_GREATER_EQUAL: BINARY_OP(>=)
		case LTOKEN_LESS: BINARY_OP(<)
		case LTOKEN_LESS_EQUAL: BINARY_OP(<=)
		case LTOKEN_LESS_LESS: BITWISE_OP(<<)
		case LTOKEN_GREATER_GREATER: BITWISE_OP(>>)
		case LTOKEN_BAR: BITWISE_OP(|)
		case LTOKEN_AMPERSAND: BITWISE_OP(&)
		case LTOKEN_CARET: BITWISE_OP(^)

		case LTOKEN_SHARP: {
			if (IS_NUMBER(a) && IS_NUMBER(b)) {
				return NUMBER_VALUE(floor(AS_NUMBER(a) / AS_NUMBER(b)));
		  }

			return NULL_VALUE;
		}

		case LTOKEN_EQUAL_EQUAL: {
			return BOOL_VALUE(a == b);
		}

		case LTOKEN_BANG_EQUAL: {
			return BOOL_VALUE(a != b);
		}

		case LTOKEN_IS:
		default: {
			break;
		}
	}

	#undef FN_OP
	#undef BITWISE_OP
	#undef BINARY_OP

	return NULL_VALUE;
}

static LitValue attempt_to_optimize_binary(LitOptimizer* optimizer, LitBinaryExpression* expression, LitValue value, bool left) {
	LitTokenType op = expression->op;
	LitExpression* branch = left ? expression->left : expression->right;

	if (IS_NUMBER(value)) {
		double number = AS_NUMBER(value);

		if (op == LTOKEN_STAR) {
			if (number == 0) {
				return NUMBER_VALUE(0);
			} else if (number == 1) {
				lit_free_expression(optimizer->state, left ? expression->right : expression->left);

				expression->left = branch;
				expression->right = NULL;
			}
		} else if ((op == LTOKEN_PLUS || op == LTOKEN_MINUS) && number == 0) {
			lit_free_expression(optimizer->state, left ? expression->right : expression->left);

			expression->left = branch;
			expression->right = NULL;
		} else if (((left && op == LTOKEN_SLASH) || op == LTOKEN_STAR_STAR) && number == 1) {
			lit_free_expression(optimizer->state, left ? expression->right : expression->left);

			expression->left = branch;
			expression->right = NULL;
		}
	}

	return NULL_VALUE;
}

static LitValue evaluate_expression(LitOptimizer* optimizer, LitExpression* expression) {
	if (expression == NULL) {
		return NULL_VALUE;
	}

	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			return ((LitLiteralExpression*) expression)->value;
		}

		case UNARY_EXPRESSION: {
			LitUnaryExpression* expr = (LitUnaryExpression*) expression;
			LitValue branch = evaluate_expression(optimizer, expr->right);

			if (branch != NULL_VALUE) {
				return evaluate_unary_op(branch, expr->op);
			}

			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			LitValue a = evaluate_expression(optimizer, expr->left);
			LitValue b = evaluate_expression(optimizer, expr->right);

			if (a != NULL_VALUE && b != NULL_VALUE) {
				return evaluate_binary_op(a, b, expr->op);
			} else if (a != NULL_VALUE) {
				return attempt_to_optimize_binary(optimizer, expr, a, false);
			} else if (b != NULL_VALUE) {
				return attempt_to_optimize_binary(optimizer, expr, b, true);
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
		case BINARY_EXPRESSION: {
			if (lit_is_optimization_enabled(OPTIMIZATION_LITERAL_FOLDING)) {
				LitValue optimized = evaluate_expression(optimizer, expression);

				if (optimized != NULL_VALUE) {
					*slot = (LitExpression*) lit_create_literal_expression(state, expression->line, optimized);
					lit_free_expression(state, expression);
					break;
				}
			}

			switch (expression->type) {
				case UNARY_EXPRESSION: {
					optimize_expression(optimizer, &((LitUnaryExpression*) expression)->right);
					break;
				}

				case BINARY_EXPRESSION: {
					LitBinaryExpression* expr = (LitBinaryExpression*) expression;

					optimize_expression(optimizer, &expr->left);
					optimize_expression(optimizer, &expr->right);

					break;
				}

				default: {
					UNREACHABLE
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

		case OBJECT_EXPRESSION: {
			optimize_expressions(optimizer, &((LitObjectExpression*) expression)->values);
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

		case REFERENCE_EXPRESSION: {
			optimize_expression(optimizer, &((LitReferenceExpression*) expression)->to);
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
				lit_free_statement(state, statement);
				*slot = NULL;

				break;
			}

			begin_scope(optimizer);
			optimize_statements(optimizer, &stmt->statements);
			end_scope(optimizer);

			bool found = false;

			for (uint i = 0; i < stmt->statements.count; i++) {
				LitStatement* step = stmt->statements.values[i];

				if (!is_empty(step)) {
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

			if (!found && lit_is_optimization_enabled(OPTIMIZATION_EMPTY_BODY)) {
				lit_free_statement(optimizer->state, statement);
				*slot = NULL;
			}

			break;
		}

		case IF_STATEMENT: {
			LitIfStatement* stmt = (LitIfStatement*) statement;

			optimize_expression(optimizer, &stmt->condition);
			optimize_statement(optimizer, &stmt->if_branch);

			bool empty = lit_is_optimization_enabled(OPTIMIZATION_EMPTY_BODY);
			bool dead = lit_is_optimization_enabled(OPTIMIZATION_UNREACHABLE_CODE);

			LitValue optimized = empty ? evaluate_expression(optimizer, stmt->condition) : NULL_VALUE;

			if ((optimized != NULL_VALUE && lit_is_falsey(optimized)) || (dead && is_empty(stmt->if_branch))) {
				lit_free_expression(state, stmt->condition);
				stmt->condition = NULL;

				lit_free_statement(state, stmt->if_branch);
				stmt->if_branch = NULL;
			}

			if (stmt->elseif_conditions != NULL) {
				optimize_expressions(optimizer, stmt->elseif_conditions);
				optimize_statements(optimizer, stmt->elseif_branches);

				if (dead || empty) {
					for (uint i = 0; i < stmt->elseif_conditions->count; i++) {
						if (empty && is_empty(stmt->elseif_branches->values[i])) {
							lit_free_expression(state, stmt->elseif_conditions->values[i]);
							stmt->elseif_conditions->values[i] = NULL;

							lit_free_statement(state, stmt->elseif_branches->values[i]);
							stmt->elseif_branches->values[i] = NULL;

							continue;
						}

						if (dead) {
							LitValue value = evaluate_expression(optimizer, stmt->elseif_conditions->values[i]);

							if (value != NULL_VALUE && lit_is_falsey(value)) {
								lit_free_expression(state, stmt->elseif_conditions->values[i]);
								stmt->elseif_conditions->values[i] = NULL;

								lit_free_statement(state, stmt->elseif_branches->values[i]);
								stmt->elseif_branches->values[i] = NULL;
							}
						}
					}
				}
			}

			optimize_statement(optimizer, &stmt->else_branch);
			break;
		}

		case WHILE_STATEMENT: {
			LitWhileStatement* stmt = (LitWhileStatement*) statement;
			optimize_expression(optimizer, &stmt->condition);

			if (lit_is_optimization_enabled(OPTIMIZATION_UNREACHABLE_CODE)) {
				LitValue optimized = evaluate_expression(optimizer, stmt->condition);

				if (optimized != NULL_VALUE && lit_is_falsey(optimized)) {
					lit_free_statement(optimizer->state, statement);
					*slot = NULL;
					break;
				}
			}

			optimize_statement(optimizer, &stmt->body);

			if (lit_is_optimization_enabled(OPTIMIZATION_EMPTY_BODY) && is_empty(stmt->body)) {
				lit_free_statement(optimizer->state, statement);
				*slot = NULL;
			}

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

			if (lit_is_optimization_enabled(OPTIMIZATION_EMPTY_BODY) && is_empty(stmt->body)) {
				lit_free_statement(optimizer->state, statement);
				*slot = NULL;

				break;
			}

			if (stmt->c_style || !lit_is_optimization_enabled(OPTIMIZATION_C_FOR) || stmt->condition->type != RANGE_EXPRESSION) {
				break;
			}

			LitRangeExpression* range = (LitRangeExpression*) stmt->condition;
			LitValue from = evaluate_expression(optimizer, range->from);
			LitValue to = evaluate_expression(optimizer, range->to);

			if (!IS_NUMBER(from) || !IS_NUMBER(to)) {
				break;
			}

			bool reverse = AS_NUMBER(from) > AS_NUMBER(to);

			LitVarStatement* var = (LitVarStatement*) stmt->var;
			uint line = range->expression.line;

			// var i = from
			var->init = range->from;

			// i <= to
			stmt->condition = (LitExpression*) lit_create_binary_expression(state, line, (LitExpression*) lit_create_var_expression(state, line, var->name, var->length), range->to, LTOKEN_LESS_EQUAL);

			// i++ (or i--)
			LitExpression* var_get = (LitExpression*) lit_create_var_expression(state, line, var->name, var->length);
			LitBinaryExpression* assign_value = lit_create_binary_expression(state, line, var_get, (LitExpression*) lit_create_literal_expression(state, line, NUMBER_VALUE(1)), reverse ? LTOKEN_MINUS_MINUS : LTOKEN_PLUS);
			assign_value->ignore_left = true;

			LitExpression* increment = (LitExpression*) lit_create_assign_expression(state, line, var_get, (LitExpression*) assign_value);
			stmt->increment = (LitExpression*) increment;

			range->from = NULL;
			range->to = NULL;

			stmt->c_style = true;
			lit_free_expression(state, (LitExpression*) range);

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

			if (stmt->exported) {
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
			LitFieldStatement* stmt = (LitFieldStatement*) statement;

			if (stmt->getter != NULL) {
				begin_scope(optimizer);
				optimize_statement(optimizer, &stmt->getter);
				end_scope(optimizer);
			}

			if (stmt->setter != NULL) {
				begin_scope(optimizer);
				optimize_statement(optimizer, &stmt->setter);
				end_scope(optimizer);
			}

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
	if (!optimization_states_setup) {
		setup_optimization_states();
	}

	if (!any_optimization_enabled) {
		return;
	}

	begin_scope(optimizer);
	optimize_statements(optimizer, statements);
	end_scope(optimizer);

	lit_free_variables(optimizer->state, &optimizer->variables);
}

static void setup_optimization_states() {
	lit_set_optimization_level(OPTIMIZATION_LEVEL_DEBUG);
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

	if (enabled) {
		any_optimization_enabled = true;
	} else {
		for (uint i = 0; i < OPTIMIZATION_TOTAL; i++) {
			if (optimization_states[i]) {
				return;
			}
		}

		any_optimization_enabled = false;
	}
}

void lit_set_all_optimization_enabled(bool enabled) {
	optimization_states_setup = true;
	any_optimization_enabled = enabled;

	for (uint i = 0; i < OPTIMIZATION_TOTAL; i++) {
		optimization_states[i] = enabled;
	}
}

void lit_set_optimization_level(LitOptimizationLevel level) {
	switch (level) {
		case OPTIMIZATION_LEVEL_NONE: {
			lit_set_all_optimization_enabled(false);
			break;
		}

		case OPTIMIZATION_LEVEL_REPL: {
			lit_set_all_optimization_enabled(true);

			lit_set_optimization_enabled(OPTIMIZATION_UNUSED_VAR, false);
			lit_set_optimization_enabled(OPTIMIZATION_UNREACHABLE_CODE, false);
			lit_set_optimization_enabled(OPTIMIZATION_EMPTY_BODY, false);
			lit_set_optimization_enabled(OPTIMIZATION_LINE_INFO, false);
			lit_set_optimization_enabled(OPTIMIZATION_PRIVATE_NAMES, false);

			break;
		}

		case OPTIMIZATION_LEVEL_DEBUG: {
			lit_set_all_optimization_enabled(true);

			lit_set_optimization_enabled(OPTIMIZATION_UNUSED_VAR, false);
			lit_set_optimization_enabled(OPTIMIZATION_LINE_INFO, false);
			lit_set_optimization_enabled(OPTIMIZATION_PRIVATE_NAMES, false);

			break;
		}

		case OPTIMIZATION_LEVEL_RELEASE: {
			lit_set_all_optimization_enabled(true);
			lit_set_optimization_enabled(OPTIMIZATION_LINE_INFO, false);

			break;
		}

		case OPTIMIZATION_LEVEL_EXTREME: {
			lit_set_all_optimization_enabled(true);
			break;
		}

		case OPTIMIZATION_LEVEL_TOTAL: {
			break;
		}
	}
}

const char* lit_get_optimization_name(LitOptimization optimization) {
	return optimization_names[(int) optimization];
}

const char* lit_get_optimization_description(LitOptimization optimization) {
	return optimization_descriptions[(int) optimization];
}

const char* lit_get_optimization_level_description(LitOptimizationLevel level) {
	return optimization_level_descriptions[(int) level];
}