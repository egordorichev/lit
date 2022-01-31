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
	emitter->previous_was_expression_statement = false;
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

	compiler->registers_used -= 1;
}

static void init_compiler(LitEmitter* emitter, LitCompiler* compiler, LitFunctionType type) {
	lit_init_locals(&compiler->locals);

	compiler->type = type;
	compiler->scope_depth = 0;
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
	if (!emitter->compiler->skip_return) {
		emit_abc_instruction(emitter, emitter->last_line, OP_RETURN, 0, 1, 0);
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
	emitter->compiler->scope_depth--;

	LitCompiler* compiler = emitter->compiler;
	LitLocals* locals = &compiler->locals;

	while (locals->count > 0 && locals->values[locals->count - 1].depth > compiler->scope_depth) {
		free_register(emitter, locals->values[locals->count - 1].reg);
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

static void mark_local_initialized(LitEmitter* emitter, uint index) {
	emitter->compiler->locals.values[index].depth = emitter->compiler->scope_depth;
}

static void mark_private_initialized(LitEmitter* emitter, uint index) {
	emitter->privates.values[index].initialized = true;
}

static void resolve_statement(LitEmitter* emitter, LitStatement* statement) {
	switch (statement->type) {
		default: {
			break;
		}
	}
}

static LitOpCode translate_unary_operator_into_op(LitTokenType token) {
	switch (token) {
		case LTOKEN_MINUS: return OP_NEGATE;
		case LTOKEN_BANG: default: return OP_NOT;
	}
}

static LitOpCode translate_binary_operator_into_op(LitTokenType token) {
	switch (token) {
		case LTOKEN_BANG_EQUAL: case LTOKEN_EQUAL_EQUAL: return OP_EQUAL;
		case LTOKEN_LESS: case LTOKEN_GREATER: return OP_LESS;
		case LTOKEN_LESS_EQUAL: case LTOKEN_GREATER_EQUAL: return OP_LESS_EQUAL;

		case LTOKEN_MINUS: return OP_SUBTRACT;
		case LTOKEN_STAR: return OP_MULTIPLY;
		case LTOKEN_SLASH: return OP_DIVIDE;

		case LTOKEN_PLUS: default: return OP_ADD;
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

		// TODO: privates, globals, etc etc etc
	}

	emit_expression(emitter, expression, reg);
	return reg;
}

static void emit_binary_expression(LitEmitter* emitter, LitBinaryExpression* expr, uint8_t reg, bool swap) {
	uint16_t rc = reserve_register(emitter);

	uint16_t b = parse_argument(emitter, expr->left, reg);
	uint16_t c = parse_argument(emitter, expr->right, rc);

	emit_abc_instruction(emitter, expr->expression.line, translate_binary_operator_into_op(expr->op), reg, swap ? c : b, swap ? b : c);
	free_register(emitter, rc);
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
				// TODO: when implementing this, also implement in parse_argument()
				// index = resolve_upvalue(emitter, emitter->compiler, expr->name, expr->length, expression->line);

				if (index == -1) {
					// index = resolve_private(emitter, expr->name, expr->length, expression->line);

					if (index == -1) {
						uint16_t constant = add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));
						emit_abx_instruction(emitter, expression->line, OP_GET_GLOBAL, constant, reg);
					} else {
						if (ref) {
							// emit_op(emitter, expression->line, OP_REFERENCE_PRIVATE);
							// emit_short(emitter, expression->line, index);
						} else {
							// emit_byte_or_short(emitter, expression->line, OP_GET_PRIVATE, OP_GET_PRIVATE_LONG, index);
						}
					}
				} else {
					// emit_arged_op(emitter, expression->line, ref ? OP_REFERENCE_UPVALUE : OP_GET_UPVALUE, (uint8_t) index);
				}
			} else {
				if (ref) {
					// emit_op(emitter, expression->line, OP_REFERENCE_LOCAL);
					// emit_short(emitter, expression->line, index);
				} else {
					uint16_t r = emitter->compiler->locals.values[index].reg;

					if (reg != r) {
						SET_BIT(r, 9); // Mark as ignored upon register cleanup
						emit_abx_instruction(emitter, expression->line, OP_MOVE, reg, r);
					}
				}
			}

			break;
		}

		case ASSIGN_EXPRESSION: {
			LitAssignExpression* expr = (LitAssignExpression*) expression;

			if (expr->to->type == VAR_EXPRESSION) {
				LitVarExpression *e = (LitVarExpression *) expr->to;
				int index = resolve_local(emitter, emitter->compiler, e->name, e->length, expr->to->line);

				if (index == -1) {
					uint16_t r = parse_argument(emitter, expr->value, reg);
					// index = resolve_upvalue(emitter, emitter->compiler, e->name, e->length, expr->to->line);

					if (index == -1) {
						// index = resolve_private(emitter, e->name, e->length, expr->to->line);

						if (index == -1) {
							uint16_t constant = add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length)));
							emit_abx_instruction(emitter, expression->line, OP_SET_GLOBAL, constant, r);
						} else {
							if (emitter->privates.values[index].constant) {
								error(emitter, expression->line, ERROR_CONSTANT_MODIFIED, e->length, e->name);
							}

							// emit_byte_or_short(emitter, expression->line, OP_SET_PRIVATE, OP_SET_PRIVATE_LONG, index);
						}
					} else {
						// emit_arged_op(emitter, expression->line, OP_SET_UPVALUE, (uint8_t) index);
					}

					break;
				} else {
					LitLocal local = emitter->compiler->locals.values[index];

					if (local.constant) {
						error(emitter, expression->line, ERROR_CONSTANT_MODIFIED, e->length, e->name);
					}

					uint16_t r = local.reg;
					emit_expression(emitter, expr->value, r + 1);

					SET_BIT(r, 9);
					break;
				}
			}

			error(emitter, expression->line, ERROR_INVALID_ASSIGMENT_TARGET);
			break;
		}

		case CALL_EXPRESSION: {
			LitCallExpression* expr = (LitCallExpression*) expression;
			uint arg_count = expr->args.count;
			uint16_t arg_regs[arg_count];

			emit_expression(emitter, expr->callee, reg);

			for (uint i = 0; i < arg_count; i++) {
				uint16_t arg_reg = reserve_register(emitter);

				if (arg_reg != reg + i + 1) {
					UNREACHABLE // Something went terribly wrong, can't put
				}

				arg_regs[i] = arg_reg;
				emit_expression(emitter, expr->args.values[i], arg_reg);
			}

			emit_abc_instruction(emitter, expression->line, OP_CALL, reg, arg_count + 1, 1);

			for (uint i = 0; i < arg_count; i++) {
				free_register(emitter, arg_regs[i]);
			}

			break;
		}

		default: {
			error(emitter, expression->line, ERROR_UNKNOWN_EXPRESSION, (int) expression->type);
			break;
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

static void free_parameter_registers(LitEmitter* emitter, LitParameters* parameters) {
	for (uint i = 0; i < parameters->count; i++) {
		free_register(emitter, parameters->values[i].reg);
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
			begin_scope(emitter);

			for (uint i = 0; i < statements->count; i++) {
				if (emit_statement(emitter, statements->values[i])) {
					break;
				}
			}

			end_scope(emitter);
			break;
		}

		case VAR_STATEMENT: {
			LitVarStatement* stmt = (LitVarStatement*) statement;
			uint16_t reg = reserve_register(emitter);

			bool private = false; // emitter->compiler->enclosing == NULL && emitter->compiler->scope_depth == 0;

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
				// emit_byte_or_short(emitter, statement->line, OP_SET_PRIVATE, index);
			} else {
				mark_local_initialized(emitter, index);
				// emit_byte_or_short(emitter, statement->line, private ? OP_SET_PRIVATE : OP_SET_LOCAL, private ? OP_SET_PRIVATE_LONG : OP_SET_LOCAL_LONG, index);
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
			emit_statement(emitter, stmt->if_branch);

			if (stmt->else_branch) {
				else_skip = emit_tmp_instruction(emitter);
			}

			patch_instruction(emitter, condition_branch_skip, LIT_FORM_ASBX_INSTRUCTION(OP_FALSE_JUMP, condition_reg, (int64_t) emitter->chunk->count - start));

			if (stmt->else_branch) {
				int64_t else_start = emitter->chunk->count;
				emit_statement(emitter, stmt->else_branch);
				patch_instruction(emitter, else_skip, LIT_FORM_ASBX_INSTRUCTION(OP_JUMP, 0, (int64_t) emitter->chunk->count - else_start));
			}

			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;
			LitString* name = lit_copy_string(emitter->state, stmt->name, stmt->length);
			LitCompiler compiler;

			init_compiler(emitter, &compiler, FUNCTION_REGULAR);

			begin_scope(emitter);
			emit_parameters(emitter, &stmt->parameters, statement->line);

			emit_statement(emitter, stmt->body);

			free_parameter_registers(emitter, &stmt->parameters);
			end_scope(emitter);

			LitFunction* function = end_compiler(emitter, name);

			function->arg_count = stmt->parameters.count;
			function->max_registers += function->arg_count;

			uint16_t function_constant = add_constant(emitter, statement->line, OBJECT_VALUE(function));
			SET_BIT(function_constant, 8);

			uint16_t name_constant = add_constant(emitter, statement->line, OBJECT_VALUE(function->name));
			emit_abx_instruction(emitter, statement->line, OP_SET_GLOBAL, name_constant, function_constant);

			break;
		}

		default: {
			error(emitter, statement->line, ERROR_UNKNOWN_STATEMENT, (int) statement->type);
			break;
		}
	}

	emitter->previous_was_expression_statement = statement->type == EXPRESSION_STATEMENT;
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

	for (uint i = 0; i < statements->count; i++) {
		LitStatement* stmt = statements->values[i];

		if (emit_statement(emitter, stmt)) {
			break;
		}
	}

	end_scope(emitter);
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