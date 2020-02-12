#include <lit/emitter/lit_emitter.h>
#include <lit/mem/lit_mem.h>
#include <lit/debug/lit_debug.h>
#include <lit/vm/lit_object.h>
#include <lit/scanner/lit_scanner.h>

#include <string.h>

void lit_init_emitter(LitState* state, LitEmitter* emitter) {
	emitter->state = state;
	emitter->loop_start = 0;

	lit_init_uints(&emitter->breaks);
}

void lit_free_emitter(LitEmitter* emitter) {
	lit_free_uints(emitter->state, &emitter->breaks);
}

static void emit_byte(LitEmitter* emitter, uint16_t line, uint8_t byte) {
	lit_write_chunk(emitter->state, emitter->chunk, byte, line);
	emitter->last_line = line;
}

static void emit_bytes(LitEmitter* emitter, uint16_t line, uint8_t a, uint8_t b) {
	lit_write_chunk(emitter->state, emitter->chunk, a, line);
	lit_write_chunk(emitter->state, emitter->chunk, b, line);
}

static void init_compiler(LitEmitter* emitter, LitCompiler* compiler, LitFunctionType type) {
	compiler->type = type;
	compiler->local_count = 0;
	compiler->scope_depth = 0;
	compiler->enclosing = (struct LitCompiler *) emitter->compiler;
	compiler->skip_return = false;
	compiler->function = lit_create_function(emitter->state);

	const char* name = emitter->state->scanner->file_name;

	if (emitter->compiler == NULL) {
		compiler->function->name = lit_copy_string(emitter->state, name, strlen(name));
	}

	emitter->compiler = compiler;
	emitter->chunk = &compiler->function->chunk;

	LitLocal* local = &compiler->locals[compiler->local_count++];
	local->depth = -1;
	local->name = "";
	local->length = 0;
}

static void emit_return(LitEmitter* emitter, uint line) {
	emit_bytes(emitter, line, OP_NULL, OP_RETURN);
}

static LitFunction* end_compiler(LitEmitter* emitter, LitString* name) {
	if (!emitter->compiler->skip_return) {
		emit_return(emitter, emitter->last_line);
		emitter->compiler->skip_return = true;
	}

	LitFunction* function = emitter->compiler->function;

	emitter->compiler = (LitCompiler *) emitter->compiler->enclosing;
	emitter->chunk = emitter->compiler == NULL ? NULL : &emitter->compiler->function->chunk;

	if (name != NULL) {
		function->name = name;
	}

#ifdef LIT_TRACE_CHUNK
	lit_disassemble_chunk(&function->chunk, function->name->chars);
#endif

	return function;
}

static void begin_scope(LitEmitter* emitter) {
	emitter->compiler->scope_depth++;
}

static void end_scope(LitEmitter* emitter, uint16_t line) {
	emitter->compiler->scope_depth--;

	LitCompiler* compiler = emitter->compiler;
	uint count = 0;

	while (compiler->local_count > 0 && compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth) {
		compiler->local_count--;
		count++;
	}

	if (count == 1) {
		emit_byte(emitter, line, OP_POP);
	} else if (count > 0) {
		if (count > UINT8_MAX) {
			lit_error(emitter->state, COMPILE_ERROR, line, "Too many locals popped for one scope");
		}

		emit_bytes(emitter, line, OP_POP_MULTIPLE, (uint8_t) count);
	}
}

static uint8_t add_constant(LitEmitter* emitter, uint line, LitValue value) {
	uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

	if (constant >= UINT8_MAX) {
		lit_error(emitter->state, COMPILE_ERROR, line, "Too many constants in one chunk");
	}

	return constant;
}

static uint emit_constant(LitEmitter* emitter, uint line, LitValue value) {
	uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

	if (constant < UINT8_MAX) {
		emit_bytes(emitter, line, OP_CONSTANT, constant);
	} else if (constant < UINT16_MAX) {
		emit_byte(emitter, line, OP_CONSTANT_LONG);
		emit_bytes(emitter, line, (uint8_t) ((constant >> 8) & 0xff), (uint8_t) (constant & 0xff));
	} else {
		lit_error(emitter->state, COMPILE_ERROR, line, "Too many constants in one chunk");
	}

	return constant;
}

static int add_local(LitEmitter* emitter, const char* name, uint length, uint line) {
	LitCompiler* compiler = emitter->compiler;

	if (compiler->local_count == UINT8_MAX + 1) {
		lit_error(emitter->state, COMPILE_ERROR, line, "Too many local variables in function");
	}

	for (int i = compiler->local_count - 1; i >= 0; i--) {
		LitLocal* local = &compiler->locals[i];

		if (local->depth != UINT16_MAX && local->depth < compiler->scope_depth) {
			break;
		}

		if (name == local->name && length == local->length) {
			lit_error(emitter->state, COMPILE_ERROR, line, "Variable '%.*s' was already declared in this scope", length, name);
		}
	}

	LitLocal* local = &compiler->locals[compiler->local_count++];

	local->name = name;
	local->length = length;
	local->depth = UINT16_MAX;

	return compiler->local_count - 1;
}

static int resolve_local(LitEmitter* emitter, LitCompiler* compiler, const char* name, uint length, uint line) {
	for (int i = compiler->local_count - 1; i >= 0; i--) {
		LitLocal* local = &compiler->locals[i];

		if (local->length == length && memcmp(local->name, name, length) == 0) {
			if (local->depth == UINT16_MAX) {
				lit_error(emitter->state, COMPILE_ERROR, line, "Can't use local '%.*s' in its own initializer", length, name);
			}

			return i;
		}
	}

	if (compiler->enclosing != NULL) {
		return resolve_local(emitter, (LitCompiler *) compiler->enclosing, name, length, line);
	}

	return -1;
}

static void mark_initialized(LitEmitter* emitter, uint index) {
	emitter->compiler->locals[index].depth = emitter->compiler->scope_depth;
}

static uint emit_jump(LitEmitter* emitter, LitOpCode code, uint line) {
	emit_byte(emitter, line, code);
	emit_bytes(emitter, line, 0xff, 0xff);

	return emitter->chunk->count - 2;
}

static void patch_jump(LitEmitter* emitter, uint offset, uint line) {
	uint jump = emitter->chunk->count - offset - 2;

	if (jump > UINT16_MAX) {
		lit_error(emitter->state, COMPILE_ERROR, line, "Too much code to jump over");
	}

	emitter->chunk->code[offset] = (jump >> 8) & 0xff;
	emitter->chunk->code[offset + 1] = jump & 0xff;
}

static void emit_loop(LitEmitter* emitter, uint start, uint line) {
	emit_byte(emitter, line, OP_JUMP_BACK);
	uint offset = emitter->chunk->count - start + 2;

	if (offset > UINT16_MAX) {
		lit_error(emitter->state, COMPILE_ERROR, line, "Loop body is too large");
	}

	emit_bytes(emitter, line, (offset >> 8) & 0xff, offset & 0xff);
}

static void patch_breaks(LitEmitter* emitter, uint line) {
	for (uint i = 0; i < emitter->breaks.count; i++) {
		patch_jump(emitter, emitter->breaks.values[i], line);
	}

	lit_free_uints(emitter->state, &emitter->breaks);
}

static void emit_expression(LitEmitter* emitter, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			LitValue value = ((LitLiteralExpression*) expression)->value;

			if (IS_NUMBER(value) || IS_STRING(value)) {
				emit_constant(emitter, expression->line, value);
			} else if (IS_BOOL(value)) {
				emit_byte(emitter, expression->line, AS_BOOL(value) ? OP_TRUE : OP_FALSE);
			} else if (IS_NULL(value)) {
				emit_byte(emitter, expression->line, OP_NULL);
			} else {
				UNREACHABLE
			}

			break;
		}

		case BINARY_EXPRESSION: {
			LitBinaryExpression* expr = (LitBinaryExpression*) expression;

			emit_expression(emitter, expr->left);

			if (expr->operator == TOKEN_AMPERSAND_AMPERSAND) {
				uint jump = emit_jump(emitter, OP_JUMP_IF_FALSE, emitter->last_line);

				emit_byte(emitter, emitter->last_line, OP_POP);
				emit_expression(emitter, expr->right);

				patch_jump(emitter, jump, emitter->last_line);
				break;
			} else if (expr->operator == TOKEN_BAR_BAR || expr->operator == TOKEN_QUESTION_QUESTION) {
				uint else_jump = emit_jump(emitter, expr->operator == TOKEN_BAR_BAR ? OP_JUMP_IF_FALSE : OP_JUMP_IF_NULL, emitter->last_line);
				uint end_jump = emit_jump(emitter, OP_JUMP, emitter->last_line);

				patch_jump(emitter, else_jump, emitter->last_line);
				emit_byte(emitter, emitter->last_line, OP_POP);

				emit_expression(emitter, expr->right);
				patch_jump(emitter, end_jump, emitter->last_line);

				break;
			}

			emit_expression(emitter, expr->right);

			switch (expr->operator) {
				case TOKEN_PLUS: {
					emit_byte(emitter, expression->line, OP_ADD);
					break;
				}
				case TOKEN_MINUS: {
					emit_byte(emitter, expression->line, OP_SUBTRACT);
					break;
				}

				case TOKEN_STAR: {
					emit_byte(emitter, expression->line, OP_MULTIPLY);
					break;
				}

				case TOKEN_SLASH: {
					emit_byte(emitter, expression->line, OP_DIVIDE);
					break;
				}

				case TOKEN_PERCENT: {
					emit_byte(emitter, expression->line, OP_MOD);
					break;
				}

				case TOKEN_EQUAL_EQUAL: {
					emit_byte(emitter, expression->line, OP_EQUAL);
					break;
				}

				case TOKEN_BANG_EQUAL: {
					emit_byte(emitter, expression->line, OP_NOT_EQUAL);
					break;
				}

				case TOKEN_GREATER: {
					emit_byte(emitter, expression->line, OP_GREATER);
					break;
				}

				case TOKEN_GREATER_EQUAL: {
					emit_byte(emitter, expression->line, OP_GREATER_EQUAL);
					break;
				}

				case TOKEN_LESS: {
					emit_byte(emitter, expression->line, OP_LESS);
					break;
				}

				case TOKEN_LESS_EQUAL: {
					emit_byte(emitter, expression->line, OP_LESS_EQUAL);
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
					emit_byte(emitter, expression->line, OP_NEGATE);
					break;
				}

				case TOKEN_BANG: {
					emit_byte(emitter, expression->line, OP_NOT);
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
			emit_bytes(emitter, expression->line, OP_GET_GLOBAL, add_constant(emitter, expression->line, OBJECT_VALUE(((LitVarExpression*) expression)->name)));
			break;
		}

		case LOCAL_VAR_EXPRESSION: {
			LitLocalVarExpression* expr = (LitLocalVarExpression*) expression;
			int index = resolve_local(emitter, emitter->compiler, expr->name, expr->length, expression->line);

			if (index == -1) {
				emit_bytes(emitter, expression->line, OP_GET_GLOBAL, add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, expr->name, expr->length))));
				// lit_error(emitter->state, COMPILE_ERROR, expression->line, "Undefined variable '%.*s'", (int) expr->length, expr->name);
				break;
			}

			emit_bytes(emitter, expression->line, OP_GET_LOCAL, (uint8_t) index);
			break;
		}

		case ASSIGN_EXPRESSION: {
			LitAssignExpression* expr = (LitAssignExpression*) expression;
			emit_expression(emitter, expr->value);

			if (expr->to->type == VAR_EXPRESSION) {
				emit_bytes(emitter, expression->line, OP_SET_GLOBAL, add_constant(emitter, expression->line, OBJECT_VALUE(((LitVarExpression*) expr->to)->name)));
			} else if (expr->to->type == LOCAL_VAR_EXPRESSION) {
				LitLocalVarExpression* e = (LitLocalVarExpression*) expr->to;
				int index = resolve_local(emitter, emitter->compiler, e->name, e->length, expr->to->line);

				if (index == -1) {
					emit_bytes(emitter, expression->line, OP_SET_GLOBAL, add_constant(emitter, expression->line, OBJECT_VALUE(lit_copy_string(emitter->state, e->name, e->length))));
					// lit_error(emitter->state, COMPILE_ERROR, expression->line, "Undefined variable '%.*s'", (int) e->length, e->name);
					break;
				} else {
					emit_bytes(emitter, expression->line, OP_SET_LOCAL, (uint8_t) index);
				}
			} else {
				lit_error(emitter->state, COMPILE_ERROR, expression->line, "Invalid assigment target %d", (int) expr->to->type);
			}

			break;
		}

		case CALL_EXPRESSION: {
			LitCallExpression* expr = (LitCallExpression*) expression;

			emit_expression(emitter, expr->callee);

			for (uint i = 0; i < expr->args.count; i++) {
				emit_expression(emitter, expr->args.values[i]);
			}

			emit_bytes(emitter, expression->line, OP_CALL, (uint8_t) expr->args.count);

			break;
		}

		case REQUIRE_EXPRESSION: {
			emit_expression(emitter, ((LitRequireExpression*) expression)->argument);
			emit_byte(emitter, emitter->last_line, OP_REQUIRE);

			break;
		}

		default: {
			lit_error(emitter->state, COMPILE_ERROR, expression->line, "Unknown expression type %d", (int) expression->type);
			break;
		}
	}
}

static bool emit_statement(LitEmitter* emitter, LitStatement* statement) {
	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			emit_expression(emitter, ((LitExpressionStatement*) statement)->expression);
			emit_byte(emitter, statement->line, OP_POP);

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
			int index = add_local(emitter, stmt->name, stmt->length, statement->line);

			if (stmt->init == NULL) {
				emit_byte(emitter, line, OP_NULL);
			} else {
				emit_expression(emitter, stmt->init);
			}

			mark_initialized(emitter, index);
			emit_bytes(emitter, line, OP_SET_LOCAL, (uint8_t) index);

			break;
		}

		case IF_STATEMENT: {
			LitIfStatement* stmt = (LitIfStatement*) statement;

			emit_expression(emitter, stmt->condition);

			uint64_t else_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, statement->line);
			emit_byte(emitter, statement->line, OP_POP); // Pop the condition
			emit_statement(emitter, stmt->if_branch);

			uint64_t end_jump = emit_jump(emitter, OP_JUMP, emitter->last_line);
			uint64_t end_jumps[stmt->elseif_branches == NULL ? 0 : stmt->elseif_branches->count];

			if (stmt->elseif_branches != NULL) {
				for (uint i = 0; i < stmt->elseif_branches->count; i++) {
					LitExpression *e = stmt->elseif_conditions->values[i];

					patch_jump(emitter, else_jump, e->line);
					emit_byte(emitter, e->line, OP_POP); // Pop the old condition
					emit_expression(emitter, e);
					else_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, emitter->last_line);
					emit_byte(emitter, e->line, OP_POP); // Pop the condition
					emit_statement(emitter, stmt->elseif_branches->values[i]);

					end_jumps[i] = emit_jump(emitter, OP_JUMP, emitter->last_line);
				}
			}

			if (stmt->else_branch != NULL) {
				patch_jump(emitter, else_jump, stmt->else_branch->line);
				emit_byte(emitter, stmt->else_branch->line, OP_POP); // Pop the old condition
				emit_statement(emitter, stmt->else_branch);
			} else {
				patch_jump(emitter, else_jump, emitter->last_line);
				emit_byte(emitter, emitter->last_line, OP_POP); // Pop the condition
			}

			patch_jump(emitter, end_jump, emitter->last_line);

			if (stmt->elseif_branches != NULL) {
				for (int i = 0; i < stmt->elseif_branches->count; i++) {
					patch_jump(emitter, end_jumps[i], stmt->elseif_branches->values[i]->line);
				}
			}

			break;
		}

		case WHILE_STATEMENT: {
			LitWhileStatement* stmt = (LitWhileStatement*) statement;

			uint start = emitter->chunk->count;
			emitter->loop_start = start;

			emit_expression(emitter, stmt->condition);

			uint64_t exit_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, statement->line);
			emit_byte(emitter, emitter->last_line, OP_POP); // Pop the condition
			emit_statement(emitter, stmt->body);

			emit_loop(emitter, start, emitter->last_line);
			patch_jump(emitter, exit_jump, emitter->last_line);
			emit_byte(emitter, emitter->last_line, OP_POP); // Pop the condition
			patch_breaks(emitter, emitter->last_line);

			break;
		}

		case FOR_STATEMENT: {
			LitForStatement* stmt = (LitForStatement*) statement;
			begin_scope(emitter);

			if (stmt->var != NULL) {
				emit_statement(emitter, stmt->var);
			} else if (stmt->init != NULL) {
				emit_expression(emitter, stmt->init);
			}

			uint start = emitter->chunk->count;
			uint exit_jump;

			if (stmt->condition != NULL) {
				emit_expression(emitter, stmt->condition);
				exit_jump = emit_jump(emitter, OP_JUMP_IF_FALSE, emitter->last_line);
				emit_byte(emitter, emitter->last_line, OP_POP); // Pop the condition
			}

			if (stmt->increment != NULL) {
				uint body_jump = emit_jump(emitter, OP_JUMP, emitter->last_line);
				uint increment_start = emitter->chunk->count;

				emit_expression(emitter, stmt->increment);
				emit_byte(emitter, emitter->last_line, OP_POP); // Pop the condition

				emit_loop(emitter, start, emitter->last_line);
				start = increment_start;
				patch_jump(emitter, body_jump, emitter->last_line);
			}

			emitter->loop_start = start;
			emit_statement(emitter, stmt->body);
			emit_loop(emitter, start, emitter->last_line);

			if (stmt->condition != NULL) {
				patch_jump(emitter, exit_jump, emitter->last_line);
				emit_byte(emitter, emitter->last_line, OP_POP); // Pop the condition
			}

			patch_breaks(emitter, emitter->last_line);
			end_scope(emitter, emitter->last_line);

			break;
		}

		case CONTINUE_STATEMENT: {
			emit_loop(emitter, emitter->loop_start, statement->line);
			break;
		}

		case BREAK_STATEMENT: {
			lit_uints_write(emitter->state, &emitter->breaks, emit_jump(emitter, OP_JUMP, statement->line));
			break;
		}

		case FUNCTION_STATEMENT: {
			LitFunctionStatement* stmt = (LitFunctionStatement*) statement;
			begin_scope(emitter);

			LitString* name = lit_copy_string(emitter->state, stmt->name, stmt->length);
			// mark_initialized(emitter, add_local(emitter, stmt->name, stmt->length, statement->line));

			LitCompiler compiler;
			init_compiler(emitter, &compiler, FUNCTION_REGULAR);

			for (uint i = 0; i < stmt->parameters.count; i++) {
				LitParameter parameter = stmt->parameters.values[i];
				mark_initialized(emitter, add_local(emitter, parameter.name, parameter.length, statement->line));
			}

			emit_statement(emitter, stmt->body);

			LitFunction* function = end_compiler(emitter, name);
			function->arg_count = stmt->parameters.count;

			emit_constant(emitter, emitter->last_line, OBJECT_VALUE(function));
			emit_bytes(emitter, emitter->last_line, OP_SET_GLOBAL, add_constant(emitter, statement->line, OBJECT_VALUE(name)));
			emit_byte(emitter, emitter->last_line, OP_POP);

			end_scope(emitter, emitter->last_line);

			break;
		}

		case RETURN_STATEMENT: {
			LitExpression* expression = ((LitReturnStatement*) statement)->expression;

			if (expression == NULL) {
				emit_byte(emitter, emitter->last_line, OP_NULL);
			} else {
				emit_expression(emitter, expression);
			}

			emit_byte(emitter, emitter->last_line, OP_RETURN);
			emitter->compiler->skip_return = true;

			return true;
		}

		default: {
			lit_error(emitter->state, COMPILE_ERROR, statement->line, "Unknown statement type %d", (int) statement->type);
			break;
		}
	}

	return false;
}

LitFunction* lit_emit(LitEmitter* emitter, LitStatements* statements) {
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
	return end_compiler(emitter, NULL);
}