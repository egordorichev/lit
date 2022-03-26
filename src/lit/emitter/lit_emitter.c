#include "emitter/lit_emitter.h"
#include "parser/lit_error.h"
#include "mem/lit_mem.h"
#include"debug/lit_debug.h"
#include "vm/lit_object.h"
#include "vm/lit_vm.h"
#include "scanner/lit_scanner.h"
#include "util/lit_table.h"
#include "optimizer/lit_optimizer.h"

#include <math.h>
#include <string.h>

DEFINE_ARRAY(LitPrivates, LitPrivate, privates)
DEFINE_ARRAY(LitLocals, LitLocal, locals)

static void emit_expression(LitEmitter* emitter, LitExpression* expression, uint8_t reg);
static bool emit_statement(LitEmitter* emitter, LitStatement* statement);
static void resolve_statement(LitEmitter* emitter, LitStatement* statement);

static void resolve_statements(LitEmitter* emitter, LitStatements* statements) {
	for (uint i = 0; i < statements->count; i++) {
		resolve_statement(emitter, statements->values[i]);
	}
}

void lit_init_emitter(LitState* state, LitEmitter* emitter) {
	emitter->state = state;
	emitter->loop_start = 0;
	emitter->emit_reference = 0;
	emitter->class_name = NULL;
	emitter->compiler = NULL;
	emitter->chunk = NULL;
	emitter->module = NULL;
	emitter->class_has_super = false;

	lit_init_privates(&emitter->privates);
	lit_init_uints(&emitter->breaks);
	lit_init_uints(&emitter->continues);
}

void lit_free_emitter(LitEmitter* emitter) {
	lit_free_uints(emitter->state, &emitter->breaks);
	lit_free_uints(emitter->state, &emitter->continues);
}

static void error(LitEmitter* emitter, uint line, LitError error, ...) {
	va_list args;
	va_start(args, error);
	lit_error(emitter->state, COMPILE_ERROR, lit_vformat_error(emitter->state, line, error, args)->chars);
	va_end(args);
}

static uint emit_tmp_instruction(LitEmitter* emitter) {
	lit_write_chunk(emitter->state, emitter->chunk, 0, emitter->last_line);
	return emitter->chunk->count - 1;
}

static void patch_instruction(LitEmitter* emitter, uint64_t position, uint64_t instruction) {
	emitter->chunk->code[position] = instruction;
}

static void emit_abc_instruction(LitEmitter* emitter, uint16_t line, uint8_t opcode, uint8_t a, uint16_t b, uint16_t c) {
	emitter->last_line = fmax(line, emitter->last_line);
	lit_write_chunk(emitter->state, emitter->chunk, LIT_FORM_ABC_INSTRUCTION(opcode, a, b, c), emitter->last_line);
}

static void emit_abx_instruction(LitEmitter* emitter, uint16_t line, uint8_t opcode, uint8_t a, uint32_t bx) {
	emitter->last_line = fmax(line, emitter->last_line);
	lit_write_chunk(emitter->state, emitter->chunk, LIT_FORM_ABX_INSTRUCTION(opcode, a, bx), emitter->last_line);
}

static void emit_asbx_instruction(LitEmitter* emitter, uint16_t line, uint8_t opcode, uint8_t a, int32_t sbx) {
	emitter->last_line = fmax(line, emitter->last_line);
	lit_write_chunk(emitter->state, emitter->chunk, LIT_FORM_ASBX_INSTRUCTION(opcode, a, sbx), emitter->last_line);
}

static void dump_used_registers(LitEmitter* emitter) {
	printf("[ ");
	for (uint i = 0; i < emitter->compiler->registers_used; i++) {
		printf("%i ", i);
	}

	printf("]\n");
}

// Be very careful with the use of this function, always reserve a register just before using it, do not wait around!
static uint8_t reserve_register(LitEmitter* emitter) {
	LitCompiler* compiler = emitter->compiler;

	if (compiler->registers_used == LIT_REGISTERS_MAX) {
		error(emitter, emitter->last_line, ERROR_TOO_MANY_REGISTERS);
		return 0;
	}

	compiler->function->max_registers = fmax(compiler->function->max_registers, ++compiler->registers_used);
	return compiler->registers_used;
}

static void free_register(LitEmitter* emitter, uint16_t reg) {
	if (IS_BIT_SET(reg, 9)) {
		return;
	}

	LitCompiler* compiler = emitter->compiler;

	if (compiler->registers_used == 0) {
		return error(emitter, emitter->last_line, ERROR_INVALID_REGISTER_FREED);
	}

	compiler->registers_used--;
}

static void init_compiler(LitEmitter* emitter, LitCompiler* compiler, LitFunctionType type) {
	lit_init_locals(&compiler->locals);

	compiler->type = type;
	compiler->scope_depth = -1;
	compiler->enclosing = (struct LitCompiler*) emitter->compiler;
	compiler->skip_return = false;
	compiler->function = lit_create_function(emitter->state, emitter->module);
	compiler->loop_depth = 0;
	compiler->registers_used = 0;

	emitter->compiler = compiler;

	const char* name = emitter->state->scanner->file_name;

	if (emitter->compiler == NULL) {
		compiler->function->name = lit_copy_string(emitter->state, name, strlen(name));
	}

	emitter->chunk = &compiler->function->chunk;

	if (lit_is_optimization_enabled(OPTIMIZATION_LINE_INFO)) {
		emitter->chunk->has_line_info = false;
	}

	if (type == FUNCTION_METHOD || type == FUNCTION_STATIC_METHOD || type == FUNCTION_CONSTRUCTOR) {
		lit_locals_write(emitter->state, &compiler->locals, (LitLocal) {
			"this", 4, -1, false, false, 0
		});
	} else {
		lit_locals_write(emitter->state, &compiler->locals, (LitLocal) {
			"", 0, -1, false, false, 0
		});
	}
}

static LitFunction* end_compiler(LitEmitter* emitter, LitString* name) {
	if (emitter->compiler->registers_used > 0) {
		error(emitter, emitter->last_line, ERROR_NOT_ALL_REGISTERS_FREED, emitter->compiler->registers_used);
	}

	if (!emitter->compiler->skip_return) {
		uint8_t reg = reserve_register(emitter);

		emit_abc_instruction(emitter, emitter->last_line, OP_LOAD_NULL, reg, 0, 0);
		emit_abc_instruction(emitter, emitter->last_line, OP_RETURN, reg, 1, 0);
		free_register(emitter, reg);

		emitter->compiler->skip_return = true;
	}

	LitFunction* function = emitter->compiler->function;

	lit_free_locals(emitter->state, &emitter->compiler->locals);

	emitter->compiler = (LitCompiler*) emitter->compiler->enclosing;
	emitter->chunk = emitter->compiler == NULL ? NULL : &emitter->compiler->function->chunk;

	if (name != NULL) {
		function->name = name;
	}

#ifdef LIT_TRACE_CHUNK
	if (!emitter->state->had_error) {
		lit_disassemble_chunk(&function->chunk, function->name->chars, NULL);
	}
#endif

	return function;
}

static void begin_scope(LitEmitter* emitter) {
	emitter->compiler->scope_depth++;
}

static void end_scope(LitEmitter* emitter) {
	if (emitter->compiler->scope_depth == -1) {
		error(emitter, emitter->last_line, ERROR_INVALID_SCOPE_ENDING);
	}

	emitter->compiler->scope_depth--;

	LitCompiler* compiler = emitter->compiler;
	LitLocals* locals = &compiler->locals;

	while (locals->count > 0 && locals->values[locals->count - 1].depth > compiler->scope_depth) {
		LitLocal* local = &locals->values[locals->count - 1];

		if (local->captured) {
			emit_abc_instruction(emitter, emitter->last_line, OP_CLOSE_UPVALUE, local->reg, 0, 0);
		}

		free_register(emitter, local->reg);
		locals->count--;
	}
}

static uint16_t add_constant(LitEmitter* emitter, uint line, LitValue value) {
	uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

	if (constant >= UINT16_MAX) {
		error(emitter, line, ERROR_TOO_MANY_CONSTANTS);
	}

	return constant;
}

static int add_private(LitEmitter* emitter, const char* name, uint length, uint line, bool constant) {
	LitPrivates* privates = &emitter->privates;

	if (privates->count == UINT16_MAX) {
		error(emitter, line, ERROR_TOO_MANY_PRIVATES);
	}

	LitTable* private_names = &emitter->module->private_names->values;
	LitString* key = lit_table_find_string(private_names, name, length, lit_hash_string(name, length));

	if (key != NULL) {
		error(emitter, line, ERROR_VAR_REDEFINED, length, name);

		LitValue index;
		lit_table_get(private_names, key, &index);

		return AS_NUMBER(index);
	}

	LitState* state = emitter->state;
	int index = (int) privates->count;

	lit_privates_write(state, privates, (LitPrivate) {
		false, constant
	});

	lit_table_set(state, private_names, lit_copy_string(state, name, length), NUMBER_VALUE(index));
	emitter->module->private_count++;

	return index;
}

static int resolve_private(LitEmitter* emitter, const char* name, uint length, uint line) {
	LitTable* private_names = &emitter->module->private_names->values;
	LitString* key = lit_table_find_string(private_names, name, length, lit_hash_string(name, length));

	if (key != NULL) {
		LitValue index;
		lit_table_get(private_names, key, &index);

		int number_index = AS_NUMBER(index);

		if (!emitter->privates.values[number_index].initialized) {
			error(emitter, line, ERROR_VARIABLE_USED_IN_INIT, length, name);
		}

		return number_index;
	}

	return -1;
}

static int add_local(LitEmitter* emitter, const char* name, uint length, uint line, bool constant, uint8_t reg) {
	LitCompiler* compiler = emitter->compiler;
	LitLocals* locals = &compiler->locals;

	if (locals->count == UINT16_MAX) {
		error(emitter, line, ERROR_TOO_MANY_LOCALS);
	}

	for (int i = (int) locals->count - 1; i >= 0; i--) {
		LitLocal* local = &locals->values[i];

		if (local->depth != UINT16_MAX && local->depth < compiler->scope_depth) {
			break;
		}

		if (length == local->length && memcmp(local->name, name, length) == 0) {
			error(emitter, line, ERROR_VAR_REDEFINED, length, name);
		}
	}

	lit_locals_write(emitter->state, locals, (LitLocal) {
		name, length, UINT16_MAX, false, constant, reg
	});

	return (int) locals->count - 1;
}

static int resolve_local(LitEmitter* emitter, LitCompiler* compiler, const char* name, uint length, uint line) {
	LitLocals* locals = &compiler->locals;

	for (int i = (int) locals->count - 1; i >= 0; i--) {
		LitLocal* local = &locals->values[i];

		if (local->length == length && memcmp(local->name, name, length) == 0) {
			if (local->depth == UINT16_MAX) {
				error(emitter, line, ERROR_VARIABLE_USED_IN_INIT, length, name);
			}

			return i;
		}
	}

	return -1;
}

static int add_upvalue(LitEmitter* emitter, LitCompiler* compiler, uint8_t index, uint line, bool is_local) {
	uint upvalue_count = compiler->function->upvalue_count;

	for (uint i = 0; i < upvalue_count; i++) {
		LitCompilerUpvalue* upvalue = &compiler->upvalues[i];

		if (upvalue->index == index && upvalue->isLocal == is_local) {
			return i;
		}
	}

	if (upvalue_count == UINT16_COUNT) {
		error(emitter, line, ERROR_TOO_MANY_UPVALUES);
		return 0;
	}

	compiler->upvalues[upvalue_count].isLocal = is_local;
	compiler->upvalues[upvalue_count].index = index;

	return compiler->function->upvalue_count++;
}

static int resolve_upvalue(LitEmitter* emitter, LitCompiler* compiler, const char* name, uint length, uint line) {
	if (compiler->enclosing == NULL) {
		return -1;
	}

	int local = resolve_local(emitter, (LitCompiler*) compiler->enclosing, name, length, line);

	if (local != -1) {
		((LitCompiler*) compiler->enclosing)->locals.values[local].captured = true;
		return add_upvalue(emitter, compiler, (uint8_t) local, line, true);
	}

	int upvalue = resolve_upvalue(emitter, (LitCompiler*) compiler->enclosing, name, length, line);

	if (upvalue != -1) {
		return add_upvalue(emitter, compiler, (uint8_t) upvalue, line, false);
	}

	return -1;
}

static void mark_local_initialized(LitEmitter* emitter, uint index) {
	emitter->compiler->locals.values[index].depth = emitter->compiler->scope_depth;
}

static void mark_private_initialized(LitEmitter* emitter, uint index) {
	emitter->privates.values[index].initialized = true;
}

static void resolve_statement(LitEmitter* emitter, LitStatement* statement) {
	switch (statement->type) {
		case VAR_STATEMENT: {
			LitVarStatement* stmt = (LitVarStatement*) statement;
			mark_private_initialized(emitter, add_private(emitter, stmt->name, stmt->length, statement->line, stmt->constant));

			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;

			if (!stmt->exported) {
				mark_private_initialized(emitter, add_private(emitter, stmt->name, stmt->length, statement->line, false));
			}

			break;
		}

		default: {
			break;
		}
	}
}

static LitOpCode translate_unary_operator_into_op(LitTokenType token) {
	switch (token) {
		case LTOKEN_MINUS: return OP_NEGATE;
		case LTOKEN_BANG: return OP_NOT;
		case LTOKEN_TILDE: return OP_BNOT;

		default: UNREACHABLE
	}
}

static LitOpCode translate_binary_operator_into_op(LitTokenType token) {
	switch (token) {
		case LTOKEN_BANG_EQUAL: case LTOKEN_EQUAL_EQUAL: return OP_EQUAL;
		case LTOKEN_LESS: case LTOKEN_GREATER: return OP_LESS;
		case LTOKEN_LESS_EQUAL: case LTOKEN_GREATER_EQUAL: return OP_LESS_EQUAL;

		case LTOKEN_PLUS: return OP_ADD;
		case LTOKEN_MINUS: return OP_SUBTRACT;
		case LTOKEN_STAR: return OP_MULTIPLY;
		case LTOKEN_STAR_STAR: return OP_POWER;
		case LTOKEN_SLASH: return OP_DIVIDE;
		case LTOKEN_SHARP: return OP_FLOOR_DIVIDE;
		case LTOKEN_PERCENT: return OP_MOD;

		case LTOKEN_LESS_LESS: return OP_LSHIFT;
		case LTOKEN_GREATER_GREATER: return OP_RSHIFT;
		case LTOKEN_CARET: return OP_BXOR;
		case LTOKEN_AMPERSAND: return OP_BAND;
		case LTOKEN_BAR: return OP_BOR;

		case LTOKEN_IS: return OP_IS;

		default: UNREACHABLE
	}
}

static uint16_t parse_argument(LitEmitter* emitter, LitExpression* expression, uint8_t reg) {
	if (expression->type == LITERAL_EXPRESSION) {
		LitValue value = ((LitLiteralExpression*) expression)->value;

		if (IS_NUMBER(value) || IS_STRING(value)) {
			uint16_t arg = add_constant(emitter, expression->line, value);
			SET_BIT(arg, 8) // Mark that this is a constant

			return arg;
		}
	} else if (expression->type == VAR_EXPRESSION) {
		LitVarExpression* expr = ((LitVarExpression*) expression);
		int index = resolve_local(emitter, emitter->compiler, expr->name, expr->length, expression->line);

		if (index != -1) {
			return emitter->compiler->locals.values[index].reg;
		}
	}

	emit_expression(emitter, expression, reg);
	return reg;
}

static void emit_binary_expression(LitEmitter* emitter, LitBinaryExpression* expr, uint8_t reg, bool swap) {
	LitTokenType op = expr->op;

	if (op == LTOKEN_AMPERSAND_AMPERSAND || op == LTOKEN_BAR_BAR || op == LTOKEN_QUESTION_QUESTION) {
		emit_expression(emitter, expr->left, reg);
		uint jump = emit_tmp_instruction(emitter);
		emit_expression(emitter, expr->right, reg);

		patch_instruction(emitter, jump, LIT_FORM_ABX_INSTRUCTION(op == LTOKEN_BAR_BAR ? OP_TRUE_JUMP : (op == LTOKEN_QUESTION_QUESTION ? OP_NON_NULL_JUMP : OP_FALSE_JUMP),
      reg, emitter->chunk->count - jump - 1
		));
	} else {
		uint16_t b = parse_argument(emitter, expr->left, reg);
		LitOpCode opcode = translate_binary_operator_into_op(op);

		if (opcode == OP_IS) {
			if (expr->right->type != VAR_EXPRESSION) {
				return error(emitter, expr->expression.line, ERROR_IS_NOT_USED_WITH_VAR);
			}

			LitVarExpression* e = (LitVarExpression*) expr->right;

			int constant = add_constant(emitter, expr->expression.line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length)));
			emit_abc_instruction(emitter, expr->expression.line, opcode, reg, b, constant);
		} else {
			uint16_t rc = reserve_register(emitter);
			uint16_t c = parse_argument(emitter, expr->right, rc);

			emit_abc_instruction(emitter, expr->expression.line, opcode, reg, swap ? c : b, swap ? b : c);
			free_register(emitter, rc);
		}
	}
}

static bool emit_parameters(LitEmitter* emitter, LitParameters* parameters, uint line) {
	for (uint i = 0; i < parameters->count; i++) {
		LitParameter* parameter = &parameters->values[i];
		uint8_t reg = reserve_register(emitter);

		parameter->reg = reg;

		int index = add_local(emitter, parameter->name, parameter->length, line, false, reg);
		mark_local_initialized(emitter, index);
	}

	return false;
}

static void emit_expression_ignoring_register(LitEmitter* emitter, LitExpression* expression) {
	uint8_t reg = reserve_register(emitter);

	emit_expression(emitter, expression, reg);
	free_register(emitter, reg);
}

static void emit_expression(LitEmitter* emitter, LitExpression* expression, uint8_t reg) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			LitValue value = ((LitLiteralExpression*) expression)->value;

			if (IS_NULL(value)) {
				emit_abc_instruction(emitter, expression->line, OP_LOAD_NULL, reg, 0, 0);
			} else if (IS_BOOL(value)) {
				emit_abc_instruction(emitter, expression->line, OP_LOAD_BOOL, reg, (uint8_t) AS_BOOL(value), 0);
			} else {
				uint16_t constant = add_constant(emitter, expression->line, value);
				SET_BIT(constant, 8);

				emit_abx_instruction(emitter, expression->line, OP_MOVE, reg, constant);
			}

			break;
		}

		case UNARY_EXPRESSION: {
			LitUnaryExpression* expr = (LitUnaryExpression*) expression;
			uint16_t b = parse_argument(emitter, expr->right, reg);

			emit_abc_instruction(emitter, expression->line, translate_unary_operator_into_op(expr->op), reg, b, 0);
			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			switch (expr->op) {
				case LTOKEN_LESS:
				case LTOKEN_LESS_EQUAL:
				case LTOKEN_EQUAL_EQUAL:{
					emit_binary_expression(emitter, expr, reg, false);
					break;
				}

				case LTOKEN_GREATER:
				case LTOKEN_GREATER_EQUAL:{
					emit_binary_expression(emitter, expr, reg, true);
					break;
				}

				case LTOKEN_BANG_EQUAL: {
					emit_binary_expression(emitter, expr, reg, true);
					emit_abc_instruction(emitter, expression->line, OP_NOT, reg, reg, false);

					break;
				}

				default: {
					return emit_binary_expression(emitter, expr, reg, false);
				}
			}

			break;
		}

		case VAR_EXPRESSION: {
			LitVarExpression* expr = (LitVarExpression*) expression;
			bool ref = emitter->emit_reference > 0;

			if (ref) {
				emitter->emit_reference--;
			}

			int index = resolve_local(emitter, emitter->compiler, expr->name, expr->length, expression->line);

			if (index == -1) {
				index = resolve_upvalue(emitter, emitter->compiler, expr->name, expr->length, expression->line);

				if (index == -1) {
					index = resolve_private(emitter, expr->name, expr->length, expression->line);

					if (index == -1) {
						if (ref) {
							NOT_IMPLEMENTED
						} else {
							uint16_t constant = add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));
							emit_abx_instruction(emitter, expression->line, OP_GET_GLOBAL, constant, reg);
						}
					} else {
						if (ref) {
							NOT_IMPLEMENTED
							// emit_op(emitter, expression->line, OP_REFERENCE_PRIVATE);
							// emit_short(emitter, expression->line, index);
						} else {
							emit_abx_instruction(emitter, expression->line, OP_GET_PRIVATE, reg, index);
							// emit_byte_or_short(emitter, expression->line, OP_GET_PRIVATE, OP_GET_PRIVATE_LONG, index);
						}
					}
				} else {
					if (ref) {
						NOT_IMPLEMENTED
					} else {
						emit_abx_instruction(emitter, expression->line, OP_GET_UPVALUE, reg, index);
					}
					// emit_arged_op(emitter, expression->line, ref ? OP_REFERENCE_UPVALUE : OP_GET_UPVALUE, (uint8_t) index);
				}
			} else {
				if (ref) {
					NOT_IMPLEMENTED
					// emit_op(emitter, expression->line, OP_REFERENCE_LOCAL);
					// emit_short(emitter, expression->line, index);
				} else {
					uint16_t r = emitter->compiler->locals.values[index].reg;

					if (reg != r) {
						emit_abx_instruction(emitter, expression->line, OP_MOVE, reg, r);
					}
				}
			}

			break;
		}

		case ASSIGN_EXPRESSION: {
			LitAssignExpression* expr = (LitAssignExpression*) expression;

			if (expr->to->type == VAR_EXPRESSION) {
				LitVarExpression* e = (LitVarExpression*) expr->to;
				int index = resolve_local(emitter, emitter->compiler, e->name, e->length, expr->to->line);

				if (index == -1) {
					uint16_t r = parse_argument(emitter, expr->value, reg);
					index = resolve_upvalue(emitter, emitter->compiler, e->name, e->length, expr->to->line);

					if (index == -1) {
						index = resolve_private(emitter, e->name, e->length, expr->to->line);

						if (index == -1) {
							uint16_t constant = add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length)));
							emit_abx_instruction(emitter, expression->line, OP_SET_GLOBAL, constant, r);
						} else {
							if (emitter->privates.values[index].constant) {
								error(emitter, expression->line, ERROR_CONSTANT_MODIFIED, e->length, e->name);
							}

							if (IS_BIT_SET(r, 8)) {
								SET_BIT(index, 16);
							}

							emit_abx_instruction(emitter, expression->line, OP_SET_PRIVATE, r, index);
						}
					} else {
						emit_abx_instruction(emitter, expression->line, OP_SET_UPVALUE, index, r);
					}

					break;
				} else {
					LitLocal local = emitter->compiler->locals.values[index];

					if (local.constant) {
						error(emitter, expression->line, ERROR_CONSTANT_MODIFIED, e->length, e->name);
					}

					emit_expression(emitter, expr->value, local.reg);
					break;
				}
			} else if (expr->to->type == SUBSCRIPT_EXPRESSION) {
				LitSubscriptExpression* e = (LitSubscriptExpression*) expr->to;
				emit_expression(emitter, e->array, reg);

				uint8_t reg_a = reserve_register(emitter);
				emit_expression(emitter, e->index, reg_a);

				uint8_t reg_b = reserve_register(emitter);
				emit_expression(emitter, expr->value, reg_b);

				emit_abc_instruction(emitter, emitter->last_line, OP_SUBSCRIPT_SET, reg, reg_a, reg_b);
				free_register(emitter, reg_a);
				free_register(emitter, reg_b);

				break;
			}

			error(emitter, expression->line, ERROR_INVALID_ASSIGMENT_TARGET);
			break;
		}

		case CALL_EXPRESSION: {
			LitCallExpression* expr = (LitCallExpression*) expression;
			uint arg_count = expr->args.count;
			uint16_t arg_regs[arg_count];

			bool method = expr->callee->type == GET_EXPRESSION;
			bool super = expr->callee->type == SUPER_EXPRESSION;

			if (method) {
				((LitGetExpression*) expr->callee)->ignore_emit = true;
			} else if (super) {
				((LitSuperExpression*) expr->callee)->ignore_emit = true;
			}

			emit_expression(emitter, expr->callee, reg);

			for (uint i = 0; i < arg_count; i++) {
				uint16_t arg_reg = reserve_register(emitter);

				if (arg_reg != reg + i + 1) {
					UNREACHABLE // Something went terribly wrong
				}

				arg_regs[i] = arg_reg;
				emit_expression(emitter, expr->args.values[i], arg_reg);
			}

			if (method) {
				if (expr->callee->type != GET_EXPRESSION) {
					UNREACHABLE // TODO: replace with a proper error code?
				}

				LitGetExpression *e = (LitGetExpression*) expr->callee;

				int constant = add_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length)));
				emit_abc_instruction(emitter, expression->line, OP_INVOKE, reg, arg_count + 1, constant);
			} else if (super) {
				NOT_IMPLEMENTED
			} else {
				emit_abc_instruction(emitter, expression->line, OP_CALL, reg, arg_count + 1, 1);
			}

			for (uint i = 0; i < arg_count; i++) {
				free_register(emitter, arg_regs[i]);
			}

			if (expr->init == NULL) {
				break;
			}

			LitObjectExpression* init = (LitObjectExpression*) expr->init;
			uint8_t r = reserve_register(emitter);

			for (uint i = 0; i < init->values.count; i++) {
				LitExpression* e = init->values.values[i];
				emitter->last_line = e->line;

				emit_expression(emitter, e, r);
				emit_abc_instruction(emitter, emitter->last_line, OP_PUSH_OBJECT_ELEMENT, reg, add_constant(emitter, emitter->last_line, init->keys.values[i]), r);
			}

			free_register(emitter, r);
			break;
		}

		case GET_EXPRESSION: {
			LitGetExpression* expr = (LitGetExpression*) expression;
			bool ref = emitter->emit_reference > 0;

			if (ref) {
				emitter->emit_reference--;
			}

			bool jump = expr->jump == 0;
			bool emit = !expr->ignore_emit;

			emit_expression(emitter, expr->where, reg);

			if (jump) {
				/*expr->jump = emit_jump(emitter, OP_JUMP_IF_NULL, emitter->last_line);

				if (!expr->ignore_emit) {
					emit_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));
					emit_op(emitter, emitter->last_line, ref ? OP_REFERENCE_FIELD : OP_GET_FIELD);
				}

				patch_jump(emitter, expr->jump, emitter->last_line);*/
				NOT_IMPLEMENTED
			} else if (emit) {
				int constant = add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));

				if (ref) {
					// emit_op(emitter, emitter->last_line, ref ? OP_REFERENCE_FIELD : OP_GET_FIELD);
					NOT_IMPLEMENTED
				}

				emit_abc_instruction(emitter, expression->line, OP_GET_FIELD, reg, reg, constant);
			}

			break;
		}

		case SET_EXPRESSION: {
			LitSetExpression* expr = (LitSetExpression*) expression;
			uint8_t where_reg = reserve_register(emitter);

			emit_expression(emitter, expr->where, where_reg);
			emit_expression(emitter, expr->value, reg);

			int constant = add_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));

			emit_abc_instruction(emitter, emitter->last_line, OP_SET_FIELD, where_reg, constant, reg);
			free_register(emitter, where_reg);

			break;
		}

		case SUBSCRIPT_EXPRESSION: {
			LitSubscriptExpression* expr = (LitSubscriptExpression*) expression;
			emit_expression(emitter, expr->array, reg);

			uint8_t r = reserve_register(emitter);
			emit_expression(emitter, expr->index, r);

			emit_abc_instruction(emitter, expression->line, OP_SUBSCRIPT_GET, reg, r, 0);
			free_register(emitter, r);

			break;
		}

		case ARRAY_EXPRESSION: {
			LitArrayExpression* expr = (LitArrayExpression*) expression;
			uint8_t r = reserve_register(emitter);

			emit_abx_instruction(emitter, expression->line, OP_ARRAY, reg, expr->values.count);

			for (uint i = 0; i < expr->values.count; i++) {
				emit_expression(emitter, expr->values.values[i], r);
				emit_abx_instruction(emitter, emitter->last_line, OP_PUSH_ARRAY_ELEMENT, reg, r);
			}

			free_register(emitter, r);
			break;
		}

		case OBJECT_EXPRESSION: {
			LitObjectExpression* expr = (LitObjectExpression*) expression;
			uint8_t r = reserve_register(emitter);

			emit_abc_instruction(emitter, expression->line, OP_OBJECT, reg, 0, 0);

			for (uint i = 0; i < expr->values.count; i++) {
				emit_expression(emitter, expr->values.values[i], r);
				emit_abc_instruction(emitter, emitter->last_line, OP_PUSH_OBJECT_ELEMENT, reg, add_constant(emitter, emitter->last_line, expr->keys.values[i]), r);
			}

			free_register(emitter, r);
			break;
		}

		case LAMBDA_EXPRESSION: {
			LitLambdaExpression* expr = (LitLambdaExpression*) expression;
			LitString* name = AS_STRING(lit_string_format(emitter->state, "lambda @:@", OBJECT_VALUE(emitter->module->name), lit_number_to_string(emitter->state, expression->line)));

			LitCompiler compiler;
			init_compiler(emitter, &compiler, FUNCTION_REGULAR);

			begin_scope(emitter);
			bool vararg = emit_parameters(emitter, &expr->parameters, expression->line);

			if (expr->body != NULL) {
				bool single_expression = expr->body->type == EXPRESSION_STATEMENT;

				if (single_expression) {
					compiler.skip_return = true;
				}

				if (single_expression) {
					uint8_t r = reserve_register(emitter);

					emit_expression(emitter, ((LitExpressionStatement*) expr->body)->expression, r);
					emit_abc_instruction(emitter, expr->body->line, OP_RETURN, r, 0, 0);
					free_register(emitter, r);
				} else {
					emit_statement(emitter, expr->body);
				}
			}

			end_scope(emitter);

			LitFunction* function = end_compiler(emitter, name);

			function->arg_count = expr->parameters.count;
			function->max_registers += function->arg_count;
			function->vararg = vararg;

			uint16_t function_reg;
			bool closure = function->upvalue_count > 0;

			if (closure) {
				function_reg = reserve_register(emitter);
				LitClosurePrototype* closure_prototype = lit_create_closure_prototype(emitter->state, function);

				for (uint i = 0; i < function->upvalue_count; i++) {
					LitCompilerUpvalue* upvalue = &compiler.upvalues[i];

					closure_prototype->local[i] = upvalue->isLocal;
					closure_prototype->indexes[i] = upvalue->index;
				}

				uint16_t constant_index = add_constant(emitter, expression->line, OBJECT_VALUE(closure_prototype));
				emit_abx_instruction(emitter, expression->line, OP_CLOSURE, function_reg, constant_index);
			} else {
				function_reg = add_constant(emitter, expression->line, OBJECT_VALUE(function));
				SET_BIT(function_reg, 8);
			}

			emit_abc_instruction(emitter, expression->line, OP_MOVE, reg, function_reg, 0);

			if (closure) {
				free_register(emitter, function_reg);
			}

			break;
		}

		case RANGE_EXPRESSION: {
			LitRangeExpression* expr = (LitRangeExpression*) expression;
			emit_expression(emitter, expr->to, reg);

			uint8_t reg_b = reserve_register(emitter);
			emit_expression(emitter, expr->from, reg_b);

			emit_abc_instruction(emitter, expression->line, OP_RANGE, reg, reg_b, reg);
			free_register(emitter, reg_b);

			break;
		}

		default: {
			error(emitter, expression->line, ERROR_UNKNOWN_EXPRESSION, (int) expression->type);
			break;
		}
	}
}

static void patch_loop_jumps(LitEmitter* emitter, LitUInts* breaks) {
	for (uint i = 0; i < breaks->count; i++) {
		patch_instruction(emitter, breaks->values[i], LIT_FORM_ASBX_INSTRUCTION(OP_JUMP, 0, (int64_t) emitter->chunk->count - breaks->values[i] - 1));
	}

	lit_free_uints(emitter->state, breaks);
}

static void emit_statement_scoped(LitEmitter* emitter, LitStatement* statement) {
	begin_scope(emitter);

	if (!emit_statement(emitter, statement)) {
		end_scope(emitter);
	}
}

static bool emit_statement(LitEmitter* emitter, LitStatement* statement) {
	if (statement == NULL) {
		return false;
	}

	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			uint16_t reg = reserve_register(emitter);
			emit_expression(emitter, ((LitExpressionStatement*) statement)->expression, reg);
			free_register(emitter, reg);

			break;
		}

		case BLOCK_STATEMENT: {
			LitStatements* statements = &((LitBlockStatement*) statement)->statements;
			bool ended_scope = false;

			for (uint i = 0; i < statements->count; i++) {
				if (emit_statement(emitter, statements->values[i])) {
					ended_scope = true;

					break;
				}
			}

			if (ended_scope) {
				return true;
			}

			break;
		}

		case VAR_STATEMENT: {
			LitVarStatement* stmt = (LitVarStatement*) statement;
			uint16_t reg = reserve_register(emitter);

			bool private = emitter->compiler->enclosing == NULL && emitter->compiler->scope_depth == -1;

			if (stmt->init == NULL) {
				emit_abc_instruction(emitter, statement->line, OP_LOAD_NULL, reg, 0, 0);
			} else {
				emit_expression(emitter, stmt->init, reg);
			}

			int index = private ?
        resolve_private(emitter, stmt->name, stmt->length, statement->line) :
        add_local(emitter, stmt->name, stmt->length, statement->line, stmt->constant, reg);

			if (private) {
				mark_private_initialized(emitter, index);
				emit_abx_instruction(emitter, statement->line, OP_SET_PRIVATE, reg, index);
				free_register(emitter, reg);
			} else {
				mark_local_initialized(emitter, index);
			}

			break;
		}

		case IF_STATEMENT: {
			LitIfStatement* stmt = (LitIfStatement*) statement;
			uint16_t condition_reg = reserve_register(emitter);

			emit_expression(emitter, stmt->condition, condition_reg);

			uint condition_branch_skip = emit_tmp_instruction(emitter);
			uint else_skip;

			free_register(emitter, condition_reg);

			int64_t start = emitter->chunk->count;
			emit_statement_scoped(emitter, stmt->if_branch);

			if (stmt->else_branch) {
				else_skip = emit_tmp_instruction(emitter);
			}

			patch_instruction(emitter, condition_branch_skip, LIT_FORM_ABX_INSTRUCTION(OP_FALSE_JUMP, condition_reg, (int64_t) emitter->chunk->count - start));

			if (stmt->else_branch) {
				int64_t else_start = emitter->chunk->count;
				emit_statement_scoped(emitter, stmt->else_branch);
				patch_instruction(emitter, else_skip, LIT_FORM_ASBX_INSTRUCTION(OP_JUMP, 0, (int64_t) emitter->chunk->count - else_start));
			}

			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;

			bool export = stmt->exported;
			bool private = !export && emitter->compiler->enclosing == NULL && emitter->compiler->scope_depth == -1;
			bool local = !(export || private);

			int index;
			uint8_t reg = 0;

			if (!export) {
				index = private ?
	        resolve_private(emitter, stmt->name, stmt->length, statement->line) :
	        add_local(emitter, stmt->name, stmt->length, statement->line, false, reg = reserve_register(emitter));
			}

			LitString* name = lit_copy_string(emitter->state, stmt->name, stmt->length);

			if (local) {
				mark_local_initialized(emitter, index);
			} else if (private) {
				mark_private_initialized(emitter, index);
			}

			LitCompiler compiler;

			init_compiler(emitter, &compiler, FUNCTION_REGULAR);

			begin_scope(emitter);
			emit_parameters(emitter, &stmt->parameters, statement->line);

			if (!emit_statement(emitter, stmt->body)) {
				end_scope(emitter);
			}

			LitFunction* function = end_compiler(emitter, name);

			function->arg_count = stmt->parameters.count;
			function->max_registers += function->arg_count;

			uint16_t function_reg;
			bool closure = function->upvalue_count > 0;

			if (closure) {
				function_reg = reserve_register(emitter);
				LitClosurePrototype* closure_prototype = lit_create_closure_prototype(emitter->state, function);

				for (uint i = 0; i < function->upvalue_count; i++) {
					LitCompilerUpvalue* upvalue = &compiler.upvalues[i];

					closure_prototype->local[i] = upvalue->isLocal;
					closure_prototype->indexes[i] = upvalue->index;
				}

				uint16_t constant_index = add_constant(emitter, statement->line, OBJECT_VALUE(closure_prototype));
				emit_abx_instruction(emitter, statement->line, OP_CLOSURE, function_reg, constant_index);
			} else {
				function_reg = add_constant(emitter, statement->line, OBJECT_VALUE(function));
				SET_BIT(function_reg, 8);
			}

			if (export) {
				uint16_t name_constant = add_constant(emitter, statement->line, OBJECT_VALUE(function->name));
				emit_abx_instruction(emitter, statement->line, OP_SET_GLOBAL, name_constant, function_reg);
			} else if (private) {
				if (!closure) {
					SET_BIT(index, 16);
				}

				emit_abx_instruction(emitter, statement->line, OP_SET_PRIVATE, function_reg, index);
			} else {
				emit_abc_instruction(emitter, statement->line, OP_MOVE, reg, function_reg, 0);
			}

			if (closure) {
				free_register(emitter, function_reg);
			}

			break;
		}

		case RETURN_STATEMENT: {
			LitReturnStatement* stmt = (LitReturnStatement*) statement;
			uint8_t reg = reserve_register(emitter);

			if (stmt->expression == NULL) {
				emit_abc_instruction(emitter, statement->line, OP_LOAD_NULL, reg, 0, 0);
			} else {
				emit_expression(emitter, stmt->expression, reg);
			}

			end_scope(emitter);
			emit_abc_instruction(emitter, statement->line, OP_RETURN, reg, 0, 0);
			free_register(emitter, reg);

			return true;
		}

		case WHILE_STATEMENT: {
			LitWhileStatement* stmt = (LitWhileStatement*) statement;

			uint8_t reg = reserve_register(emitter);
			uint before_condition = emit_tmp_instruction(emitter);

			emitter->loop_start = before_condition;
			emitter->compiler->loop_depth++;

			emit_expression(emitter, stmt->condition, reg);

			uint tmp_instruction = emit_tmp_instruction(emitter);
			emit_statement_scoped(emitter, stmt->body);

			patch_loop_jumps(emitter, &emitter->continues);
			emit_asbx_instruction(emitter, statement->line, OP_JUMP, 0, (int) before_condition - emitter->chunk->count - 1);
			patch_instruction(emitter, tmp_instruction, LIT_FORM_ABX_INSTRUCTION(OP_FALSE_JUMP, reg, emitter->chunk->count - tmp_instruction - 1));
			patch_loop_jumps(emitter, &emitter->breaks);

			emitter->compiler->loop_depth--;
			free_register(emitter, reg);

			break;
		}

		case FOR_STATEMENT: {
			LitForStatement* stmt = (LitForStatement*) statement;

			if (stmt->c_style) {
				if (stmt->var != NULL) {
					emit_statement(emitter, stmt->var);
				} else if (stmt->init != NULL) {
					emit_expression_ignoring_register(emitter, stmt->init);
				}

				uint start = emitter->chunk->count;
				uint exit_jump = 0;
				uint8_t condition_reg = 0;

				if (stmt->condition != NULL) {
					condition_reg = reserve_register(emitter);
					emit_expression(emitter, stmt->condition, condition_reg);
					exit_jump = emit_tmp_instruction(emitter);
				}

				if (stmt->increment != NULL) {
					uint body_jump = emit_tmp_instruction(emitter);
					uint increment_start = emitter->chunk->count;

					emit_expression_ignoring_register(emitter, stmt->increment);
					emit_asbx_instruction(emitter, statement->line, OP_JUMP, 0, (int) start - emitter->chunk->count - 1);

					start = increment_start;
					patch_instruction(emitter, body_jump, LIT_FORM_ASBX_INSTRUCTION(OP_JUMP, 0, (int64_t) emitter->chunk->count - body_jump - 1));
				}

				emitter->loop_start = start;
				bool ended_scope = false;

				begin_scope(emitter);

				if (stmt->body != NULL) {
					if (stmt->body->type == BLOCK_STATEMENT) {
						LitStatements *statements = &((LitBlockStatement*) stmt->body)->statements;

						for (uint i = 0; i < statements->count; i++) {
							if (emit_statement(emitter, statements->values[i])) {
								ended_scope = true;
								break;
							}
						}
					} else {
						ended_scope = emit_statement(emitter, stmt->body);
					}
				}

				patch_loop_jumps(emitter, &emitter->continues);

				if (!ended_scope) {
					end_scope(emitter);
				}

				emit_asbx_instruction(emitter, statement->line, OP_JUMP, 0, (int) start - emitter->chunk->count - 1);

				if (stmt->condition != NULL) {
					int a = emitter->chunk->count;
					patch_instruction(emitter, exit_jump, LIT_FORM_ABX_INSTRUCTION(OP_FALSE_JUMP, condition_reg, (int64_t) emitter->chunk->count - exit_jump - 1));
					free_register(emitter, condition_reg);
				}
			} else {
				// TODO: implement
				NOT_IMPLEMENTED
			}

			break;
		}

		case BREAK_STATEMENT: {
			if (emitter->compiler->loop_depth == 0) {
				error(emitter, statement->line, ERROR_LOOP_JUMP_MISSUSE, "break");
			}

			lit_uints_write(emitter->state, &emitter->breaks, emit_tmp_instruction(emitter));
			break;
		}

		case CONTINUE_STATEMENT: {
			if (emitter->compiler->loop_depth == 0) {
				error(emitter, statement->line, ERROR_LOOP_JUMP_MISSUSE, "continue");
			}

			lit_uints_write(emitter->state, &emitter->continues, emit_tmp_instruction(emitter));
			break;
		}

		case CLASS_STATEMENT: {
			LitClassStatement* stmt = (LitClassStatement*) statement;
			bool has_parent = stmt->parent != NULL;
			uint16_t b = 0;

			emitter->class_name = stmt->name;

			if (has_parent) {
				uint16_t constant = add_constant(emitter, statement->line, OBJECT_VALUE(stmt->parent));

				b = reserve_register(emitter);
				emit_abx_instruction(emitter, statement->line, OP_GET_GLOBAL, constant, b);
			}

			int name_constant = add_constant(emitter, emitter->last_line, OBJECT_VALUE(stmt->name));
			uint8_t class_register = reserve_register(emitter);
			emitter->class_register = class_register;

			emit_abc_instruction(emitter, statement->line, OP_CLASS, name_constant, has_parent ? b + 1 : 0, class_register);

			if (has_parent) {
				free_register(emitter, b);
				emitter->class_has_super = true;

				begin_scope(emitter);

				uint8_t super = add_local(emitter, "super", 5, emitter->last_line, false, reserve_register(emitter));
				mark_local_initialized(emitter, super);
			}

			for (uint i = 0; i < stmt->fields.count; i++) {
				LitStatement* s = stmt->fields.values[i];

				if (s->type == VAR_STATEMENT) {
					LitVarStatement* var = (LitVarStatement*) s;
					uint8_t reg = reserve_register(emitter);

					emit_expression(emitter, var->init, reg);
					int field_name_constant = add_constant(emitter, statement->line, OBJECT_VALUE(lit_copy_string(emitter->state, var->name, var->length)));

					emit_abc_instruction(emitter, s->line, OP_STATIC_FIELD, class_register, field_name_constant, reg);
					free_register(emitter, reg);
				} else {
					emit_statement(emitter, s);
				}
			}

			if (stmt->parent != NULL) {
				end_scope(emitter);
			}

			free_register(emitter, class_register);

			emitter->class_name = NULL;
			emitter->class_has_super = false;

			break;
		}

		case METHOD_STATEMENT: {
			LitMethodStatement* stmt = (LitMethodStatement*) statement;
			bool constructor = stmt->name->length == 11 && memcmp(stmt->name->chars, "constructor", 11) == 0;

			if (constructor && stmt->is_static) {
				error(emitter, statement->line, ERROR_STATIC_CONSTRUCTOR);
			}

			LitCompiler compiler;
			init_compiler(emitter, &compiler, constructor ? FUNCTION_CONSTRUCTOR : (stmt->is_static ? FUNCTION_STATIC_METHOD : FUNCTION_METHOD));

			begin_scope(emitter);

			bool vararg = emit_parameters(emitter, &stmt->parameters, statement->line);

			if (!emit_statement(emitter, stmt->body)) {
				end_scope(emitter);
			}

			LitFunction* function = end_compiler(emitter, AS_STRING(lit_string_format(emitter->state, "@:@", OBJECT_VALUE(emitter->class_name), stmt->name)));
			function->arg_count = stmt->parameters.count;
			function->max_registers += function->arg_count;
			function->vararg = vararg;

			uint16_t function_reg;
			bool closure = function->upvalue_count > 0;

			if (closure) {
				function_reg = reserve_register(emitter);
				LitClosurePrototype* closure_prototype = lit_create_closure_prototype(emitter->state, function);

				for (uint i = 0; i < function->upvalue_count; i++) {
					LitCompilerUpvalue* upvalue = &compiler.upvalues[i];

					closure_prototype->local[i] = upvalue->isLocal;
					closure_prototype->indexes[i] = upvalue->index;
				}

				uint16_t constant_index = add_constant(emitter, statement->line, OBJECT_VALUE(closure_prototype));
				emit_abx_instruction(emitter, statement->line, OP_CLOSURE, function_reg, constant_index);
			} else {
				function_reg = add_constant(emitter, statement->line, OBJECT_VALUE(function));
				SET_BIT(function_reg, 8);
			}

			int field_name_constant = add_constant(emitter, statement->line, OBJECT_VALUE(stmt->name));
			emit_abc_instruction(emitter, statement->line, stmt->is_static ? OP_STATIC_FIELD : OP_METHOD, emitter->class_register, field_name_constant, function_reg);

			if (closure) {
				free_register(emitter, function_reg);
			}

			break;
		}

		default: {
			error(emitter, statement->line, ERROR_UNKNOWN_STATEMENT, (int) statement->type);
			break;
		}
	}

	return false;
}

LitModule* lit_emit(LitEmitter* emitter, LitStatements* statements, LitString* module_name) {
	emitter->last_line = 1;
	emitter->emit_reference = 0;

	LitState* state = emitter->state;

	LitValue module_value;
	LitModule* module;

	bool new = false;

	if (lit_table_get(&emitter->state->vm->modules->values, module_name, &module_value)) {
		module = AS_MODULE(module_value);
	} else {
		module = lit_create_module(emitter->state, module_name);
		new = true;
	}

	emitter->module = module;
	uint old_privates_count = module->private_count;

	if (old_privates_count > 0) {
		LitPrivates* privates = &emitter->privates;
		privates->count = old_privates_count - 1;

		lit_privates_write(state, privates, (LitPrivate) {
			true, false
		});

		for (uint i = 0; i < old_privates_count; i++) {
			privates->values[i].initialized = true;
		}
	}

	LitCompiler compiler;
	init_compiler(emitter, &compiler, FUNCTION_SCRIPT);

	emitter->chunk = &compiler.function->chunk;

	resolve_statements(emitter, statements);
	begin_scope(emitter);

	bool ended_scope = false;

	for (uint i = 0; i < statements->count; i++) {
		LitStatement* stmt = statements->values[i];

		if (emit_statement(emitter, stmt)) {
			ended_scope = true;
			break;
		}
	}

	if (!ended_scope) {
		end_scope(emitter);
	}

	module->main_function = end_compiler(emitter, module_name);

	if (new) {
		uint total = emitter->privates.count;
		module->privates = LIT_ALLOCATE(emitter->state, LitValue, total);

		for (uint i = 0; i < total; i++) {
			module->privates[i] = NULL_VALUE;
		}
	} else {
		module->privates = LIT_GROW_ARRAY(emitter->state, module->privates, LitValue, old_privates_count, module->private_count);

		for (uint i = old_privates_count; i < module->private_count; i++) {
			module->privates[i] = NULL_VALUE;
		}
	}

	lit_free_privates(emitter->state, &emitter->privates);

	if (lit_is_optimization_enabled(OPTIMIZATION_PRIVATE_NAMES)) {
		lit_free_table(emitter->state, &emitter->module->private_names->values);
	}

	if (new && !state->had_error) {
		lit_table_set(state, &state->vm->modules->values, module_name, OBJECT_VALUE(module));
	}

	module->ran = true;
	return module;
}