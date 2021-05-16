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
	lit_ensure_fiber_registers(vm->state, fiber, function->max_slots + (int) (fiber->registers_allocated - fiber->registers_used));

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
	register LitVm *vm = state->vm;
	PUSH_GC(state, true);

	vm->fiber = fiber;
	fiber->abort = false;

	register LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];
	register LitChunk* current_chunk = &frame->function->chunk;

	fiber->module = frame->function->module;

	register uint64_t* ip = frame->ip;

	// Has to be inside of the function in order for goto to work
	static void* dispatch_table[] = {
#define OPCODE(name, a, b) &&OP_##name,
#include "vm/lit_opcodes.h"
#undef OPCODE
	};

#define CASE_CODE(name) OP_##name:
#define READ_FRAME() frame = &fiber->frames[fiber->frame_count - 1]; \
	current_chunk = &frame->function->chunk; \
	ip = frame->ip; \
	fiber->module = frame->function->module; \

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
		continue; \
	} else { \
		RETURN_ERROR() \
	}

#define RUNTIME_ERROR_VARG(format, ...) \
	if (lit_runtime_error(vm, format, __VA_ARGS__)) { \
		RECOVER_STATE() \
		continue; \
	} else { \
		RETURN_ERROR() \
	}

#ifdef LIT_TRACE_EXECUTION
	TRACE_FRAME()
#endif

	uint64_t instruction;

	while (true) {
		instruction = *ip++;

		#ifdef LIT_TRACE_EXECUTION
		lit_disassemble_instruction(current_chunk, (uint) (ip - current_chunk->code - 1), NULL);
#endif

		goto *dispatch_table[LIT_INSTRUCTION_OPCODE(instruction)];

		CASE_CODE(LOADK) {
			continue;
		}

		CASE_CODE(RETURN) {
			return (LitInterpretResult) { INTERPRET_OK, NULL_VALUE };
		}

		CASE_CODE(MOVE) {

			continue;
		}

		RUNTIME_ERROR_VARG("Unknown op code '%d'", LIT_INSTRUCTION_OPCODE(instruction))
		break;
	}

#undef RUNTIME_ERROR_VARG
#undef RUNTIME_ERROR
#undef CALL_VALUE
#undef RECOVER_STATE
#undef WRITE_FRAME
#undef READ_FRAME
#undef CASE_CODE

	RETURN_ERROR()

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