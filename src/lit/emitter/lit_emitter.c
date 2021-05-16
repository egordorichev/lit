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

static void emit_expression(LitEmitter* emitter, LitExpression* expression);
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

static void init_compiler(LitEmitter* emitter, LitCompiler* compiler, LitFunctionType type) {
	lit_init_locals(&compiler->locals);

	compiler->type = type;
	compiler->scope_depth = 0;
	compiler->enclosing = (struct LitCompiler*) emitter->compiler;
	compiler->skip_return = false;
	compiler->function = lit_create_function(emitter->state, emitter->module);
	compiler->loop_depth = 0;

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
			"this", 4, -1, false, false
		});
	} else {
		lit_locals_write(emitter->state, &compiler->locals, (LitLocal) {
			"", 0, -1, false, false
		});
	}

	compiler->slots = 1;
	compiler->max_slots = 1;
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

static void end_scope(LitEmitter* emitter, uint16_t line) {
	emitter->compiler->scope_depth--;

	/*LitCompiler* compiler = emitter->compiler;
	LitLocals* locals = &compiler->locals;

	while (locals->count > 0 && locals->values[locals->count - 1].depth > compiler->scope_depth) {
		if (locals->values[locals->count - 1].captured) {
			emit_op(emitter, line, OP_CLOSE_UPVALUE);
		} else {
			emit_op(emitter, line, OP_POP);
		}

		locals->count--;
	}*/
}

static void error(LitEmitter* emitter, uint line, LitError error, ...) {
	va_list args;
	va_start(args, error);
	lit_error(emitter->state, COMPILE_ERROR, lit_vformat_error(emitter->state, line, error, args)->chars);
	va_end(args);
}

static uint16_t add_constant(LitEmitter* emitter, uint line, LitValue value) {
	uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

	if (constant >= UINT16_MAX) {
		error(emitter, line, ERROR_TOO_MANY_CONSTANTS);
	}

	return constant;
}

static uint emit_constant(LitEmitter* emitter, uint line, LitValue value) {
	uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

	/*if (constant < UINT8_MAX) {
		emit_arged_op(emitter, line, OP_CONSTANT, constant);
	} else if (constant < UINT16_MAX) {
		emit_op(emitter, line, OP_CONSTANT_LONG);
		emit_short(emitter, line, constant);
	} else {
		error(emitter, line, ERROR_TOO_MANY_CONSTANTS);
	}*/

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

static int add_local(LitEmitter* emitter, const char* name, uint length, uint line, bool constant) {
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
		name, length, UINT16_MAX, false, constant
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

static void resolve_statement(LitEmitter* emitter, LitStatement* statement) {
	switch (statement->type) {
		default: {
			break;
		}
	}
}

static void emit_expression(LitEmitter* emitter, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			uint16_t constant = add_constant(emitter, expression->line, ((LitLiteralExpression*) expression)->value);
			emit_abx_instruction(emitter, expression->line, OP_LOADK, 10, constant);

			break;
		}

		default: {
			error(emitter, expression->line, ERROR_UNKNOWN_EXPRESSION, (int) expression->type);
			break;
		}
	}
}

static bool emit_statement(LitEmitter* emitter, LitStatement* statement) {
	if (statement == NULL) {
		return false;
	}

	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			emit_expression(emitter, ((LitExpressionStatement*) statement)->expression);
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

	end_scope(emitter, emitter->last_line);
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