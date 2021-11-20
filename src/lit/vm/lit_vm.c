#include "vm/lit_vm.h"
#include "vm/lit_object.h"
#include"debug/lit_debug.h"
#include "mem/lit_mem.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <setjmp.h>

#ifdef LIT_TRACE_EXECUTION
	#define TRACE_FRAME() lit_trace_frame(fiber);
#else
	#define TRACE_FRAME() do {} while (0);
#endif

#define PUSH_GC(state, allow) bool was_allowed = state->allow_gc; state->allow_gc = allow;
#define POP_GC(state) state->allow_gc = was_allowed;

static jmp_buf jump_buffer;

static void reset_vm(LitState* state, LitVm* vm) {
	vm->state = state;
	vm->objects = NULL;
	vm->fiber = NULL;

	vm->gray_stack = NULL;
	vm->gray_count = 0;
	vm->gray_capacity = 0;

	lit_init_table(&vm->strings);

	vm->globals = NULL;
	vm->modules = NULL;
}

void lit_init_vm(LitState* state, LitVm* vm) {
	reset_vm(state, vm);

	vm->globals = lit_create_map(state);
	vm->modules = lit_create_map(state);
}

void lit_free_vm(LitVm* vm) {
	lit_free_table(vm->state, &vm->strings);
	lit_free_objects(vm->state, vm->objects);

	reset_vm(vm->state, vm);
}

bool lit_handle_runtime_error(LitVm* vm, LitString* error_string) {
	LitValue error = OBJECT_VALUE(error_string);
	LitFiber* fiber = vm->fiber;

	while (fiber != NULL) {
		fiber->error = error;

		if (fiber->catcher) {
			vm->fiber = fiber->parent;
			// vm->fiber->stack_top -= fiber->arg_count;
			// vm->fiber->stack_top[-1] = error;

			return true;
		}

		LitFiber* caller = fiber->parent;
		fiber->parent = NULL;
		fiber = caller;
	}

	fiber = vm->fiber;
	fiber->abort = true;
	fiber->error = error;

	if (fiber->parent != NULL) {
		fiber->parent->abort = true;
	}

	// Maan, formatting c strings is hard...
	int count = (int) fiber->frame_count - 1;
	size_t length = snprintf(NULL, 0, "%s%s\n", COLOR_RED, error_string->chars);

	for (int i = count; i >= 0; i--) {
		LitCallFrame* frame = &fiber->frames[i];
		LitFunction* function = frame->function;
		LitChunk* chunk = &function->chunk;
		const char* name = function->name == NULL ? "unknown" : function->name->chars;

		if (chunk->has_line_info) {
			length += snprintf(NULL, 0, "[line %d] in %s()\n", lit_chunk_get_line(chunk, frame->ip - chunk->code - 1), name);
		} else {
			length += snprintf(NULL, 0, "\tin %s()\n", name);
		}
	}

	length += snprintf(NULL, 0, "%s", COLOR_RESET);
	char buffer[length + 1];
	buffer[length] = '\0';

	char* start = buffer + sprintf(buffer, "%s%s\n", COLOR_RED, error_string->chars);

	for (int i = count; i >= 0; i--) {
		LitCallFrame* frame = &fiber->frames[i];
		LitFunction* function = frame->function;
		LitChunk* chunk = &function->chunk;
		const char* name = function->name == NULL ? "unknown" : function->name->chars;

		if (chunk->has_line_info) {
			start += sprintf(start, "[line %d] in %s()\n", lit_chunk_get_line(chunk, frame->ip - chunk->code - 1), name);
		} else {
			start += sprintf(start, "\tin %s()\n", name);
		}
	}

	start += sprintf(start, "%s", COLOR_RESET);
	lit_error(vm->state, RUNTIME_ERROR, buffer);
	// reset_stack(vm);

	return false;
}

bool lit_vruntime_error(LitVm* vm, const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	size_t buffer_size = vsnprintf(NULL, 0, format, args_copy) + 1;
	va_end(args_copy);

	char buffer[buffer_size];
	vsnprintf(buffer, buffer_size, format, args);

	return lit_handle_runtime_error(vm, lit_copy_string(vm->state, buffer, buffer_size));
}

bool lit_runtime_error(LitVm* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	bool result = lit_vruntime_error(vm, format, args);
	va_end(args);

	return result;
}

bool lit_runtime_error_exiting(LitVm* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	bool result = lit_vruntime_error(vm, format, args);
	va_end(args);

	lit_native_exit_jump();
	return result;
}

static bool call(LitVm* vm, register LitFunction* function, LitClosure* closure, uint8_t arg_count) {
	register LitFiber* fiber = vm->fiber;

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return true;
	}

	if (fiber->frame_count + 1 > fiber->frame_capacity) {
		uint new_capacity = fmin(LIT_CALL_FRAMES_MAX, fiber->frame_capacity * 2);
		fiber->frames = (LitCallFrame*) lit_reallocate(vm->state, fiber->frames, sizeof(LitCallFrame) * fiber->frame_capacity, sizeof(LitCallFrame) * new_capacity);
		fiber->frame_capacity = new_capacity;
	}

	uint function_arg_count = function->arg_count;
	lit_ensure_fiber_registers(vm->state, fiber, function->max_registers + (int) (fiber->registers_allocated - fiber->registers_used));

	register LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->function = function;
	frame->closure = closure;
	frame->ip = function->chunk.code;
	frame->slots = fiber->registers + fiber->registers_used;
	frame->result_ignored = false;
	frame->return_to_c = false;

	// TODO: pass args

	return true;
}

static bool call_value(LitVm* vm, LitValue callee, uint8_t arg_count) {
	if (IS_OBJECT(callee)) {
		if (lit_set_native_exit_jump()) {
			return true;
		}

		switch (OBJECT_TYPE(callee)) {
			case OBJECT_FUNCTION: {
				return call(vm, AS_FUNCTION(callee), NULL, arg_count);
			}

			default: {
				break;
			}
		}
	}

	if (IS_NULL(callee)) {
		lit_runtime_error(vm, "Attempt to call a null value");
	} else {
		lit_runtime_error(vm, "Can only call functions and classes, got %s", lit_get_value_type(callee));
	}

	return true;
}

LitInterpretResult lit_interpret_module(LitState* state, LitModule* module) {
	register LitVm *vm = state->vm;

	LitFiber* fiber = lit_create_fiber(state, module, module->main_function);
	vm->fiber = fiber;

	// lit_push(vm, OBJECT_VALUE(module->main_function));
	LitInterpretResult result = lit_interpret_fiber(state, fiber);

	return result;
}

LitInterpretResult lit_interpret_fiber(LitState* state, register LitFiber* fiber) {
	// Has to be inside of the function in order for goto to work
	static void* dispatch_table[] = {
		#define OPCODE(name, a, b) &&OP_##name,
		#include "vm/lit_opcodes.h"
		#undef OPCODE
	};

	#define DISPATCH_NEXT() goto dispatch;
	#define CASE_CODE(name) OP_##name:
	#define READ_FRAME() frame = &fiber->frames[fiber->frame_count - 1]; \
		current_chunk = &frame->function->chunk; \
	  constants = current_chunk->constants.values; \
		ip = frame->ip; \
		fiber->module = frame->function->module; \
		registers = fiber->registers;

	#define WRITE_FRAME() frame->ip = ip;
	#define RETURN_ERROR() POP_GC(state) return (LitInterpretResult) { INTERPRET_RUNTIME_ERROR, NULL_VALUE };

	#define RECOVER_STATE() \
		WRITE_FRAME() \
		fiber = vm->fiber; \
		if (fiber == NULL) { \
			return (LitInterpretResult) { INTERPRET_OK, NULL_VALUE }; \
		} \
		if (fiber->abort) { \
			RETURN_ERROR() \
		} \
		READ_FRAME() \
		TRACE_FRAME()

	#define RUNTIME_ERROR(format) \
		if (lit_runtime_error(vm, format)) { \
			RECOVER_STATE() \
			DISPATCH_NEXT() \
		} else { \
			RETURN_ERROR() \
		}

	#define RUNTIME_ERROR_VARG(format, ...) \
		if (lit_runtime_error(vm, format, __VA_ARGS__)) { \
			RECOVER_STATE() \
			DISPATCH_NEXT() \
		} else { \
			RETURN_ERROR() \
		}

	#define GET_RC(r) (IS_BIT_SET(r, 8) ? constants[r & 0xff] : registers[r])

	// Instruction helpers
	#define BINARY_INSTRUCTION(type, op, op_string) \
		uint16_t b = LIT_INSTRUCTION_B(instruction); \
		uint16_t c = LIT_INSTRUCTION_C(instruction); \
    LitValue bv = GET_RC(b); \
    LitValue cv = GET_RC(c); \
		if (IS_NUMBER(bv)) { \
			if (!IS_NUMBER(cv)) { \
				RUNTIME_ERROR_VARG("Attempt to use the operator %s with a number and a %s", op_string, lit_get_value_type(cv)) \
			} \
			registers[LIT_INSTRUCTION_A(instruction)] = type(AS_NUMBER(bv) op AS_NUMBER(cv)); \
		} else if (IS_NULL(bv)) { \
			RUNTIME_ERROR_VARG("Attempt to use the operator %s on a null value", op_string) \
		} else { \
			RUNTIME_ERROR("Invoking operator methods not implemented yet") /*INVOKE_METHOD(bv, op_string, 1)*/ \
		}

	#define COMPARISON_INSTRUCTION(op) \
		uint16_t b = LIT_INSTRUCTION_B(instruction); \
    uint16_t c = LIT_INSTRUCTION_C(instruction); \
		registers[LIT_INSTRUCTION_A(instruction)] = BOOL_VALUE(GET_RC(b) op GET_RC(c));

	register LitVm *vm = state->vm;
	register LitTable *globals = &vm->globals->values;

	PUSH_GC(state, true)

	vm->fiber = fiber;
	fiber->abort = false;

	register LitCallFrame* frame;
	register LitChunk* current_chunk;
	register LitValue* registers;
	register LitValue* constants;
	register uint64_t* ip;

	uint64_t instruction;

	READ_FRAME()
	TRACE_FRAME()

	printf("\nstart:\n\n");

	dispatch:
	instruction = *ip++;

	#ifdef LIT_TRACE_EXECUTION
		lit_disassemble_instruction(current_chunk, (uint) (ip - current_chunk->code - 1), NULL);
	#endif

	goto *dispatch_table[LIT_INSTRUCTION_OPCODE(instruction)];

	CASE_CODE(MOVE) {
		uint16_t b = LIT_INSTRUCTION_B(instruction);
		registers[LIT_INSTRUCTION_A(instruction)] = GET_RC(b);

		DISPATCH_NEXT()
	}

	CASE_CODE(LOAD_NULL) {
		registers[LIT_INSTRUCTION_A(instruction)] = NULL_VALUE;
		DISPATCH_NEXT()
	}

	CASE_CODE(LOAD_BOOL) {
		registers[LIT_INSTRUCTION_A(instruction)] = BOOL_VALUE(LIT_INSTRUCTION_B(instruction) != 0);
		DISPATCH_NEXT()
	}

	CASE_CODE(RETURN) {
		printf("\ntop registers:\n");

		for (uint8_t i = 0; i < frame->function->max_registers; i++) {
			printf("%i: ", i);
			lit_print_value(registers[i]);
			printf("\n");
		}

		// TODO: implement the return of values
		return (LitInterpretResult) { INTERPRET_OK, NULL_VALUE };
	}

	CASE_CODE(ADD) {
		BINARY_INSTRUCTION(NUMBER_VALUE, +, "+")
		DISPATCH_NEXT()
	}

	CASE_CODE(SUBTRACT) {
		BINARY_INSTRUCTION(NUMBER_VALUE, -, "-")
		DISPATCH_NEXT()
	}

	CASE_CODE(MULTIPLY) {
		BINARY_INSTRUCTION(NUMBER_VALUE, *, "*")
		DISPATCH_NEXT()
	}

	CASE_CODE(DIVIDE) {
		BINARY_INSTRUCTION(NUMBER_VALUE, /, "/")
		DISPATCH_NEXT()
	}

	CASE_CODE(JUMP) {
		ip += LIT_INSTRUCTION_SBX(instruction);
		DISPATCH_NEXT()
	}

	CASE_CODE(FALSE_JUMP) {
		if (lit_is_falsey(registers[LIT_INSTRUCTION_A(instruction)])) {
			ip += LIT_INSTRUCTION_BX(instruction);
		}

		DISPATCH_NEXT()
	}

	CASE_CODE(EQUAL) {
		COMPARISON_INSTRUCTION(==)
		DISPATCH_NEXT()
	}

	CASE_CODE(LESS) {
		COMPARISON_INSTRUCTION(<)
		DISPATCH_NEXT()
	}

	CASE_CODE(LESS_EQUAL) {
		COMPARISON_INSTRUCTION(<=)
		DISPATCH_NEXT()
	}

	CASE_CODE(NEGATE) {
		uint16_t b = LIT_INSTRUCTION_B(instruction);
    LitValue value = GET_RC(b);

		if (!IS_NUMBER(value)) {
			// Don't even ask me why
			// This doesn't kill our performance, since it's a error anyway
			if (IS_STRING(value) && strcmp(AS_CSTRING(value), "muffin") == 0) {
				RUNTIME_ERROR("Idk, can you negate a muffin?")
			} else {
				RUNTIME_ERROR("Operand must be a number")
			}
		}

		registers[LIT_INSTRUCTION_A(instruction)] = NUMBER_VALUE(-AS_NUMBER(value));
		DISPATCH_NEXT()
	}

	CASE_CODE(NOT) {
		uint16_t b = LIT_INSTRUCTION_B(instruction);
		registers[LIT_INSTRUCTION_A(instruction)] = BOOL_VALUE(lit_is_falsey(GET_RC(b)));

		DISPATCH_NEXT()
	}

	CASE_CODE(SET_GLOBAL) {
		lit_table_set(state, globals, AS_STRING(constants[LIT_INSTRUCTION_A(instruction)]), GET_RC(LIT_INSTRUCTION_BX(instruction)));
		DISPATCH_NEXT()
	}

	CASE_CODE(GET_GLOBAL) {
		LitValue *reg = &registers[LIT_INSTRUCTION_BX(instruction)];

		if (!lit_table_get(globals, AS_STRING(constants[LIT_INSTRUCTION_A(instruction)]), reg)) {
			*reg = NULL_VALUE;
		}

		DISPATCH_NEXT()
	}

	RETURN_ERROR()

	#undef COMPARISON_INSTRUCTION
	#undef BINARY_INSTRUCTION

	#undef GET_RC
	#undef RUNTIME_ERROR_VARG
	#undef RUNTIME_ERROR
	#undef CALL_VALUE
	#undef RECOVER_STATE
	#undef WRITE_FRAME
	#undef READ_FRAME
	#undef CASE_CODE
	#undef DISPATCH_NEXT
	#undef RETURN_ERROR
}

void lit_native_exit_jump() {
	longjmp(jump_buffer, 1);
}

bool lit_set_native_exit_jump() {
	return setjmp(jump_buffer);
}

#undef PUSH_GC
#undef POP_GC
#undef LIT_TRACE_FRAME