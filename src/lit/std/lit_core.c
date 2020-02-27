#include <lit/std/lit_core.h>
#include <lit/std/lit_math.h>
#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>

#include <time.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

void lit_open_libraries(LitState* state) {
	lit_open_math_library(state);
}

/*
 * Class
 */

LIT_METHOD(class_super) {
	LitClass* super = AS_CLASS(instance)->super;

	if (super == NULL) {
		return NULL_VALUE;
	}

	return OBJECT_VALUE(super);
}

LIT_METHOD(class_name) {
	return OBJECT_VALUE(AS_CLASS(instance)->name);
}

/*
 * Object
 */

LIT_METHOD(object_class) {
	return OBJECT_VALUE(lit_get_class_for(vm->state, instance));
}

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

LIT_METHOD(string_replace) {
	LIT_ENSURE_ARGS(2)

	if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
		lit_runtime_error(vm, "Expected 2 string arguments");
		return NULL_VALUE;
	}

	LitString* string = AS_STRING(instance);
	LitString* what = AS_STRING(args[0]);
	LitString* with = AS_STRING(args[1]);

	uint buffer_length = 0;

	for (uint i = 0; i < string->length; i++) {
		if (strncmp(string->chars + i, what->chars, what->length) == 0) {
			i += what->length - 1;
			buffer_length += with->length;
		} else {
			buffer_length++;
		}
	}

	uint buffer_index = 0;
	char buffer[buffer_length];

	for (uint i = 0; i < string->length; i++) {
		if (strncmp(string->chars + i, what->chars, what->length) == 0) {
			memcpy(buffer + buffer_index, with->chars, with->length);

			buffer_index += with->length;
			i += what->length - 1;
		} else {
			buffer[buffer_index] = string->chars[i];
			buffer_index++;
		}
	}

	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, buffer_length));
}

LIT_METHOD(string_subscript) {
	LitString* string = AS_STRING(instance);
	int index = LIT_CHECK_NUMBER(0);

	if (arg_count != 1) {
		lit_runtime_error(vm, "Can't modify strings with the subscript operator");
		return NULL_VALUE;
	}

	if (index < 0) {
		index = fmax(0, string->length + index);
	}

	return OBJECT_VALUE(lit_copy_string(vm->state, &string->chars[index], 1));
}

LIT_METHOD(string_length) {
	return NUMBER_VALUE(AS_STRING(instance)->length);
}

/*
 * Function
 */

LIT_METHOD(function_name) {
	if (!IS_FUNCTION(instance)) {
		return NULL_VALUE;
	}

	return OBJECT_VALUE(AS_FUNCTION(instance)->name);
}

/*
 * Module
 */

LIT_METHOD(module_name) {
	return OBJECT_VALUE(AS_MODULE(instance)->name);
}

/*
 * Array
 */

LIT_METHOD(array_add) {
	LIT_ENSURE_ARGS(1)

	LitArray* array = AS_ARRAY(instance);
	lit_values_write(vm->state, &array->values, args[0]);

	return NULL_VALUE;
}

LIT_METHOD(array_addAll) {
	LIT_ENSURE_ARGS(1)

	if (!IS_ARRAY(args[0])) {
		lit_runtime_error(vm, "Expected array as the argument");
		return NULL_VALUE;
	}

	LitArray* array = AS_ARRAY(instance);
	LitArray* toAdd = AS_ARRAY(args[0]);

	for (uint i = 0; i < toAdd->values.count; i++) {
		lit_values_write(vm->state, &array->values, toAdd->values.values[i]);
	}

	return NULL_VALUE;
}

static int indexOf(LitArray* array, LitValue value) {
	for (uint i = 0; i < array->values.count; i++) {
		if (array->values.values[i] == value) {
			return (int) i;
		}
	}

	return -1;
}

LIT_METHOD(array_indexOf) {
	LIT_ENSURE_ARGS(1)
	return NUMBER_VALUE(indexOf(AS_ARRAY(instance), args[0]));
}

static void removeAt(LitArray* array, uint index) {
	LitValues* values = &array->values;
	uint count = values->count;

	if (index >= count) {
		return;
	} else if (index == count - 1) {
		values->values[count - 1] = NULL_VALUE;
	} else {
		for (uint i = count - 2; i <= index; i++) {
			values->values[i] = values->values[i + 1];
		}
	}

	values->count--;
}

LIT_METHOD(array_remove) {
	LIT_ENSURE_ARGS(1)

	LitArray* array = AS_ARRAY(instance);
	int index = indexOf(array, args[0]);

	if (index != -1) {
		removeAt(array, (uint) index);
	}

	return NULL_VALUE;
}

LIT_METHOD(array_removeAt) {
	int index = LIT_CHECK_NUMBER(0);

	if (index < 0) {
		return NULL_VALUE;
	}

	removeAt(AS_ARRAY(instance), (uint) index);
	return NULL_VALUE;
}

LIT_METHOD(array_contains) {
	LIT_ENSURE_ARGS(1)
	return BOOL_VALUE(indexOf(AS_ARRAY(instance), args[0]) != -1);
}

LIT_METHOD(array_clear) {
	AS_ARRAY(instance)->values.count = 0;
	return NULL_VALUE;
}

LIT_METHOD(array_iterator) {
	LIT_ENSURE_ARGS(1)

	LitArray* array = AS_ARRAY(instance);
	int number = 0;

	if (IS_NUMBER(args[0])) {
		number = AS_NUMBER(args[0]);

		if (number >= array->values.count - 1) {
			return NULL_VALUE;
		}

		number++;
	}

	return NUMBER_VALUE(number);
}

LIT_METHOD(array_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	return AS_ARRAY(instance)->values.values[index];
}

LIT_METHOD(array_length) {
	return NUMBER_VALUE(AS_ARRAY(instance)->values.count);
}

/*
 * Map
 */

LIT_METHOD(map_addAll) {
	LIT_ENSURE_ARGS(1)

	if (!IS_MAP(args[0])) {
		lit_runtime_error(vm, "Expected map as the argument");
		return NULL_VALUE;
	}

	lit_map_add_all(vm->state, AS_MAP(args[0]), AS_MAP(instance));
	return NULL_VALUE;
}

LIT_METHOD(map_clear) {
	LitMap* map = AS_MAP(instance);

	map->values.count = 0;
	map->key_list->values.count = 0;

	return NULL_VALUE;
}

LIT_METHOD(map_iterator) {
	LIT_ENSURE_ARGS(1)

	LitMap* map = AS_MAP(instance);
	int number = 0;

	if (IS_NUMBER(args[0])) {
		number = AS_NUMBER(args[0]);

		if (number >= map->values.count - 1) {
			return NULL_VALUE;
		}

		number++;
	}

	return NUMBER_VALUE(number);
}

LIT_METHOD(map_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	return AS_MAP(instance)->key_list->values.values[index];
}

LIT_METHOD(map_length) {
	return NUMBER_VALUE(AS_MAP(instance)->values.count);
}

LIT_METHOD(map_keys) {
	return OBJECT_VALUE(AS_MAP(instance)->key_list);
}

/*
 * Range
 */

LIT_METHOD(range_iterator) {
	LIT_ENSURE_ARGS(1)

	LitRange* range = AS_RANGE(instance);
	int number = range->from;

	if (IS_NUMBER(args[0])) {
		number = AS_NUMBER(args[0]);

		if (range->to > range->from ? number >= range->to : number >= range->from) {
			return NULL_VALUE;
		}

		number += (range->from - range->to) > 0 ? -1 : 1;
	}

	return NUMBER_VALUE(number);
}

LIT_METHOD(range_iteratorValue) {
	LIT_ENSURE_ARGS(1)
	return args[0];
}

LIT_METHOD(range_from) {
	return NUMBER_VALUE(AS_RANGE(instance)->from);
}

LIT_METHOD(range_set_from) {
	AS_RANGE(instance)->from = AS_NUMBER(args[0]);
	return args[0];
}

LIT_METHOD(range_to) {
	return NUMBER_VALUE(AS_RANGE(instance)->to);
}

LIT_METHOD(range_set_to) {
	AS_RANGE(instance)->to = AS_NUMBER(args[0]);
	return args[0];
}

LIT_METHOD(range_length) {
	LitRange* range = AS_RANGE(instance);
	return NUMBER_VALUE(range->to - range->from);
}

/*
 * Natives
 */

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
		LIT_BIND_STATIC_GETTER("super", class_super)
		LIT_BIND_STATIC_GETTER("name", class_name)

		state->class_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Object")
		LIT_INHERIT_CLASS(state->class_class)

		LIT_BIND_METHOD("toString", object_toString)
		LIT_BIND_GETTER("class", object_class)

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
		LIT_BIND_METHOD("replace", string_replace)
		LIT_BIND_METHOD("[]", string_subscript)

		LIT_BIND_GETTER("length", string_length)

		state->string_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Bool")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_METHOD("toString", bool_toString)
		state->bool_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Function")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_GETTER("name", function_name)

		state->function_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Fiber")
		LIT_INHERIT_CLASS(state->object_class)
		state->fiber_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Module")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_GETTER("name", module_name)

		state->module_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Array")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_METHOD("add", array_add)
		LIT_BIND_METHOD("addAll", array_addAll)
		LIT_BIND_METHOD("remove", array_remove)
		LIT_BIND_METHOD("removeAt", array_removeAt)
		LIT_BIND_METHOD("indexOf", array_indexOf)
		LIT_BIND_METHOD("contains", array_contains)
		LIT_BIND_METHOD("clear", array_clear)
		LIT_BIND_METHOD("iterator", array_iterator)
		LIT_BIND_METHOD("iteratorValue", array_iteratorValue)

		LIT_BIND_GETTER("length", array_length)

		state->array_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Map")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_METHOD("addAll", map_addAll)
		LIT_BIND_METHOD("clear", map_clear)
		LIT_BIND_METHOD("iterator", map_iterator)
		LIT_BIND_METHOD("iteratorValue", map_iteratorValue)

		LIT_BIND_GETTER("length", map_length)
		LIT_BIND_GETTER("keys", map_keys)

		state->map_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Range")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_METHOD("iterator", range_iterator)
		LIT_BIND_METHOD("iteratorValue", range_iteratorValue)

		LIT_BIND_FIELD("from", range_from, range_set_from)
		LIT_BIND_FIELD("to", range_to, range_set_to)
		LIT_BIND_GETTER("length", range_length)

		state->range_class = klass;
	LIT_END_CLASS()

	lit_define_native(state, "time", time_native);
	lit_define_native(state, "eval", eval_native);
	lit_define_native(state, "print", print_native);
}