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

static bool call(LitVm* vm, register LitFunction* function, LitClosure* closure, uint8_t arg_count, uint callee_register) {
	register LitFiber* fiber = vm->fiber;
	assert(fiber->frame_count > 0);

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return true;
	}

	if (fiber->frame_count + 1 > fiber->frame_capacity) {
		uint new_capacity = fmin(LIT_CALL_FRAMES_MAX, fiber->frame_capacity * 2);
		fiber->frames = (LitCallFrame*) lit_reallocate(vm->state, fiber->frames, sizeof(LitCallFrame) * fiber->frame_capacity, sizeof(LitCallFrame) * new_capacity);
		fiber->frame_capacity = new_capacity;
	}

	fiber->registers_used += function->max_registers;
	lit_ensure_fiber_registers(vm->state, fiber, fiber->registers_used);

	register LitCallFrame* frame = &fiber->frames[fiber->frame_count++];
	LitCallFrame* previous_frame = &fiber->frames[fiber->frame_count - 2];

	frame->function = function;
	frame->closure = closure;
	frame->ip = function->chunk.code;
	frame->slots = previous_frame->slots + callee_register;
	frame->result_ignored = false;
	frame->return_to_c = false;
	frame->return_address = previous_frame->slots + (int) callee_register;

	return true;
}

static bool call_value(LitVm* vm, uint callee_register, uint8_t arg_count, LitValue alternate_callee) {
	LitCallFrame* frame = &vm->fiber->frames[vm->fiber->frame_count - 1];
	LitValue callee = IS_NULL(alternate_callee) ? frame->slots[callee_register] : alternate_callee;

	if (IS_OBJECT(callee)) {
		if (lit_set_native_exit_jump()) {
			return true;
		}

		switch (OBJECT_TYPE(callee)) {
			case OBJECT_FUNCTION: {
				return call(vm, AS_FUNCTION(callee), NULL, arg_count, callee_register);
			}

			case OBJECT_CLOSURE: {
				LitClosure* closure = AS_CLOSURE(callee);
				return call(vm, closure->function, closure, arg_count, callee_register);
			}

			case OBJECT_NATIVE_FUNCTION: {
				frame->slots[callee_register] = AS_NATIVE_FUNCTION(callee)->function(vm, arg_count, frame->slots + callee_register + 1);
				return true;
			}

			case OBJECT_NATIVE_PRIMITIVE: {
				PUSH_GC(vm->state, false)

				LitFiber* fiber = vm->fiber;
				bool result = AS_NATIVE_PRIMITIVE(callee)->function(vm, arg_count, frame->slots + callee_register + 1);

				POP_GC(vm->state)
				return result;
			}

			case OBJECT_NATIVE_METHOD: {
				PUSH_GC(vm->state, false)

				LitNativeMethod* method = AS_NATIVE_METHOD(callee);
				LitFiber* fiber = vm->fiber;

				frame->slots[callee_register] = method->method(vm, *(frame->slots + callee_register + 1), arg_count, frame->slots + callee_register + 2);
				POP_GC(vm->state)

				return true;
			}

			case OBJECT_PRIMITIVE_METHOD: {
				PUSH_GC(vm->state, false)

				LitFiber* fiber = vm->fiber;
				bool result = AS_PRIMITIVE_METHOD(callee)->method(vm, *(frame->slots + callee_register + 1), arg_count, frame->slots + callee_register + 2);

				POP_GC(vm->state)
				return result;
			}

			case OBJECT_CLASS: {
				LitClass* klass = AS_CLASS(callee);
				LitInstance* instance = lit_create_instance(vm->state, klass);

				frame->slots[callee_register] = OBJECT_VALUE(instance);

				if (klass->init_method != NULL) {
					return call_value(vm, callee_register, arg_count, OBJECT_VALUE(klass->init_method));
				}

				return true;
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

static LitUpvalue* capture_upvalue(LitState* state, LitValue* local) {
	LitUpvalue* previous_upvalue = NULL;
	LitUpvalue* upvalue = state->vm->fiber->open_upvalues;

	while (upvalue != NULL && upvalue->location > local) {
		previous_upvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	LitUpvalue* created_upvalue = lit_create_upvalue(state, local);
	created_upvalue->next = upvalue;

	if (previous_upvalue == NULL) {
		state->vm->fiber->open_upvalues = created_upvalue;
	} else {
		previous_upvalue->next = created_upvalue;
	}

	return created_upvalue;
}

static void close_upvalues(register LitVm* vm, const LitValue* last) {
	LitFiber* fiber = vm->fiber;

	while (fiber->open_upvalues != NULL && fiber->open_upvalues->location >= last) {
		LitUpvalue* upvalue = fiber->open_upvalues;

		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;

		fiber->open_upvalues = upvalue->next;
	}
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
	assert(fiber->frame_count == 1);

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
		registers = frame->slots; \
		privates = fiber->module->privates; \
		upvalues = frame->closure == NULL ? NULL : frame->closure->upvalues;

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

	#define CALL_VALUE(callee, reg, arg_count) \
		if (call_value(vm, reg, arg_count, callee)) { \
			RECOVER_STATE() \
		}

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

	#define COMPARISON_INSTRUCTION(type, op, op_string) \
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

	register LitVm *vm = state->vm;
	register LitTable *globals = &vm->globals->values;

	PUSH_GC(state, true)

	vm->fiber = fiber;
	fiber->abort = false;

	register LitCallFrame* frame;
	register LitChunk* current_chunk;
	register LitValue* registers;
	register LitValue* constants;
	register LitValue* privates;
	LitUpvalue** upvalues;

	register uint64_t* ip;

	uint64_t instruction;

	READ_FRAME()

#ifdef LIT_TRACE_EXECUTION
	// Make sure we don't crash printing out some mess from the memory
	for (int i = 1; i <= frame->function->max_registers; i++) {
		registers[i] = NUMBER_VALUE(0);
	}
#endif

	registers[0] = OBJECT_VALUE(frame->function);
	TRACE_FRAME()

	fiber->registers_used = frame->function->max_registers;

#ifdef LIT_TRACE_EXECUTION
	printf("\nstart:\n\n");
#endif

	dispatch:
	instruction = *ip++;

	#ifdef LIT_TRACE_EXECUTION
		if (frame->function->max_registers > 0) {
			printf("        |\n        | f%i ", fiber->frame_count);

			for (int i = 0; i <= frame->function->max_registers; i++) {
				printf("[ ");
				lit_print_value(*(registers + i));
				printf(" ]");
			}

			printf("\n");
		}

		lit_disassemble_instruction(current_chunk, (uint) (ip - current_chunk->code - 1), NULL);
	#endif

	goto *dispatch_table[LIT_INSTRUCTION_OPCODE(instruction)];

	CASE_CODE(MOVE) {
		registers[LIT_INSTRUCTION_A(instruction)] = GET_RC(LIT_INSTRUCTION_B(instruction));
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

	CASE_CODE(CLOSURE) {
		LitClosurePrototype* closure_prototype = AS_CLOSURE_PROTOTYPE(constants[LIT_INSTRUCTION_BX(instruction)]);
		LitClosure* closure = lit_create_closure(state, closure_prototype->function);

		registers[LIT_INSTRUCTION_A(instruction)] = OBJECT_VALUE(closure);

		for (uint i = 0; i < closure->function->upvalue_count; i++) {
			uint8_t index = closure_prototype->indexes[i];

			if (closure_prototype->local[i]) {
				closure->upvalues[i] = capture_upvalue(state, registers + index);
			} else {
				closure->upvalues[i] = upvalues[index];
			}
		}

		DISPATCH_NEXT()
	}

	CASE_CODE(RETURN) {
		LitValue value = registers[LIT_INSTRUCTION_A(instruction)];

		fiber->frame_count--;
		fiber->registers_used -= fiber->frames[fiber->frame_count].function->max_registers;

		if (fiber->frame_count == 0) {
			return (LitInterpretResult) { INTERPRET_OK, value };
		}

		*frame->return_address = value;

		READ_FRAME()
		TRACE_FRAME()
		DISPATCH_NEXT()
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

	CASE_CODE(TRUE_JUMP) {
		if (!lit_is_falsey(registers[LIT_INSTRUCTION_A(instruction)])) {
			ip += LIT_INSTRUCTION_BX(instruction);
		}

		DISPATCH_NEXT()
	}

	CASE_CODE(FALSE_JUMP) {
		if (lit_is_falsey(registers[LIT_INSTRUCTION_A(instruction)])) {
			ip += LIT_INSTRUCTION_BX(instruction);
		}

		DISPATCH_NEXT()
	}

	CASE_CODE(NON_NULL_JUMP) {
		if (registers[LIT_INSTRUCTION_A(instruction)] != NULL_VALUE) {
			ip += LIT_INSTRUCTION_BX(instruction);
		}

		DISPATCH_NEXT()
	}

	CASE_CODE(EQUAL) {
		COMPARISON_INSTRUCTION(NUMBER_VALUE, ==, "==")
		DISPATCH_NEXT()
	}

	CASE_CODE(LESS) {
		COMPARISON_INSTRUCTION(NUMBER_VALUE, <, "<")
		DISPATCH_NEXT()
	}

	CASE_CODE(LESS_EQUAL) {
		COMPARISON_INSTRUCTION(NUMBER_VALUE, <=, "<=")
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

	CASE_CODE(SET_UPVALUE) {
		*frame->closure->upvalues[LIT_INSTRUCTION_A(instruction)]->location = GET_RC(LIT_INSTRUCTION_BX(instruction));
		DISPATCH_NEXT()
	}

	CASE_CODE(GET_UPVALUE) {
		registers[LIT_INSTRUCTION_A(instruction)] = *frame->closure->upvalues[LIT_INSTRUCTION_BX(instruction)]->location;
		DISPATCH_NEXT()
	}

	CASE_CODE(SET_PRIVATE) {
		uint8_t a = LIT_INSTRUCTION_A(instruction);
		uint32_t b = LIT_INSTRUCTION_BX(instruction);

		privates[(uint16_t) b] = IS_BIT_SET(b, 16) ? constants[a] : registers[a];
		DISPATCH_NEXT()
	}

	CASE_CODE(GET_PRIVATE) {
		registers[LIT_INSTRUCTION_A(instruction)] = privates[LIT_INSTRUCTION_BX(instruction)];
		DISPATCH_NEXT()
	}

	CASE_CODE(CALL) {
		WRITE_FRAME()

		if (!call_value(vm, LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_B(instruction) - 1, NULL_VALUE)) {
			RETURN_ERROR()
		}

		READ_FRAME()
		DISPATCH_NEXT()
	}

	CASE_CODE(CLOSE_UPVALUE) {
		close_upvalues(vm, &registers[LIT_INSTRUCTION_A(instruction)]);
		DISPATCH_NEXT()
	}

	CASE_CODE(CLASS) {
		LitString* name = AS_STRING(constants[LIT_INSTRUCTION_A(instruction)]);
		LitClass* klass = lit_create_class(state, name);

		registers[LIT_INSTRUCTION_C(instruction)] = OBJECT_VALUE(klass);
		lit_table_set(state, &vm->globals->values, name, OBJECT_VALUE(klass));

		uint16_t b = LIT_INSTRUCTION_B(instruction);

		if (b == 0) {
			klass->super = state->object_class;

			lit_table_add_all(state, &klass->super->methods, &klass->methods);
			lit_table_add_all(state, &klass->super->static_fields, &klass->static_fields);
		} else {
			LitValue super = registers[--b];

			if (!IS_CLASS(super)) {
				RUNTIME_ERROR("Superclass must be a class")
			}

			LitClass* super_klass = AS_CLASS(super);

			klass->super = super_klass;
			klass->init_method = super_klass->init_method;

			lit_table_add_all(state, &super_klass->methods, &klass->methods);
			lit_table_add_all(state, &klass->super->static_fields, &klass->static_fields);
		}

		DISPATCH_NEXT()
	}

	CASE_CODE(STATIC_FIELD) {
		lit_table_set(state, &AS_CLASS(registers[LIT_INSTRUCTION_A(instruction)])->static_fields, AS_STRING(constants[LIT_INSTRUCTION_B(instruction)]), GET_RC(LIT_INSTRUCTION_C(instruction)));
		DISPATCH_NEXT()
	}

	CASE_CODE(METHOD) {
		LitClass* klass = AS_CLASS(registers[LIT_INSTRUCTION_A(instruction)]);
		LitString* name = AS_STRING(constants[LIT_INSTRUCTION_B(instruction)]);

		if ((klass->init_method == NULL || (klass->super != NULL && klass->init_method == ((LitClass*) klass->super)->init_method)) && name->length == 11 && memcmp(name->chars, "constructor", 11) == 0) {
			klass->init_method = AS_OBJECT(GET_RC(LIT_INSTRUCTION_C(instruction)));
		}

		lit_table_set(state, &klass->methods, name, GET_RC(LIT_INSTRUCTION_C(instruction)));
		DISPATCH_NEXT()
	}

	CASE_CODE(GET_FIELD) {
		LitValue object = registers[LIT_INSTRUCTION_B(instruction)];

		if (IS_NULL(object)) {
			RUNTIME_ERROR("Attempt to index a null value")
		}

		LitValue value;
		LitString *name = AS_STRING(constants[LIT_INSTRUCTION_C(instruction)]);
		uint8_t result_reg = LIT_INSTRUCTION_A(instruction);

		if (IS_INSTANCE(object)) {
			LitInstance *instance = AS_INSTANCE(object);

			if (!lit_table_get(&instance->fields, name, &value)) {
				if (lit_table_get(&instance->klass->methods, name, &value)) {
					if (IS_FIELD(value)) {
						LitField *field = AS_FIELD(value);

						if (field->getter == NULL) {
							RUNTIME_ERROR_VARG("Class %s does not have a getter for the field %s", instance->klass->name->chars, name->chars)
						}

						WRITE_FRAME()
						CALL_VALUE(OBJECT_VALUE(AS_FIELD(value)->getter), result_reg, 0)
						READ_FRAME()
						DISPATCH_NEXT()
					} else {
						value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(instance), value));
					}
				} else {
					value = NULL_VALUE;
				}
			}
		} else if (IS_CLASS(object)) {
			LitClass *klass = AS_CLASS(object);

			if (lit_table_get(&klass->static_fields, name, &value)) {
				if (IS_NATIVE_METHOD(value) || IS_PRIMITIVE_METHOD(value)) {
					value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(klass), value));
				} else if (IS_FIELD(value)) {
					LitField* field = AS_FIELD(value);

					if (field->getter == NULL) {
						RUNTIME_ERROR_VARG("Class %s does not have a getter for the field %s", klass->name->chars, name->chars)
					}

					WRITE_FRAME()
					CALL_VALUE(OBJECT_VALUE(field->getter), result_reg, 0)
					READ_FRAME()
					DISPATCH_NEXT()
				}
			} else {
				value = NULL_VALUE;
			}
		} else {
			LitClass *klass = lit_get_class_for(state, object);

			if (klass == NULL) {
				RUNTIME_ERROR("Only instances and classes have fields")
			}

			if (lit_table_get(&klass->methods, name, &value)) {
				if (IS_FIELD(value)) {
					LitField *field = AS_FIELD(value);

					if (field->getter == NULL) {
						RUNTIME_ERROR_VARG("Class %s does not have a getter for the field %s", klass->name->chars, name->chars)
					}

					WRITE_FRAME()
					CALL_VALUE(OBJECT_VALUE(AS_FIELD(value)->getter), result_reg, 0)
					READ_FRAME()
					DISPATCH_NEXT()
				} else if (IS_NATIVE_METHOD(value) || IS_PRIMITIVE_METHOD(value)) {
					value = OBJECT_VALUE(lit_create_bound_method(state, object, value));
				}
			} else {
				value = NULL_VALUE;
			}
		}

		registers[result_reg] = value;
		DISPATCH_NEXT()
	}

	CASE_CODE(SET_FIELD) {
		uint8_t result_reg = LIT_INSTRUCTION_A(instruction);
		LitValue instance = registers[result_reg];

		if (IS_NULL(instance)) {
			RUNTIME_ERROR("Attempt to index a null value")
		}

		LitValue value = registers[LIT_INSTRUCTION_C(instruction)];
		int b = LIT_INSTRUCTION_B(instruction);
		LitString *field_name = AS_STRING(constants[LIT_INSTRUCTION_B(instruction)]);

		if (IS_CLASS(instance)) {
			LitClass *klass = AS_CLASS(instance);
			LitValue setter;

			if (lit_table_get(&klass->static_fields, field_name, &setter) && IS_FIELD(setter)) {
				LitField *field = AS_FIELD(setter);

				if (field->setter == NULL) {
					RUNTIME_ERROR_VARG("Class %s does not have a setter for the field %s", klass->name->chars, field_name->chars)
				}

				WRITE_FRAME()
				CALL_VALUE(OBJECT_VALUE(field->setter), result_reg, 1)
				READ_FRAME()
				DISPATCH_NEXT()
			}

			if (IS_NULL(value)) {
				lit_table_delete(&klass->static_fields, field_name);
			} else {
				lit_table_set(state, &klass->static_fields, field_name, value);
			}

			// fiber->stack_top[-1] = value;
		} else if (IS_INSTANCE(instance)) {
			LitInstance *inst = AS_INSTANCE(instance);
			LitValue setter;

			if (lit_table_get(&inst->klass->methods, field_name, &setter) && IS_FIELD(setter)) {
				LitField *field = AS_FIELD(setter);

				if (field->setter == NULL) {
					RUNTIME_ERROR_VARG("Class %s does not have a setter for the field %s", inst->klass->name->chars, field_name->chars)
				}

				WRITE_FRAME()
				CALL_VALUE(OBJECT_VALUE(field->setter), result_reg, 1)
				READ_FRAME()
				DISPATCH_NEXT()
			}

			if (IS_NULL(value)) {
				lit_table_delete(&inst->fields, field_name);
			} else {
				lit_table_set(state, &inst->fields, field_name, value);
			}

			// fiber->stack_top[-1] = value;
		} else {
			LitClass *klass = lit_get_class_for(state, instance);

			if (klass == NULL) {
				RUNTIME_ERROR("Only instances and classes have fields")
			}

			LitValue setter;

			if (lit_table_get(&klass->methods, field_name, &setter) && IS_FIELD(setter)) {
				LitField *field = AS_FIELD(setter);

				if (field->setter == NULL) {
					RUNTIME_ERROR_VARG("Class %s does not have a setter for the field %s", klass->name->chars, field_name->chars)
				}

				WRITE_FRAME()
				CALL_VALUE(OBJECT_VALUE(field->setter), result_reg, 1)
				READ_FRAME()
				DISPATCH_NEXT()
			} else {
				RUNTIME_ERROR_VARG("Class %s does not contain field %s", klass->name->chars, field_name->chars)
			}
		}

		DISPATCH_NEXT()
	}

	RUNTIME_ERROR_VARG("Unknown op %i", instruction)
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