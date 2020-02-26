#include <lit/std/lit_core.h>
#include <lit/std/lit_math.h>
#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>

#include <time.h>
#include <ctype.h>
#include <string.h>

void lit_open_libraries(LitState* state) {
	lit_open_math_library(state);
}

/*
 * Object
 */

LIT_METHOD(object_toString) {
	return OBJECT_VALUE(lit_string_format(vm->state, "@ instance", OBJECT_VALUE(lit_get_class_for(vm->state, instance)->name)));
}

/*
 * Number
 */

LIT_METHOD(number_toString) {
	return OBJECT_VALUE(lit_number_to_string(vm->state, AS_NUMBER(instance)));
}

/*
 * Bool
 */

LIT_METHOD(bool_toString) {
	if (AS_BOOL(instance)) {
		return OBJECT_CONST_STRING(vm->state, "true");
	}

	return OBJECT_CONST_STRING(vm->state, "false");
}

/*
 * String
 */

LIT_METHOD(string_toString) {
	return instance;
}

LIT_METHOD(string_toUpperCase) {
	LitString* string = AS_STRING(instance);
	char buffer[string->length];

	for (uint i = 0; i < string->length; i++) {
		buffer[i] = (char) toupper(string->chars[i]);
	}

	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, string->length));
}

LIT_METHOD(string_toLowerCase) {
	LitString* string = AS_STRING(instance);
	char buffer[string->length];

	for (uint i = 0; i < string->length; i++) {
		buffer[i] = (char) tolower(string->chars[i]);
	}

	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, string->length));
}

LIT_METHOD(string_contains) {
	LitString* string = AS_STRING(instance);
	LitString* sub = LIT_CHECK_OBJECT_STRING(0);

	if (sub == string) {
		return TRUE_VALUE;
	}

	return BOOL_VALUE(strstr(string->chars, sub->chars) != NULL);
}

LIT_METHOD(string_startsWith) {
	LitString* string = AS_STRING(instance);
	LitString* sub = LIT_CHECK_OBJECT_STRING(0);

	if (sub == string) {
		return TRUE_VALUE;
	}

	if (sub->length > string->length) {
		return FALSE_VALUE;
	}

	for (uint i = 0; i < sub->length; i++) {
		if (sub->chars[i] != string->chars[i]) {
			return FALSE_VALUE;
		}
	}

	return TRUE_VALUE;
}

LIT_METHOD(string_endsWith) {
	LitString* string = AS_STRING(instance);
	LitString* sub = LIT_CHECK_OBJECT_STRING(0);

	if (sub == string) {
		return TRUE_VALUE;
	}

	if (sub->length > string->length) {
		return FALSE_VALUE;
	}

	uint start = string->length - sub->length;

	for (uint i = 0; i < sub->length; i++) {
		if (sub->chars[i] != string->chars[i + start]) {
			return FALSE_VALUE;
		}
	}

	return TRUE_VALUE;
}

LIT_NATIVE(time) {
	return NUMBER_VALUE((double) clock() / CLOCKS_PER_SEC);
}

LIT_NATIVE(print) {
	for (uint i = 0; i < arg_count; i++) {
		lit_print_value(args[i]);
		printf("\n");
	}

	return NULL_VALUE;
}

LIT_NATIVE(eval) {
	const char* code = LIT_CHECK_STRING(0);
	return lit_interpret(vm->state, "eval", code).result;
}

void lit_open_core_library(LitState* state) {
	LIT_BEGIN_CLASS("Class")
		state->class_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Object")
		LIT_BIND_METHOD("toString", object_toString)
		state->object_class = klass;
		state->object_class->super = state->class_class;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Number")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_METHOD("toString", number_toString)
		state->number_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("String")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_METHOD("toString", string_toString)
		LIT_BIND_METHOD("toUpperCase", string_toUpperCase)
		LIT_BIND_METHOD("toLowerCase", string_toLowerCase)
		LIT_BIND_METHOD("contains", string_contains)
		LIT_BIND_METHOD("startsWith", string_startsWith)
		LIT_BIND_METHOD("endsWith", string_endsWith)

		state->string_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Bool")
		LIT_INHERIT_CLASS(state->object_class)state->bool_class = klass;
		LIT_BIND_METHOD("toString", bool_toString)
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Function")
		LIT_INHERIT_CLASS(state->object_class)
		state->function_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Fiber")
		LIT_INHERIT_CLASS(state->object_class)
		state->fiber_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Module")
		LIT_INHERIT_CLASS(state->object_class)
		state->module_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Array")
		LIT_INHERIT_CLASS(state->object_class)
		state->array_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Map")
		LIT_INHERIT_CLASS(state->object_class)
		state->map_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Range")
		LIT_INHERIT_CLASS(state->object_class)
		state->range_class = klass;
	LIT_END_CLASS()

	lit_define_native(state, "time", time_native);
	lit_define_native(state, "eval", eval_native);
	lit_define_native(state, "print", print_native);
}