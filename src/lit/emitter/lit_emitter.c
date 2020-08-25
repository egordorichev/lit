#include <lit/emitter/lit_emitter.h>
#include <lit/parser/lit_error.h>
#include <lit/mem/lit_mem.h>
#include <lit/debug/lit_debug.h>
#include <lit/vm/lit_object.h>
#include <lit/vm/lit_vm.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/util/lit_table.h>

#include <string.h>

DEFINE_ARRAY(LitPrivates, LitPrivate, privates)
DEFINE_ARRAY(LitLocals, LitLocal, locals)

static bool emit_statement(LitEmitter* emitter, LitStatement* statement);

void lit_init_emitter(LitState* state, LitEmitter* emitter) {
	emitter->state = state;
	emitter->loop_start = 0;
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

static void emit_byte(LitEmitter* emitter, uint16_t line, uint8_t byte) {
	if (line < emitter->last_line) {
		// Egor-fail proofing
		line = emitter->last_line;
	}

	lit_write_chunk(emitter->state, emitter->chunk, byte, line);
	emitter->last_line = line;
}

static const int8_t stack_effects[] = {
	#define OPCODE(_, effect) effect,
	#include <lit/vm/lit_opcodes.h>
	#undef OPCODE
};

static void emit_bytes(LitEmitter* emitter, uint16_t line, uint8_t a, uint8_t b) {
	if (line < emitter->last_line) {
		// Egor-fail proofing
		line = emitter->last_line;
	}

	lit_write_chunk(emitter->state, emitter->chunk, a, line);
	lit_write_chunk(emitter->state, emitter->chunk, b, line);

	emitter->last_line = line;
}

static void emit_op(LitEmitter* emitter, uint16_t line, LitOpCode op) {
	LitCompiler* compiler = emitter->compiler;

	emit_byte(emitter, line, (uint8_t) op);
	compiler->slots += stack_effects[(int) op];

	if (compiler->slots > compiler->function->max_slots) {
		compiler->function->max_slots = compiler->slots;
	}
}

static void emit_ops(LitEmitter* emitter, uint16_t line, LitOpCode a, LitOpCode b) {
	LitCompiler* compiler = emitter->compiler;

	emit_bytes(emitter, line, (uint8_t) a, (uint8_t) b);
	compiler->slots += stack_effects[(int) a] + stack_effects[(int) b];

	if (compiler->slots > compiler->function->max_slots) {
		compiler->function->max_slots = compiler->slots;
	}
}

static void emit_varying_op(LitEmitter* emitter, uint16_t line, LitOpCode op, uint8_t arg) {
	LitCompiler* compiler = emitter->compiler;

	emit_bytes(emitter, line, (uint8_t) op, arg);
	compiler->slots -= arg;

	if (compiler->slots > compiler->function->max_slots) {
		compiler->function->max_slots = compiler->slots;
	}
}

static void emit_arged_op(LitEmitter* emitter, uint16_t line, LitOpCode op, uint8_t arg) {
	LitCompiler* compiler = emitter->compiler;

	emit_bytes(emitter, line, (uint8_t) op, arg);
	compiler->slots += stack_effects[(int) op];

	if (compiler->slots > compiler->function->max_slots) {
		compiler->function->max_slots = compiler->slots;
	}
}

static void emit_short(LitEmitter* emitter, uint16_t line, uint16_t value) {
	emit_bytes(emitter, line, (uint8_t) ((value >> 8) & 0xff), (uint8_t) (value & 0xff));
}

static void emit_byte_or_short(LitEmitter* emitter, uint16_t line, uint8_t a, uint8_t b, uint16_t index) {
	if (index > UINT8_MAX) {
		emit_op(emitter, line, b);
		emit_short(emitter, line, (uint16_t) index);
	} else {
		emit_arged_op(emitter, line, a, (uint8_t) index);
	}
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

static void emit_return(LitEmitter* emitter, uint line) {
	if (emitter->compiler->type == FUNCTION_CONSTRUCTOR) {
		emit_arged_op(emitter, line, OP_GET_LOCAL, 0);
		emit_op(emitter, line, OP_RETURN);
	} else if (emitter->previous_was_expression_statement && emitter->chunk->count > 0) {
		emitter->chunk->count--; // Remove the OP_POP
		emit_op(emitter, line, OP_RETURN);
	} else {
		emit_ops(emitter, line, OP_NULL, OP_RETURN);
	}
}

static LitFunction* end_compiler(LitEmitter* emitter, LitString* name) {
	if (!emitter->compiler->skip_return) {
		emit_return(emitter, emitter->last_line);
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
	lit_disassemble_chunk(&function->chunk, function->name->chars, NULL);
#endif

	return function;
}

static void begin_scope(LitEmitter* emitter) {
	emitter->compiler->scope_depth++;
}

static void end_scope(LitEmitter* emitter, uint16_t line) {
	emitter->compiler->scope_depth--;

	LitCompiler* compiler = emitter->compiler;
	LitLocals* locals = &compiler->locals;

	while (locals->count > 0 && locals->values[locals->count - 1].depth > compiler->scope_depth) {
		if (locals->values[locals->count - 1].captured) {
			emit_op(emitter, line, OP_CLOSE_UPVALUE);
		} else {
			emit_op(emitter, line, OP_POP);
		}

		locals->count--;
	}
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

	if (constant < UINT8_MAX) {
		emit_arged_op(emitter, line, OP_CONSTANT, constant);
	} else if (constant < UINT16_MAX) {
		emit_op(emitter, line, OP_CONSTANT_LONG);
		emit_short(emitter, line, constant);
	} else {
		error(emitter, line, ERROR_TOO_MANY_CONSTANTS);
	}

	return constant;
}

static int add_private(LitEmitter* emitter, const char* name, uint length, uint line, bool constant) {
	LitPrivates* privates = &emitter->privates;

	if (privates->count == UINT16_MAX) {
		error(emitter, line, ERROR_TOO_MANY_PRIVATES);
	}

	LitTable* private_names = &emitter->module->private_names;
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

	return index;
}

static int resolve_private(LitEmitter* emitter, const char* name, uint length, uint line) {
	LitTable* private_names = &emitter->module->private_names;
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

static uint emit_jump(LitEmitter* emitter, LitOpCode code, uint line) {
	emit_op(emitter, line, code);
	emit_bytes(emitter, line, 0xff, 0xff);

	return emitter->chunk->count - 2;
}

static void patch_jump(LitEmitter* emitter, uint offset, uint line) {
	uint jump = emitter->chunk->count - offset - 2;

	if (jump > UINT16_MAX) {
		error(emitter, line, ERROR_JUMP_TOO_BIG);
	}

	emitter->chunk->code[offset] = (jump >> 8) & 0xff;
	emitter->chunk->code[offset + 1] = jump & 0xff;
}

static void emit_loop(LitEmitter* emitter, uint start, uint line) {
	emit_op(emitter, line, OP_JUMP_BACK);
	uint offset = emitter->chunk->count - start + 2;

	if (offset > UINT16_MAX) {
		error(emitter, line, ERROR_JUMP_TOO_BIG);
	}

	emit_short(emitter, line, offset);
}

static void patch_loop_jumps(LitEmitter* emitter, LitUInts* breaks, uint line) {
	for (uint i = 0; i < breaks->count; i++) {
		patch_jump(emitter, breaks->values[i], line);
	}

	lit_free_uints(emitter->state, breaks);
}

static void emit_expression(LitEmitter* emitter, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			LitValue value = ((LitLiteralExpression*) expression)->value;

			if (IS_NUMBER(value) || IS_STRING(value)) {
				emit_constant(emitter, expression->line, value);
			} else if (IS_BOOL(value)) {
				emit_op(emitter, expression->line, AS_BOOL(value) ? OP_TRUE : OP_FALSE);
			} else if (IS_NULL(value)) {
				emit_op(emitter, expression->line, OP_NULL);
			} else {
				UNREACHABLE
			}

			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			emit_expression(emitter, expr->left);
			LitTokenType op = expr->operator;

			if (op == TOKEN_AMPERSAND_AMPERSAND || op == TOKEN_BAR_BAR || op == TOKEN_QUESTION_QUESTION) {
				uint jump = emit_jump(emitter, op == TOKEN_BAR_BAR ? OP_OR : (op == TOKEN_QUESTION_QUESTION ? OP_NULL_OR : OP_AND), emitter->last_line);

				emit_expression(emitter, expr->right);
				patch_jump(emitter, jump, emitter->last_line);

				break;
			}

			emit_expression(emitter, expr->right);

			switch (op) {
				case TOKEN_PLUS: {
					emit_op(emitter, expression->line, OP_ADD);
					break;
				}

				case TOKEN_MINUS: {
					emit_op(emitter, expression->line, OP_SUBTRACT);
					break;
				}

				case TOKEN_STAR: {
					emit_op(emitter, expression->line, OP_MULTIPLY);
					break;
				}

				case TOKEN_STAR_STAR: {
					emit_op(emitter, expression->line, OP_POWER);
					break;
				}

				case TOKEN_SLASH: {
					emit_op(emitter, expression->line, OP_DIVIDE);
					break;
				}

				case TOKEN_SHARP: {
					emit_op(emitter, expression->line, OP_FLOOR_DIVIDE);
					break;
				}

				case TOKEN_PERCENT: {
					emit_op(emitter, expression->line, OP_MOD);
					break;
				}

				case TOKEN_IS: {
					emit_op(emitter, expression->line, OP_IS);
					break;
				}

				case TOKEN_EQUAL_EQUAL: {
					emit_op(emitter, expression->line, OP_EQUAL);
					break;
				}

				case TOKEN_BANG_EQUAL: {
					emit_ops(emitter, expression->line, OP_EQUAL, OP_NOT);
					break;
				}

				case TOKEN_GREATER: {
					emit_op(emitter, expression->line, OP_GREATER);
					break;
				}

				case TOKEN_GREATER_EQUAL: {
					emit_op(emitter, expression->line, OP_GREATER_EQUAL);
					break;
				}

				case TOKEN_LESS: {
					emit_op(emitter, expression->line, OP_LESS);
					break;
				}

				case TOKEN_LESS_EQUAL: {
					emit_op(emitter, expression->line, OP_LESS_EQUAL);
					break;
				}

				case TOKEN_LESS_LESS: {
					emit_op(emitter, expression->line, OP_LSHIFT);
					break;
				}

				case TOKEN_GREATER_GREATER: {
					emit_op(emitter, expression->line, OP_RSHIFT);
					break;
				}

				case TOKEN_BAR: {
					emit_op(emitter, expression->line, OP_BOR);
					break;
				}

				case TOKEN_AMPERSAND: {
					emit_op(emitter, expression->line, OP_BAND);
					break;
				}

				case TOKEN_CARET: {
					emit_op(emitter, expression->line, OP_BXOR);
					break;
				}

				default: {
					UNREACHABLE
				}
			}

			break;
		}

		case UNARY_EXPRESSION: {
			LitUnaryExpression* expr = (LitUnaryExpression*) expression;
			emit_expression(emitter, expr->right);

			switch (expr->operator) {
				case TOKEN_MINUS: {
					emit_op(emitter, expression->line, OP_NEGATE);
					break;
				}

				case TOKEN_BANG: {
					emit_op(emitter, expression->line, OP_NOT);
					break;
				}

				case TOKEN_TILDE: {
					emit_op(emitter, expression->line, OP_BNOT);
					break;
				}

				default: {
					UNREACHABLE
				}
			}

			break;
		}

		case GROUPING_EXPRESSION: {
			emit_expression(emitter, ((LitGroupingExpression*) expression)->child);
			break;
		}

		case VAR_EXPRESSION: {
			LitVarExpression* expr = (LitVarExpression*) expression;
			int index = resolve_local(emitter, emitter->compiler, expr->name, expr->length, expression->line);

			if (index == -1) {
				index = resolve_upvalue(emitter, emitter->compiler, expr->name, expr->length, expression->line);

				if (index == -1) {
					index = resolve_private(emitter, expr->name, expr->length, expression->line);

					if (index == -1) {
						emit_op(emitter, expression->line, OP_GET_GLOBAL);
						emit_short(emitter, expression->line, add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length))));
					} else {
						emit_byte_or_short(emitter, expression->line, OP_GET_PRIVATE, OP_GET_PRIVATE_LONG, index);
					}
				} else {
					emit_arged_op(emitter, expression->line, OP_GET_UPVALUE, (uint8_t) index);
				}
			} else {
				emit_byte_or_short(emitter, expression->line, OP_GET_LOCAL, OP_GET_LOCAL_LONG, index);
			}

			break;
		}

		case ASSIGN_EXPRESSION: {
			LitAssignExpression* expr = (LitAssignExpression*) expression;

			if (expr->to->type == VAR_EXPRESSION) {
				emit_expression(emitter, expr->value);
				LitVarExpression *e = (LitVarExpression*) expr->to;
				int index = resolve_local(emitter, emitter->compiler, e->name, e->length, expr->to->line);

				if (index == -1) {
					index = resolve_upvalue(emitter, emitter->compiler, e->name, e->length, expr->to->line);

					if (index == -1) {
						index = resolve_private(emitter, e->name, e->length, expr->to->line);

						if (index == -1) {
							emit_op(emitter, expression->line, OP_SET_GLOBAL);
							emit_short(emitter, expression->line, add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length))));
						} else {
							if (emitter->privates.values[index].constant) {
								error(emitter, expression->line, ERROR_CONSTANT_MODIFIED, e->length, e->name);
							}

							emit_byte_or_short(emitter, expression->line, OP_SET_PRIVATE, OP_SET_PRIVATE_LONG, index);
						}
					} else {
						emit_arged_op(emitter, expression->line, OP_SET_UPVALUE, (uint8_t) index);
					}

					break;
				} else {
					if (emitter->compiler->locals.values[index].constant) {
						error(emitter, expression->line, ERROR_CONSTANT_MODIFIED, e->length, e->name);
					}

					emit_byte_or_short(emitter, expression->line, OP_SET_LOCAL, OP_SET_LOCAL_LONG, index);
				}
			} else if (expr->to->type == GET_EXPRESSION) {
				emit_expression(emitter, expr->value);
				LitGetExpression *e = (LitGetExpression*) expr->to;

				emit_expression(emitter, e->where);
				emit_expression(emitter, expr->value);
				emit_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length)));

				emit_ops(emitter, emitter->last_line, OP_SET_FIELD, OP_POP);
			} else if (expr->to->type == SUBSCRIPT_EXPRESSION) {
				LitSubscriptExpression *e = (LitSubscriptExpression*) expr->to;

				emit_expression(emitter, e->array);
				emit_expression(emitter, e->index);
				emit_expression(emitter, expr->value);

				emit_op(emitter, emitter->last_line, OP_SUBSCRIPT_SET);
			} else {
				error(emitter, expression->line, ERROR_INVALID_ASSIGMENT_TARGET);
			}

			break;
		}

		case CALL_EXPRESSION: {
			LitCallExpression* expr = (LitCallExpression*) expression;

			bool method = expr->callee->type == GET_EXPRESSION;
			bool super = expr->callee->type == SUPER_EXPRESSION;

			if (method) {
				((LitGetExpression*) expr->callee)->ignore_emit = true;
			} else if (super) {
				((LitSuperExpression*) expr->callee)->ignore_emit = true;
			}

			emit_expression(emitter, expr->callee);

			if (super) {
				emit_arged_op(emitter, expression->line, OP_GET_LOCAL, 0);
			}

			for (uint i = 0; i < expr->args.count; i++) {
				emit_expression(emitter, expr->args.values[i]);
			}

			if (method || super) {
				if (method) {
					LitGetExpression *e = (LitGetExpression*) expr->callee;

					emit_varying_op(emitter, expression->line, ((LitGetExpression*) expr->callee)->ignore_result ? OP_INVOKE_IGNORING : OP_INVOKE, (uint8_t) expr->args.count);
					emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length))));
				} else {
					LitSuperExpression *e = (LitSuperExpression*) expr->callee;

					emit_varying_op(emitter, emitter->last_line, ((LitSuperExpression*) expr->callee)->ignore_result ? OP_INVOKE_SUPER_IGNORING : OP_INVOKE_SUPER, (uint8_t) expr->args.count);
					emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_VALUE(e->method)));
				}
			} else {
				emit_varying_op(emitter, expression->line, OP_CALL, (uint8_t) expr->args.count);
			}

			if (method) {
				LitExpression* get = expr->callee;

				while (get != NULL) {
					if (get->type == GET_EXPRESSION) {
						LitGetExpression* getter = (LitGetExpression*) get;

						if (getter->jump > 0) {
							patch_jump(emitter, getter->jump, emitter->last_line);
						}

						get = getter->where;
					} else if (get->type == SUBSCRIPT_EXPRESSION) {
						get = ((LitSubscriptExpression*) get)->array;
					} else {
						break;
					}
				}
			}

			break;
		}

		case GET_EXPRESSION: {
			LitGetExpression* expr = (LitGetExpression*) expression;
			emit_expression(emitter, expr->where);

			if (expr->jump == 0) {
				expr->jump = emit_jump(emitter, OP_JUMP_IF_NULL, emitter->last_line);

				if (!expr->ignore_emit) {
					emit_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));
					emit_op(emitter, emitter->last_line, OP_GET_FIELD);
				}

				patch_jump(emitter, expr->jump, emitter->last_line);
			} else if (!expr->ignore_emit) {
				emit_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));
				emit_op(emitter, emitter->last_line, OP_GET_FIELD);
			}

			break;
		}

		case SET_EXPRESSION: {
			LitSetExpression* expr = (LitSetExpression*) expression;

			emit_expression(emitter, expr->where);
			emit_expression(emitter, expr->value);
			emit_constant(emitter, emitter->last_line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length)));

			emit_op(emitter, emitter->last_line, OP_SET_FIELD);

			break;
		}

		case LAMBDA_EXPRESSION: {
			LitLambdaExpression* expr = (LitLambdaExpression*) expression;
			LitString* name = AS_STRING(lit_string_format(emitter->state, "lambda @:@", OBJECT_VALUE(emitter->module->name), lit_number_to_string(emitter->state, expression->line)));

			begin_scope(emitter);

			LitCompiler compiler;
			init_compiler(emitter, &compiler, FUNCTION_REGULAR);

			for (uint i = 0; i < expr->parameters.count; i++) {
				LitParameter parameter = expr->parameters.values[i];
				mark_local_initialized(emitter, add_local(emitter, parameter.name, parameter.length, expression->line, false));
			}

			if (expr->body != NULL) {
				bool single_expression = expr->body->type == EXPRESSION_STATEMENT;

				if (single_expression) {
					compiler.skip_return = true;
					((LitExpressionStatement*) expr->body)->pop = false;
				}

				emit_statement(emitter, expr->body);

				if (single_expression) {
					emit_op(emitter, emitter->last_line, OP_RETURN);
				}
			}

			LitFunction* function = end_compiler(emitter, name);
			function->arg_count = expr->parameters.count;
			function->max_slots += function->arg_count;

			if (function->upvalue_count > 0) {
				emit_op(emitter, emitter->last_line, OP_CLOSURE);
				emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_VALUE(function)));

				for (uint i = 0; i < function->upvalue_count; i++) {
					emit_bytes(emitter, emitter->last_line, compiler.upvalues[i].isLocal ? 1 : 0, compiler.upvalues[i].index);
				}
			} else {
				emit_constant(emitter, emitter->last_line, OBJECT_VALUE(function));
			}

			end_scope(emitter, emitter->last_line);
			break;
		}

		case ARRAY_EXPRESSION: {
			LitArrayExpression* expr = (LitArrayExpression*) expression;
			emit_op(emitter, expression->line, OP_ARRAY);

			for (uint i = 0; i < expr->values.count; i++) {
				emit_expression(emitter, expr->values.values[i]);
				emit_op(emitter, emitter->last_line, OP_PUSH_ARRAY_ELEMENT);
			}

			break;
		}

		case MAP_EXPRESSION: {
			LitMapExpression* expr = (LitMapExpression*) expression;
			emit_op(emitter, expression->line, OP_MAP);

			for (uint i = 0; i < expr->values.count; i++) {
				emit_constant(emitter, emitter->last_line, expr->keys.values[i]);
				emit_expression(emitter, expr->values.values[i]);
				emit_op(emitter, emitter->last_line, OP_PUSH_MAP_ELEMENT);
			}

			break;
		}

		case SUBSCRIPT_EXPRESSION: {
			LitSubscriptExpression* expr = (LitSubscriptExpression*) expression;

			emit_expression(emitter, expr->array);
			emit_expression(emitter, expr->index);

			emit_op(emitter, expression->line, OP_SUBSCRIPT_GET);

			break;
		}

		case THIS_EXPRESSION: {
			if (emitter->compiler->type == FUNCTION_STATIC_METHOD) {
				error(emitter, expression->line, ERROR_THIS_MISSUSE, "in static methods");
			}

			emit_arged_op(emitter, expression->line, OP_GET_LOCAL, 0);
			break;
		}

		case SUPER_EXPRESSION: {
			if (emitter->compiler->type == FUNCTION_STATIC_METHOD) {
				error(emitter, expression->line, ERROR_SUPER_MISSUSE, "in static methods");
			} else if (!emitter->class_has_super) {
				error(emitter, expression->line, ERROR_NO_SUPER, emitter->class_name->chars);
			}

			LitSuperExpression* expr = (LitSuperExpression*) expression;

			if (!expr->ignore_emit) {
				emit_arged_op(emitter, expression->line, OP_GET_LOCAL, 0);
				emit_op(emitter, expression->line, OP_GET_SUPER_METHOD);
				emit_short(emitter, expression->line, add_constant(emitter, expression->line, OBJECT_VALUE(expr->method)));
			}

			break;
		}

		case RANGE_EXPRESSION: {
			LitRangeExpression* expr = (LitRangeExpression*) expression;

			emit_expression(emitter, expr->to);
			emit_expression(emitter, expr->from);

			emit_op(emitter, expression->line, OP_RANGE);
			break;
		}

		case IF_EXPRESSION: {
			LitIfExpression* expr = (LitIfExpression*) expression;
			emit_expression(emitter, expr->condition);

			uint64_t else_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, expression->line);
			emit_expression(emitter, expr->if_branch);

			uint64_t end_jump = emit_jump(emitter, OP_JUMP, emitter->last_line);

			patch_jump(emitter, else_jump, expr->else_branch->line);
			emit_expression(emitter, expr->else_branch);

			patch_jump(emitter, end_jump, emitter->last_line);
			break;
		}

		case INTERPOLATION_EXPRESSION: {
			LitInterpolationExpression* expr = (LitInterpolationExpression*) expression;
			emit_op(emitter, expression->line, OP_ARRAY);

			for (uint i = 0; i < expr->expressions.count; i++) {
				emit_expression(emitter, expr->expressions.values[i]);
				emit_op(emitter, emitter->last_line, OP_PUSH_ARRAY_ELEMENT);
			}

			emit_varying_op(emitter, emitter->last_line, OP_INVOKE, 0);
			emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_CONST_STRING(emitter->state, "join")));

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
			LitExpressionStatement* expr = (LitExpressionStatement*) statement;
			emit_expression(emitter, expr->expression);

			if (expr->pop) {
				emit_op(emitter, statement->line, OP_POP);
			}

			break;
		}

		case BLOCK_STATEMENT: {
			LitStatements statements = ((LitBlockStatement*) statement)->statements;
			begin_scope(emitter);

			for (uint i = 0; i < statements.count; i++) {
				LitStatement* stmt = statements.values[i];

				if (emit_statement(emitter, stmt)) {
					break;
				}
			}

			end_scope(emitter, emitter->last_line);
			break;
		}

		case VAR_STATEMENT: {
			LitVarStatement* stmt = (LitVarStatement*) statement;

			uint line = statement->line;
			bool private = emitter->compiler->enclosing == NULL && emitter->compiler->scope_depth == 0;

			int index = private ?
				add_private(emitter, stmt->name, stmt->length, statement->line, stmt->constant) :
				add_local(emitter, stmt->name, stmt->length, statement->line, stmt->constant);

			if (stmt->init == NULL) {
				emit_op(emitter, line, OP_NULL);
			} else {
				emit_expression(emitter, stmt->init);
			}

			if (private) {
				mark_private_initialized(emitter, index);
			} else {
				mark_local_initialized(emitter, index);
			}

			emit_byte_or_short(emitter, statement->line, private ? OP_SET_PRIVATE : OP_SET_LOCAL, private ? OP_SET_PRIVATE_LONG : OP_SET_LOCAL_LONG, index);

			if (private) {
				// Privates don't live on stack, so we need to pop them manually
				emit_op(emitter, statement->line, OP_POP);
			}

			break;
		}

		case IF_STATEMENT: {
			LitIfStatement* stmt = (LitIfStatement*) statement;

			emit_expression(emitter, stmt->condition);

			uint64_t else_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, statement->line);
			emit_statement(emitter, stmt->if_branch);

			uint64_t end_jump = emit_jump(emitter, OP_JUMP, emitter->last_line);
			uint64_t end_jumps[stmt->elseif_branches == NULL ? 0 : stmt->elseif_branches->count];

			if (stmt->elseif_branches != NULL) {
				for (uint i = 0; i < stmt->elseif_branches->count; i++) {
					LitExpression *e = stmt->elseif_conditions->values[i];

					patch_jump(emitter, else_jump, e->line);
					emit_expression(emitter, e);
					else_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, emitter->last_line);
					emit_statement(emitter, stmt->elseif_branches->values[i]);

					end_jumps[i] = emit_jump(emitter, OP_JUMP, emitter->last_line);
				}
			}

			if (stmt->else_branch != NULL) {
				patch_jump(emitter, else_jump, stmt->else_branch->line);
				emit_statement(emitter, stmt->else_branch);
			} else {
				patch_jump(emitter, else_jump, emitter->last_line);
			}

			patch_jump(emitter, end_jump, emitter->last_line);

			if (stmt->elseif_branches != NULL) {
				for (uint i = 0; i < stmt->elseif_branches->count; i++) {
					patch_jump(emitter, end_jumps[i], stmt->elseif_branches->values[i]->line);
				}
			}

			break;
		}

		case WHILE_STATEMENT: {
			LitWhileStatement* stmt = (LitWhileStatement*) statement;

			uint start = emitter->chunk->count;
			emitter->loop_start = start;
			emitter->compiler->loop_depth++;

			emit_expression(emitter, stmt->condition);

			uint64_t exit_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, statement->line);
			emit_statement(emitter, stmt->body);

			patch_loop_jumps(emitter, &emitter->continues, emitter->last_line);
			emit_loop(emitter, start, emitter->last_line);
			patch_jump(emitter, exit_jump, emitter->last_line);
			patch_loop_jumps(emitter, &emitter->breaks, emitter->last_line);
			emitter->compiler->loop_depth--;

			break;
		}

		case FOR_STATEMENT: {
			LitForStatement* stmt = (LitForStatement*) statement;
			begin_scope(emitter);
			emitter->compiler->loop_depth++;

			if (stmt->c_style) {
				if (stmt->var != NULL) {
					emit_statement(emitter, stmt->var);
				} else if (stmt->init != NULL) {
					emit_expression(emitter, stmt->init);
				}

				uint start = emitter->chunk->count;
				uint exit_jump = 0;

				if (stmt->condition != NULL) {
					emit_expression(emitter, stmt->condition);
					exit_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, emitter->last_line);
				}

				if (stmt->increment != NULL) {
					uint body_jump = emit_jump(emitter, OP_JUMP, emitter->last_line);
					uint increment_start = emitter->chunk->count;

					emit_expression(emitter, stmt->increment);
					emit_op(emitter, emitter->last_line, OP_POP);

					emit_loop(emitter, start, emitter->last_line);
					start = increment_start;
					patch_jump(emitter, body_jump, emitter->last_line);
				}

				emitter->loop_start = start;
				begin_scope(emitter);

				if (stmt->body != NULL) {
					if (stmt->body->type == BLOCK_STATEMENT) {
						LitStatements *statements = &((LitBlockStatement*) stmt->body)->statements;

						for (uint i = 0; i < statements->count; i++) {
							emit_statement(emitter, statements->values[i]);
						}
					} else {
						emit_statement(emitter, stmt->body);
					}
				}

				patch_loop_jumps(emitter, &emitter->continues, emitter->last_line);
				end_scope(emitter, emitter->last_line);

				emit_loop(emitter, start, emitter->last_line);

				if (stmt->condition != NULL) {
					patch_jump(emitter, exit_jump, emitter->last_line);
				}
			} else {
				uint sequence = add_local(emitter, "seq ", 4, statement->line, false);
				mark_local_initialized(emitter, sequence);
				emit_expression(emitter, stmt->condition);
				emit_byte_or_short(emitter, emitter->last_line, OP_SET_LOCAL, OP_SET_LOCAL_LONG, sequence);

				uint iterator = add_local(emitter, "iter ", 5, statement->line, false);
				mark_local_initialized(emitter, iterator);
				emit_op(emitter, emitter->last_line, OP_NULL);
				emit_byte_or_short(emitter, emitter->last_line, OP_SET_LOCAL, OP_SET_LOCAL_LONG, iterator);

				uint start = emitter->chunk->count;
				emitter->loop_start = emitter->chunk->count;

				// iter = seq.iterator(iter)
				emit_byte_or_short(emitter, emitter->last_line, OP_GET_LOCAL, OP_GET_LOCAL_LONG, sequence);
				emit_byte_or_short(emitter, emitter->last_line, OP_GET_LOCAL, OP_GET_LOCAL_LONG, iterator);
				emit_varying_op(emitter, emitter->last_line, OP_INVOKE, 1);
				emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_CONST_STRING(emitter->state, "iterator")));
				emit_byte_or_short(emitter, emitter->last_line, OP_SET_LOCAL, OP_SET_LOCAL_LONG, iterator);

				// If iter is null, just get out of the loop
				uint exit_jump = emit_jump(emitter, OP_JUMP_IF_NULL_POPPING, emitter->last_line);

				begin_scope(emitter);

				// var i = seq.iteratorValue(iter)
				LitVarStatement* var = (LitVarStatement*) stmt->var;
				uint local = add_local(emitter, var->name, var->length, statement->line, false);
				mark_local_initialized(emitter, local);

				emit_byte_or_short(emitter, emitter->last_line, OP_GET_LOCAL, OP_GET_LOCAL_LONG, sequence);
				emit_byte_or_short(emitter, emitter->last_line, OP_GET_LOCAL, OP_GET_LOCAL_LONG, iterator);
				emit_varying_op(emitter, emitter->last_line, OP_INVOKE, 1);
				emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_CONST_STRING(emitter->state, "iteratorValue")));
				emit_byte_or_short(emitter, emitter->last_line, OP_SET_LOCAL, OP_SET_LOCAL_LONG, local);

				if (stmt->body != NULL) {
					if (stmt->body->type == BLOCK_STATEMENT) {
						LitStatements *statements = &((LitBlockStatement*) stmt->body)->statements;

						for (uint i = 0; i < statements->count; i++) {
							emit_statement(emitter, statements->values[i]);
						}
					} else {
						emit_statement(emitter, stmt->body);
					}
				}

				patch_loop_jumps(emitter, &emitter->continues, emitter->last_line);
				end_scope(emitter, emitter->last_line);
				emit_loop(emitter, start, emitter->last_line);
				patch_jump(emitter, exit_jump, emitter->last_line);
			}

			patch_loop_jumps(emitter, &emitter->breaks, emitter->last_line);
			end_scope(emitter, emitter->last_line);
			emitter->compiler->loop_depth--;

			break;
		}

		case CONTINUE_STATEMENT: {
			if (emitter->compiler->loop_depth == 0) {
				error(emitter, statement->line, ERROR_LOOP_JUMP_MISSUSE, "continue");
			}

			lit_uints_write(emitter->state, &emitter->continues, emit_jump(emitter, OP_JUMP, statement->line));
			break;
		}

		case BREAK_STATEMENT: {
			if (emitter->compiler->loop_depth == 0) {
				error(emitter, statement->line, ERROR_LOOP_JUMP_MISSUSE, "break");
			}

			emit_op(emitter, statement->line, OP_POP_LOCALS);

			int depth = emitter->compiler->scope_depth;
			uint16_t local_count = 0;

			LitLocals* locals = &emitter->compiler->locals;

			for (int i = locals->count - 1; i >= 0; i--) {
				LitLocal* local = &locals->values[i];

				if (local->depth < depth) {
					break;
				}

				if (!local->captured) {
					local_count++;
				}
			}

			emit_short(emitter, statement->line, local_count);
			lit_uints_write(emitter->state, &emitter->breaks, emit_jump(emitter, OP_JUMP, statement->line));
			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;

			bool export = stmt->export;
			bool private = !export && emitter->compiler->enclosing == NULL && emitter->compiler->scope_depth == 0;
			bool local = !(export || private);

			int index;

			if (!export) {
				index = private ?
					add_private(emitter, stmt->name, stmt->length, statement->line, false) :
					add_local(emitter, stmt->name, stmt->length, statement->line, false);
			}

			LitString* name = lit_copy_string(emitter->state, stmt->name, stmt->length);

			if (local) {
				mark_local_initialized(emitter, index);
			} else if (private) {
				mark_private_initialized(emitter, index);
			}

			begin_scope(emitter);

			LitCompiler compiler;
			init_compiler(emitter, &compiler, FUNCTION_REGULAR);

			for (uint i = 0; i < stmt->parameters.count; i++) {
				LitParameter parameter = stmt->parameters.values[i];
				mark_local_initialized(emitter, add_local(emitter, parameter.name, parameter.length, statement->line, false));
			}

			emit_statement(emitter, stmt->body);

			LitFunction* function = end_compiler(emitter, name);
			function->arg_count = stmt->parameters.count;
			function->max_slots += function->arg_count;

			if (function->upvalue_count > 0) {
				emit_op(emitter, emitter->last_line, OP_CLOSURE);
				emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_VALUE(function)));

				for (uint i = 0; i < function->upvalue_count; i++) {
					emit_bytes(emitter, emitter->last_line, compiler.upvalues[i].isLocal ? 1 : 0, compiler.upvalues[i].index);
				}
			} else {
				emit_constant(emitter, emitter->last_line, OBJECT_VALUE(function));
			}

			if (export) {
				emit_op(emitter, emitter->last_line, OP_SET_GLOBAL);
				emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_VALUE(name)));
			} else if (private) {
				emit_byte_or_short(emitter, emitter->last_line, OP_SET_PRIVATE, OP_SET_PRIVATE_LONG, index);
			} else {
				emit_byte_or_short(emitter, emitter->last_line, OP_SET_LOCAL, OP_SET_LOCAL_LONG, index);
			}

			emit_op(emitter, emitter->last_line, OP_POP);
			end_scope(emitter, emitter->last_line);

			break;
		}

		case RETURN_STATEMENT: {
			if (emitter->compiler->type == FUNCTION_CONSTRUCTOR) {
				error(emitter, statement->line, ERROR_RETURN_FROM_CONSTRUCTOR);
			}

			LitExpression* expression = ((LitReturnStatement*) statement)->expression;

			if (expression == NULL) {
				emit_op(emitter, emitter->last_line, OP_NULL);
			} else {
				emit_expression(emitter, expression);
			}

			emit_op(emitter, emitter->last_line, OP_RETURN);

			if (emitter->compiler->scope_depth == 0) {
				emitter->compiler->skip_return = true;
			}

			return true;
		}

		case METHOD_STATEMENT: {
			LitMethodStatement* stmt = (LitMethodStatement*) statement;
			bool constructor = stmt->name->length == 11 && memcmp(stmt->name->chars, "constructor", 11) == 0;

			if (constructor && stmt->is_static) {
				error(emitter, statement->line, ERROR_STATIC_CONSTRUCTOR);
			}

			begin_scope(emitter);

			LitCompiler compiler;
			init_compiler(emitter, &compiler, constructor ? FUNCTION_CONSTRUCTOR : (stmt->is_static ? FUNCTION_STATIC_METHOD : FUNCTION_METHOD));

			for (uint i = 0; i < stmt->parameters.count; i++) {
				LitParameter parameter = stmt->parameters.values[i];
				mark_local_initialized(emitter, add_local(emitter, parameter.name, parameter.length, statement->line, false));
			}

			emit_statement(emitter, stmt->body);

			LitFunction* function = end_compiler(emitter, AS_STRING(lit_string_format(emitter->state, "@:@", emitter->class_name, stmt->name)));
			function->arg_count = stmt->parameters.count;
			function->max_slots += function->arg_count;

			emit_constant(emitter, emitter->last_line, OBJECT_VALUE(function));

			emit_op(emitter, emitter->last_line, stmt->is_static ? OP_STATIC_FIELD : OP_METHOD);
			emit_short(emitter, emitter->last_line, add_constant(emitter, statement->line, OBJECT_VALUE(stmt->name)));

			end_scope(emitter, emitter->last_line);

			break;
		}

		case CLASS_STATEMENT: {
			LitClassStatement* stmt = (LitClassStatement*) statement;
			emitter->class_name = stmt->name;

			emit_constant(emitter, statement->line, OBJECT_VALUE(stmt->name));
			emit_op(emitter, statement->line, OP_CLASS);

			if (stmt->parent != NULL) {
				emit_op(emitter, emitter->last_line, OP_GET_GLOBAL);
				emit_short(emitter, emitter->last_line, add_constant(emitter, emitter->last_line, OBJECT_VALUE(stmt->parent)));
				emit_op(emitter, emitter->last_line, OP_INHERIT);

				emitter->class_has_super = true;
			}

			for (uint i = 0; i < stmt->fields.count; i++) {
				LitStatement* s = stmt->fields.values[i];

				if (s->type == VAR_STATEMENT) {
					LitVarStatement* var = (LitVarStatement*) s;

					emit_expression(emitter, var->init);

					emit_op(emitter, statement->line, OP_STATIC_FIELD);
					emit_short(emitter, statement->line, add_constant(emitter, statement->line, OBJECT_VALUE(lit_copy_string(emitter->state, var->name, var->length))));
				} else {
					emit_statement(emitter, s);
				}
			}

			emit_op(emitter, emitter->last_line, OP_POP);
			emitter->class_name = NULL;
			emitter->class_has_super = false;

			break;
		}

		case FIELD_STATEMENT: {
			LitFieldStatement* stmt = (LitFieldStatement*) statement;
			LitFunction* getter = NULL;
			LitFunction* setter = NULL;

			if (stmt->getter != NULL) {
				begin_scope(emitter);

				LitCompiler compiler;
				init_compiler(emitter, &compiler, stmt->is_static ? FUNCTION_STATIC_METHOD : FUNCTION_METHOD);
				emit_statement(emitter, stmt->getter);
				getter = end_compiler(emitter, AS_STRING(lit_string_format(emitter->state, "@:get @", emitter->class_name, stmt->name)));

				end_scope(emitter, emitter->last_line);
			}

			if (stmt->setter != NULL) {
				begin_scope(emitter);

				LitCompiler compiler;
				init_compiler(emitter, &compiler, stmt->is_static ? FUNCTION_STATIC_METHOD : FUNCTION_METHOD);
				mark_local_initialized(emitter, add_local(emitter, "value", 5, statement->line, false));

				emit_statement(emitter, stmt->setter);
				setter = end_compiler(emitter, AS_STRING(lit_string_format(emitter->state, "@:set @", emitter->class_name, stmt->name)));
				setter->arg_count = 1;
				setter->max_slots++;

				end_scope(emitter, emitter->last_line);
			}

			LitField* field = lit_create_field(emitter->state, (LitObject*) getter, (LitObject*) setter);
			emit_constant(emitter, statement->line, OBJECT_VALUE(field));
			emit_op(emitter, statement->line, stmt->is_static ? OP_STATIC_FIELD : OP_DEFINE_FIELD);
			emit_short(emitter, statement->line, add_constant(emitter, statement->line, OBJECT_VALUE(stmt->name)));

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
	LitState* state = emitter->state;

	LitValue module_value;
	LitModule* module;

	bool new = false;

	if (lit_table_get(&emitter->state->vm->modules, module_name, &module_value)) {
		module = AS_MODULE(module_value);
	} else {
		module = lit_create_module(emitter->state, module_name);
		new = true;
	}

	emitter->module = module;
	uint old_privates_count = module->private_names.count;

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

	for (uint i = 0; i < statements->count; i++) {
		LitStatement* stmt = statements->values[i];

		if (emit_statement(emitter, stmt)) {
			break;
		}
	}

	end_scope(emitter, emitter->last_line);
	module->main_function = end_compiler(emitter, module_name);

	if (new) {
		module->privates = LIT_ALLOCATE(emitter->state, LitValue, emitter->privates.count);
	} else {
		module->privates = LIT_GROW_ARRAY(emitter->state, module->privates, LitValue, old_privates_count, module->private_names.count);
	}

	lit_free_privates(emitter->state, &emitter->privates);

	if (new && !state->had_error) {
		lit_table_set(state, &state->vm->modules, module_name, OBJECT_VALUE(module));
	}

	module->ran = true;
	return module;
}