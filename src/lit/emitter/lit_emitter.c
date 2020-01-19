#include <lit/emitter/lit_emitter.h>
#include <lit/mem/lit_mem.h>
#include <lit/debug/lit_debug.h>
#include <lit/vm/lit_object.h>


void lit_init_emitter(LitState* state, LitEmitter* emitter) {
	emitter->state = state;
	emitter->had_error = false;
}

void lit_free_emitter(LitEmitter* emitter) {

}

static void emit_byte(LitEmitter* emitter, uint16_t line, uint8_t byte) {
	lit_write_chunk(emitter->state, emitter->chunk, byte, line);
}

static void emit_bytes(LitEmitter* emitter, uint16_t line, uint8_t a, uint8_t b) {
	lit_write_chunk(emitter->state, emitter->chunk, a, line);
	lit_write_chunk(emitter->state, emitter->chunk, b, line);
}

static uint8_t add_constant(LitEmitter* emitter, LitValue value, uint line) {
	uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

	if (constant >= UINT8_MAX) {
		lit_error(emitter->state, COMPILE_ERROR, line, "Too many constants in one chunk");
	}

	return constant;
}

static void emit_expression(LitEmitter* emitter, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL_EXPRESSION: {
			LitValue value = ((LitLiteralExpression*) expression)->value;

			if (IS_NUMBER(value) || IS_STRING(value)) {
				uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, value);

				if (constant < UINT8_MAX) {
					emit_bytes(emitter, expression->line, OP_CONSTANT, constant);
				} else if (constant < UINT16_MAX) {
					emit_byte(emitter, expression->line, OP_CONSTANT_LONG);
					emit_bytes(emitter, expression->line, (uint8_t) ((constant >> 8) & 0xff), (uint8_t) (constant & 0xff));
				} else {
					lit_error(emitter->state, COMPILE_ERROR, expression->line, "Too many constants in one chunk");
				}
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
			emit_bytes(emitter, expression->line, OP_GET_GLOBAL, add_constant(emitter, OBJECT_VAL(((LitVarExpression*) expression)->name), expression->line));
			break;
		}

		default: {
			lit_error(emitter->state, COMPILE_ERROR, expression->line, "Unknown expression type %d", (int) expression->type);
			break;
		}
	}
}

static void emit_statement(LitEmitter* emitter, LitStatement* statement) {
	switch (statement->type) {
		case EXPRESSION_STATEMENT: {
			emit_expression(emitter, ((LitExpressionStatement*) statement)->expression);
			emit_byte(emitter, statement->line, OP_POP);

			break;
		}

		case PRINT_STATEMENT: {
			emit_expression(emitter, ((LitPrintStatement*) statement)->expression);
			emit_byte(emitter, statement->line, OP_PRINT);

			break;
		}

		case VAR_STATEMENT: {
			LitVarStatement* stmt = (LitVarStatement*) statement;
			uint line = statement->line;

			if (stmt->init == NULL) {
				emit_byte(emitter, line, OP_NULL);
			} else {
				emit_expression(emitter, stmt->init);
			}

			emit_bytes(emitter, line, OP_DEFINE_GLOBAL, add_constant(emitter, OBJECT_VAL(stmt->name), line));
			break;
		}

		default: {
			lit_error(emitter->state, COMPILE_ERROR, statement->line, "Unknown statement type %d", (int) statement->type);
			break;
		}
	}
}

LitChunk* lit_emit(LitEmitter* emitter, LitStatements* statements) {
	emitter->had_error = false;

	LitChunk* chunk = (LitChunk*) lit_reallocate(emitter->state, NULL, 0, sizeof(LitChunk));
	lit_init_chunk(chunk);
	emitter->chunk = chunk;

	for (uint i = 0; i < statements->count; i++) {
		emit_statement(emitter, statements->values[i]);
	}

	emit_byte(emitter, 1, OP_RETURN);
	return chunk;
}