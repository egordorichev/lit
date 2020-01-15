#include <lit/emitter/lit_emitter.h>
#include <lit/mem/lit_mem.h>
#include <lit/debug/lit_debug.h>


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

static void emit_expression(LitEmitter* emitter, LitExpression* expression) {
	switch (expression->type) {
		case LITERAL: {
			uint constant = lit_chunk_add_constant(emitter->state, emitter->chunk, ((LitLiteralExpression*) expression)->value);

			if (constant < UINT8_MAX) {
				emit_bytes(emitter, expression->line, OP_CONSTANT, constant);
			} else if (constant < UINT16_MAX) {
				emit_byte(emitter, expression->line, OP_CONSTANT_LONG);
				emit_bytes(emitter, expression->line, (uint8_t) ((constant >> 8) & 0xff), (uint8_t) (constant & 0xff));
			} else {
				// TODO: ERROR
			}

			break;
		}

		default: break;
	}
}

static void emit_statement(LitEmitter* emitter, LitStatement* statement) {
	switch (statement->type) {
		case EXPRESSION: {
			emit_expression(emitter, ((LitExpressionStatement*) statement)->expression);
			emit_byte(emitter, statement->line, OP_POP);

			break;
		}

		default: break;
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