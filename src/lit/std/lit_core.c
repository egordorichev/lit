#include <lit/std/lit_core.h>
#include <lit/std/lit_math.h>
#include <lit/std/lit_file.h>
#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>
#include <lit/util/lit_fs.h>

#include <time.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

void lit_open_libraries(LitState* state) {
	lit_open_math_library(state);
	lit_open_file_library(state);
}

/*
 * Class
 */

LIT_METHOD(class_toString) {
	return OBJECT_VALUE(lit_string_format(vm->state, "class @", OBJECT_VALUE(AS_CLASS(instance)->name)));
}

static int table_iterator(LitTable* table, int number) {
	if (table->count == 0) {
		return -1;
	}

	if (number != -1) {
		if (number >= (int) table->capacity) {
			return -1;
		}

		number++;
	}

	for (; number < table->capacity; number++) {
		if (table->entries[number].key != NULL) {
			return number;
		}
	}

	return -1;
}

static LitValue table_iterator_key(LitTable* table, int index) {
	if (table->capacity <= index) {
		return NULL_VALUE;
	}

	return OBJECT_VALUE(table->entries[index].key);
}

LIT_METHOD(class_iterator) {
	LIT_ENSURE_ARGS(1)

	LitClass* klass = AS_CLASS(instance);
	int index = args[0] == NULL_VALUE ? 0 : AS_NUMBER(args[0]);
	int methodsCapacity = (int) klass->methods.capacity;
	bool fields = index >= methodsCapacity;

	int value = table_iterator(fields ? &klass->static_fields : &klass->methods, fields ? index - methodsCapacity : index);

	if (value == -1) {
		if (fields) {
			return NULL_VALUE;
		}

		index++;
		fields = true;
		value = table_iterator(&klass->static_fields, index - methodsCapacity);
	}

	return value == -1 ? NULL_VALUE : NUMBER_VALUE(fields ? value + methodsCapacity : value);
}

LIT_METHOD(class_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	LitClass* klass = AS_CLASS(instance);
	uint methodsCapacity = klass->methods.capacity;
	bool fields = index >= methodsCapacity;

	return table_iterator_key(fields ? &klass->static_fields : &klass->methods, fields ? index - methodsCapacity : index);
}

LIT_METHOD(class_super) {
	LitClass* super = NULL;

	if (IS_INSTANCE(instance)) {
		super = AS_INSTANCE(instance)->klass->super;
	} else {
		super = AS_CLASS(instance)->super;
	}

	if (super == NULL) {
		return NULL_VALUE;
	}

	return OBJECT_VALUE(super);
}

LIT_METHOD(class_subscript) {
	LitClass* klass = AS_CLASS(instance);

	if (arg_count == 2) {
		if (!IS_STRING(args[0])) {
			lit_runtime_error(vm, "Class index must be a string");
			return NULL_VALUE;
		}

		lit_table_set(vm->state, &klass->static_fields, AS_STRING(args[0]), args[1]);
		return args[1];
	}

	if (!IS_STRING(args[0])) {
		lit_runtime_error(vm, "Class index must be a string");
		return NULL_VALUE;
	}

	LitValue value;

	if (lit_table_get(&klass->static_fields, AS_STRING(args[0]), &value)) {
		return value;
	}

	if (lit_table_get(&klass->methods, AS_STRING(args[0]), &value)) {
		return value;
	}

	return NULL_VALUE;
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

LIT_METHOD(object_subscript) {
	if (!IS_INSTANCE(instance)) {
		lit_runtime_error(vm, "Can't modify built-in types");
		return NULL_VALUE;
	}

	LitInstance* inst = AS_INSTANCE(instance);

	if (arg_count == 2) {
		if (!IS_STRING(args[0])) {
			lit_runtime_error(vm, "Object index must be a string");
			return NULL_VALUE;
		}

		lit_table_set(vm->state, &inst->fields, AS_STRING(args[0]), args[1]);
		return args[1];
	}

	if (!IS_STRING(args[0])) {
		lit_runtime_error(vm, "Object index must be a string");
		return NULL_VALUE;
	}

	LitValue value;

	if (lit_table_get(&inst->fields, AS_STRING(args[0]), &value)) {
		return value;
	}

	if (lit_table_get(&inst->klass->static_fields, AS_STRING(args[0]), &value)) {
		return value;
	}

	if (lit_table_get(&inst->klass->methods, AS_STRING(args[0]), &value)) {
		return value;
	}

	return NULL_VALUE;
}

LIT_METHOD(object_iterator) {
	LIT_ENSURE_ARGS(1)

	LitInstance* self = AS_INSTANCE(instance);
	int index = args[0] == NULL_VALUE ? 0 : AS_NUMBER(args[0]);
	int methodsCapacity = (int) self->klass->methods.capacity;
	bool fields = index >= methodsCapacity;

	int value = table_iterator(fields ? &self->fields : &self->klass->methods, fields ? index - methodsCapacity : index);

	if (value == -1) {
		if (fields) {
			return NULL_VALUE;
		}

		index++;
		fields = true;
		value = table_iterator(&self->fields, index - methodsCapacity);
	}

	return value == -1 ? NULL_VALUE : NUMBER_VALUE(fields ? value + methodsCapacity : value);
}

LIT_METHOD(object_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	LitInstance* self = AS_INSTANCE(instance);
	uint methodsCapacity = self->klass->methods.capacity;
	bool fields = index >= methodsCapacity;

	return table_iterator_key(fields ? &self->fields : &self->klass->methods, fields ? index - methodsCapacity : index);
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
	return OBJECT_CONST_STRING(vm->state, AS_BOOL(instance) ? "true" : "false");
}

/*
 * String
 */

LIT_METHOD(string_plus) {
	LitString* string = AS_STRING(instance);

	LitValue value = args[0];
	LitString* string_value = NULL;

	if (IS_STRING(value)) {
		string_value = AS_STRING(value);
	} else {
		string_value = lit_to_string(vm->state, value);
	}

	uint length = string->length + string_value->length;
	LitString* result = lit_allocate_empty_string(vm->state, length);

	result->chars = LIT_ALLOCATE(vm->state, char, length + 1);
	result->chars[length] = '\0';

	memcpy(result->chars, string->chars, string->length);
	memcpy(result->chars + string->length, string_value->chars, string_value->length);

	result->hash = lit_hash_string(result->chars, result->length);
	lit_register_string(vm->state, result);

	return OBJECT_VALUE(result);
}

LIT_METHOD(string_toString) {
	return instance;
}

LIT_METHOD(string_toNumber) {
	double result = strtod(AS_STRING(instance)->chars, NULL);

	if (errno == ERANGE) {
		errno = 0;
		return NULL_VALUE;
	}

	return NUMBER_VALUE(result);
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
	char buffer[buffer_length + 1];

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

	buffer[buffer_length] = '\0';

	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, buffer_length));
}

static LitValue string_splice(LitVm* vm, LitString* string, int from, int to) {
	if (from < 0) {
		from = (int) string->length + from;
	}

	if (to < 0) {
		to = (int) string->length + to;
	}

	if (from > to) {
		lit_runtime_error(vm, "String splice from bound is larger that to bound");
	}

	from = fmax(from, 0);
	to = fmin(to, (int) string->length - 1);

	int length = fmin(string->length, to - from + 1);
	char buffer[length + 1];

	for (int i = 0; i < length; i++) {
		buffer[i] = string->chars[from + i];
	}

	buffer[length] = '\0';
	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, length));
}

LIT_METHOD(string_substring) {
	int from = LIT_CHECK_NUMBER(0);
	int to = LIT_CHECK_NUMBER(1);

	return string_splice(vm, AS_STRING(instance), from, to);
}

LIT_METHOD(string_subscript) {
	if (IS_RANGE(args[0])) {
		LitRange* range = AS_RANGE(args[0]);
		return string_splice(vm, AS_STRING(instance), range->from, range->to);
	}

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

LIT_METHOD(function_toString) {
	return lit_get_function_name(vm, instance);
}

LIT_METHOD(function_name) {
	return lit_get_function_name(vm, instance);
}

/*
 * Fiber
 */

LIT_METHOD(fiber_constructor) {
	if (arg_count < 1 || !IS_FUNCTION(args[0])) {
		lit_runtime_error(vm, "Fiber constructor expects a function as its argument");
		return NULL_VALUE;
	}

	LitFunction* function = AS_FUNCTION(args[0]);
	LitModule* module = vm->fiber->module;
	LitFiber* fiber = lit_create_fiber(vm->state, module, function);

	fiber->parent = vm->fiber;

	return OBJECT_VALUE(fiber);
}

static bool is_fiber_done(LitFiber* fiber) {
	return fiber->frame_count == 0 || fiber->abort;
}

LIT_METHOD(fiber_done) {
	return BOOL_VALUE(is_fiber_done(AS_FIBER(instance)));
}

LIT_METHOD(fiber_error) {
	return AS_FIBER(instance)->error;
}

LIT_METHOD(fiber_current) {
	return OBJECT_VALUE(vm->fiber);
}

static void run_fiber(LitVm* vm, LitFiber* fiber, LitValue* args, uint arg_count, bool catcher) {
	if (is_fiber_done(fiber)) {
		lit_runtime_error(vm, "Fiber already finished executing");
		return;
	}

	fiber->parent = vm->fiber;
	fiber->catcher = catcher;

	vm->fiber = fiber;

	LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];

	if (frame->ip == frame->function->chunk.code) {
		fiber->arg_count = arg_count;
		lit_ensure_fiber_stack(vm->state, fiber, frame->function->max_slots + 1 + (int) (fiber->stack_top - fiber->stack));

		frame->slots = fiber->stack_top;
		lit_push(vm, OBJECT_VALUE(frame->function));

		bool vararg = frame->function->vararg;
		int function_arg_count = frame->function->arg_count;
		int to = function_arg_count - (vararg ? 1 : 0);

		fiber->arg_count = function_arg_count;

		for (int i = 0; i < to; i++) {
			lit_push(vm, i < (int) arg_count ? args[i] : NULL_VALUE);
		}

		if (vararg) {
			LitArray* array = lit_create_array(vm->state);
			lit_push(vm, OBJECT_VALUE(array));

			int vararg_count = arg_count - function_arg_count + 1;

			if (vararg_count > 0) {
				lit_values_ensure_size(vm->state, &array->values, vararg_count);

				for (int i = 0; i < vararg_count; i++) {
					array->values.values[i] = args[i + function_arg_count - 1];
				}
			}
		}
	}
}

LIT_PRIMITIVE(fiber_run) {
	run_fiber(vm, AS_FIBER(instance), args, arg_count, false);
	return true;
}

LIT_PRIMITIVE(fiber_try) {
	run_fiber(vm, AS_FIBER(instance), args, arg_count, true);
	return true;
}

LIT_PRIMITIVE(fiber_yield) {
	if (vm->fiber->parent == NULL) {
		lit_handle_runtime_error(vm, arg_count == 0 ? CONST_STRING(vm->state, "Fiber was yielded") : lit_to_string(vm->state, args[0]));
		return true;
	}

	LitFiber* fiber = vm->fiber;

	vm->fiber = vm->fiber->parent;
	vm->fiber->stack_top -= fiber->arg_count;
	vm->fiber->stack_top[-1] = arg_count == 0 ? NULL_VALUE : OBJECT_VALUE(lit_to_string(vm->state, args[0]));

	args[-1] = NULL_VALUE;
	return true;
}

LIT_PRIMITIVE(fiber_yeet) {
	if (vm->fiber->parent == NULL) {
		lit_handle_runtime_error(vm, arg_count == 0 ? CONST_STRING(vm->state, "Fiber was yeeted") : lit_to_string(vm->state, args[0]));
		return true;
	}

	LitFiber* fiber = vm->fiber;

	vm->fiber = vm->fiber->parent;
	vm->fiber->stack_top -= fiber->arg_count;
	vm->fiber->stack_top[-1] = arg_count == 0 ? NULL_VALUE : OBJECT_VALUE(lit_to_string(vm->state, args[0]));

	args[-1] = NULL_VALUE;
	return true;
}

LIT_PRIMITIVE(fiber_abort) {
	lit_handle_runtime_error(vm, arg_count == 0 ? CONST_STRING(vm->state, "Fiber was aborted") : lit_to_string(vm->state, args[0]));
	args[-1] = NULL_VALUE;
	return true;
}

/*
 * Module
 */

LitValue access_private(LitVm* vm, struct sLitMap* map, LitString* index) {
	LitValue value;

	if (lit_table_get(&vm->fiber->module->private_names->values, index, &value)) {
		return vm->fiber->module->privates[(int) AS_NUMBER(value)];
	}

	return NULL_VALUE;
}

LIT_METHOD(module_privates) {
	LitMap* map = vm->fiber->module->private_names;
	map->index_fn = access_private;

	return OBJECT_VALUE(map);
}

LIT_METHOD(module_toString) {
	return OBJECT_VALUE(lit_string_format(vm->state, "Module @", OBJECT_VALUE(AS_MODULE(instance)->name)));
}

LIT_METHOD(module_name) {
	return OBJECT_VALUE(AS_MODULE(instance)->name);
}

/*
 * Array
 */

static LitValue array_splice(LitVm* vm, LitArray* array, int from, int to) {
	uint length = array->values.count;

	if (from < 0) {
		from = (int) length + from;
	}

	if (to < 0) {
		to = (int) length + to;
	}

	if (from > to) {
		lit_runtime_error(vm, "String splice from bound is larger that to bound");
	}

	from = fmax(from, 0);
	to = fmin(to, (int) length - 1);

	length = fmin(length, to - from + 1);
	LitArray* new_array = lit_create_array(vm->state);

	for (uint i = 0; i < length; i++) {
		lit_values_write(vm->state, &new_array->values,  array->values.values[from + i]);
	}

	return OBJECT_VALUE(new_array);
}

LIT_METHOD(array_slice) {
	int from = LIT_CHECK_NUMBER(0);
	int to = LIT_CHECK_NUMBER(1);

	return array_splice(vm, AS_ARRAY(instance), from, to);
}

LIT_METHOD(array_subscript) {
	if (arg_count == 2) {
		if (!IS_NUMBER(args[0])) {
			lit_runtime_error(vm, "Array index must be a number");
		}

		LitValues* values = &AS_ARRAY(instance)->values;
		int index = AS_NUMBER(args[0]);

		if (index < 0) {
			index = fmax(0, values->count + index);
		}

		lit_values_ensure_size(vm->state, values, index + 1);
		return values->values[index] = args[1];
	}

	if (!IS_NUMBER(args[0])) {
		if (IS_RANGE(args[0])) {
			LitRange* range = AS_RANGE(args[0]);
			return array_splice(vm, AS_ARRAY(instance), (int) range->from, (int) range->to);
		}

		lit_runtime_error(vm, "Array index must be a number");
		return NULL_VALUE;
	}

	LitValues* values = &AS_ARRAY(instance)->values;
	int index = AS_NUMBER(args[0]);

	if (index < 0) {
		index = fmax(0, values->count + index);
	}

	if (values->capacity <= (uint) index) {
		return NULL_VALUE;
	}

	return values->values[index];
}

LIT_METHOD(array_add) {
	LIT_ENSURE_ARGS(1)
	lit_values_write(vm->state, &AS_ARRAY(instance)->values, args[0]);

	return NULL_VALUE;
}

LIT_METHOD(array_insert) {
	LIT_ENSURE_ARGS(2)

	LitValues* values = &AS_ARRAY(instance)->values;
	int index = LIT_CHECK_NUMBER(0);

	if (index < 0) {
		index = fmax(0, values->count + index);
	}

	LitValue value = args[1];

	if ((int) values->count <= index) {
		lit_values_ensure_size(vm->state, values, index + 1);
	} else {
		lit_values_ensure_size(vm->state, values, values->count + 1);

		for (int i = values->count - 1; i > index; i--) {
			values->values[i] = values->values[i - 1];
		}
	}

	values->values[index] = value;
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

	int index = indexOf(AS_ARRAY(instance), args[0]);
	return index == -1 ? NULL_VALUE : NUMBER_VALUE(index);
}

static LitValue removeAt(LitArray* array, uint index) {
	LitValues* values = &array->values;
	uint count = values->count;

	if (index >= count) {
		return NULL_VALUE;
	}

	LitValue value = values->values[index];

	if (index == count - 1) {
		values->values[index] = NULL_VALUE;
	} else {
		for (uint i = index; i < values->count - 1; i++) {
			values->values[i] = values->values[i + 1];
		}

		values->values[count - 1] = NULL_VALUE;
	}

	values->count--;
	return value;
}

LIT_METHOD(array_remove) {
	LIT_ENSURE_ARGS(1)

	LitArray* array = AS_ARRAY(instance);
	int index = indexOf(array, args[0]);

	if (index != -1) {
		return removeAt(array, (uint) index);
	}

	return NULL_VALUE;
}

LIT_METHOD(array_removeAt) {
	int index = LIT_CHECK_NUMBER(0);

	if (index < 0) {
		return NULL_VALUE;
	}

	return removeAt(AS_ARRAY(instance), (uint) index);
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

		if (number >= (int) array->values.count - 1) {
			return NULL_VALUE;
		}

		number++;
	}

	return array->values.count == 0 ? NULL_VALUE : NUMBER_VALUE(number);
}

LIT_METHOD(array_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	LitValues* values = &AS_ARRAY(instance)->values;

	if (values->count <= index) {
		return NULL_VALUE;
	}

	return values->values[index];
}

LIT_METHOD(array_join) {
	LitValues* values = &AS_ARRAY(instance)->values;
	LitString* strings[values->count];

	uint length = 0;

	for (uint i = 0; i < values->count; i++) {
		LitString* string = lit_to_string(vm->state, values->values[i]);

		strings[i] = string;
		length += string->length;
	}

	uint index = 0;

	char chars[length + 1];
	chars[length] = '\0';

	for (uint i = 0; i < values->count; i++) {
		LitString* string = strings[i];

		memcpy(chars + index, string->chars, string->length);
		index += string->length;
	}

	return OBJECT_VALUE(lit_copy_string(vm->state, chars, length));
}

LIT_METHOD(array_clone) {
	LitState* state = vm->state;
	LitValues *values = &AS_ARRAY(instance)->values;
	LitArray* array = lit_create_array(state);
	LitValues* new_values = &array->values;

	lit_values_ensure_size(state, new_values, values->count);

	// lit_values_ensure_size sets the count to max of previous count (0 in this case) and new count, so we have to reset it
	new_values->count = 0;

	for (uint i = 0; i < values->count; i++) {
		lit_values_write(state, new_values, values->values[i]);
	}

	return OBJECT_VALUE(array);
}

LIT_METHOD(array_toString) {
	LitValues* values = &AS_ARRAY(instance)->values;
	LitState* state = vm->state;

	if (values->count == 0) {
		return OBJECT_CONST_STRING(state, "[]");
	}

	bool has_more = values->count > LIT_CONTAINER_OUTPUT_MAX;
	uint value_amount = has_more ? LIT_CONTAINER_OUTPUT_MAX : values->count;
	LitString* values_converted[value_amount];

	uint string_length = 3; // "[ ]"

	if (has_more) {
		string_length += 3;
	}

	for (uint i = 0; i < value_amount; i++) {
		LitString* value = lit_to_string(state, values->values[(has_more && i == value_amount - 1) ? values->count - 1 : i]);

		values_converted[i] = value;
		string_length += value->length + (i == value_amount - 1 ? 1 : 2);
	}

	char buffer[string_length + 1];
	memcpy(buffer, "[ ", 2);

	uint buffer_index = 2;

	for (uint i = 0; i < value_amount; i++) {
		LitString* part = values_converted[i];

		memcpy(&buffer[buffer_index], part->chars, part->length);
		buffer_index += part->length;

		if (has_more && i == value_amount - 2) {
			memcpy(&buffer[buffer_index], " ... ", 5);
			buffer_index += 5;
		} else {
			memcpy(&buffer[buffer_index], (i == value_amount - 1) ? " ]" : ", ", 2);
			buffer_index += 2;
		}
	}

	buffer[string_length] = '\0';
	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, string_length));
}

LIT_METHOD(array_length) {
	return NUMBER_VALUE(AS_ARRAY(instance)->values.count);
}

/*
 * Map
 */

LIT_METHOD(map_subscript) {
	if (arg_count == 2) {
		if (!IS_STRING(args[0])) {
			lit_runtime_error(vm, "Map index must be a string");
			return NULL_VALUE;
		}

		lit_map_set(vm->state, AS_MAP(instance), AS_STRING(args[0]), args[1]);
		return args[1];
	}

	if (!IS_STRING(args[0])) {
		lit_runtime_error(vm, "Map index must be a string");
		return NULL_VALUE;
	}

	LitValue value;
	LitMap* map = AS_MAP(instance);
	LitString* index = AS_STRING(args[0]);

	if (map->index_fn != NULL) {
		return map->index_fn(vm, map, index);
	}

	if (!lit_table_get(&map->values, index, &value)) {
		return NULL_VALUE;
	}

	return value;
}

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
	AS_MAP(instance)->values.count = 0;
	return NULL_VALUE;
}

LIT_METHOD(map_iterator) {
	LIT_ENSURE_ARGS(1)
	int index = args[0] == NULL_VALUE ? 0 : AS_NUMBER(args[0]);

	int value = table_iterator(&AS_MAP(instance)->values, index);
	return value == -1 ? NULL_VALUE : NUMBER_VALUE(value);
}

LIT_METHOD(map_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	return table_iterator_key(&AS_MAP(instance)->values, index);
}

LIT_METHOD(map_clone) {
	LitState* state = vm->state;
	LitMap* map = lit_create_map(state);

	lit_table_add_all(state, &AS_MAP(instance)->values, &map->values);

	return OBJECT_VALUE(map);
}

LIT_METHOD(map_toString) {
	LitState* state = vm->state;
	LitMap* map = AS_MAP(instance);
	LitTable* values = &map->values;

	if (values->count == 0) {
		return OBJECT_CONST_STRING(state, "{}");
	}

	bool has_wrapper = map->index_fn != NULL;
	bool has_more = values->count > LIT_CONTAINER_OUTPUT_MAX;
	uint value_amount = has_more ? LIT_CONTAINER_OUTPUT_MAX : values->count;

	LitString* values_converted[value_amount];
	LitString* keys[value_amount];
	uint string_length = 3;

	if (has_more) {
		string_length += SINGLE_LINE_MAPS_ENABLED ? 5 : 6;
	}

	uint i = 0;
	uint index = 0;

	do {
		LitTableEntry* entry = &values->entries[index++];

		if (entry->key != NULL) {
			LitValue field = has_wrapper ? map->index_fn(vm, map, entry->key) : entry->value;
			// This check is required to prevent infinite loops when playing with Module.privates and such
			LitString* value = (IS_MAP(field) && AS_MAP(field)->index_fn != NULL) ? CONST_STRING(state, "map") : lit_to_string(state, field);
			lit_push_root(state, (LitObject*) value);

			values_converted[i] = value;
			keys[i] = entry->key;
			string_length += entry->key->length + 3 + value->length +
				#ifdef SINGLE_LINE_MAPS
					(i == value_amount - 1 ? 1 : 2);
				#else
					(i == value_amount - 1 ? 2 : 3);
				#endif

			i++;
		}
	} while (i < value_amount);

	char buffer[string_length + 1];

	#ifdef SINGLE_LINE_MAPS
		memcpy(buffer, "{ ", 2);
	#else
		memcpy(buffer, "{\n", 2);
	#endif

	uint buffer_index = 2;

	for (i = 0; i < value_amount; i++) {
		LitString *key = keys[i];
		LitString *value = values_converted[i];

		#ifndef SINGLE_LINE_MAPS
			buffer[buffer_index++] = '\t';
		#endif

		memcpy(&buffer[buffer_index], key->chars, key->length);
		buffer_index += key->length;

		memcpy(&buffer[buffer_index], " : ", 3);
		buffer_index += 3;

		memcpy(&buffer[buffer_index], value->chars, value->length);
		buffer_index += value->length;

		if (has_more && i == value_amount - 1) {
			#ifdef SINGLE_LINE_MAPS
				memcpy(&buffer[buffer_index], ", ... }", 7);
			#else
				memcpy(&buffer[buffer_index], ",\n\t...\n}", 8);
			#endif
			buffer_index += 8;
		} else {
			#ifdef SINGLE_LINE_MAPS
				memcpy(&buffer[buffer_index], (i == value_amount - 1) ? " }" : ", ", 2);
			#else
				memcpy(&buffer[buffer_index], (i == value_amount - 1) ? "\n}" : ",\n", 2);
			#endif

			buffer_index += 2;
		}

		lit_pop_root(state);
	}

	buffer[string_length] = '\0';
	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, string_length));
}

LIT_METHOD(map_length) {
	return NUMBER_VALUE(AS_MAP(instance)->values.count);
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

LIT_METHOD(range_toString) {
	LitRange* range = AS_RANGE(instance);
	return OBJECT_VALUE(lit_string_format(vm->state, "Range(#, #)", range->from, range->to));
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

LIT_NATIVE(systemTime) {
	return NUMBER_VALUE(time(NULL));
}

LIT_NATIVE(print) {
	if (arg_count == 0) {
		return NULL_VALUE;
	}

	for (uint i = 0; i < arg_count; i++) {
		printf("%s\n", lit_to_string(vm->state, args[i])->chars);
	}

	return NULL_VALUE;
}

static bool interpret(LitVm* vm, LitModule* module) {
	LitFunction* function = module->main_function;
	LitFiber* fiber = lit_create_fiber(vm->state, module, function);

	fiber->parent = vm->fiber;
	vm->fiber = fiber;

	LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];

	if (frame->ip == frame->function->chunk.code) {
		frame->slots = fiber->stack_top;
		lit_push(vm, OBJECT_VALUE(frame->function));
	}

	return true;
}

static bool compile_and_interpret(LitVm* vm, LitString* module_name, const char* source) {
	LitModule *module = lit_compile_module(vm->state, module_name, source);

	if (module == NULL) {
		return false;
	}

	module->ran = true;
	return interpret(vm, module);
}

LIT_NATIVE_PRIMITIVE(eval) {
	const char* code = LIT_CHECK_STRING(0);
	return compile_and_interpret(vm, vm->fiber->module->name, code);
}

static bool file_exists(const char* filename) {
	struct stat buffer;
	return stat(filename, &buffer) == 0;
}

static bool should_update_locals;

static bool attempt_to_require(LitVm* vm, LitValue* args, uint arg_count, const char* path, bool ignore_previous) {
	should_update_locals = false;

	size_t length = strlen(path);
	char module_name[length + 5];
	char module_name_dotted[length + 5];

	memcpy((void*) module_name_dotted, path, length);
	memcpy((void*) module_name_dotted + length, ".lit", 4);

	module_name_dotted[length + 4] = '\0';

	for (uint i = 0; i < length + 5; i++) {
		char c = module_name_dotted[i];

		if (c == '.' || c == '\\') {
			module_name[i] = '/';
		} else {
			module_name[i] = c;
		}
	}

	module_name[length] = '.';

	if (!file_exists(module_name)) {
		// .lit -> .lbc
		memcpy((void*) module_name + length + 2, "bc", 2);
		memcpy((void*) module_name_dotted + length + 2, "bc", 2);

		if (!file_exists(module_name)) {
			return false;
		}
	}

	LitString *name = lit_copy_string(vm->state, module_name_dotted, length);

	if (!ignore_previous) {
		LitValue existing_module;

		if (lit_table_get(&vm->modules->values, name, &existing_module)) {
			LitModule* loaded_module = AS_MODULE(existing_module);

			if (loaded_module->ran) {
				vm->fiber->stack_top -= arg_count;
				args[-1] = AS_MODULE(existing_module)->return_value;
			} else {
				if (interpret(vm, loaded_module)) {
					should_update_locals = true;
				}
			}

			return true;
		}
	}

	const char* source = lit_read_file(module_name);

	if (source == NULL) {
		return false;
	}

	if (compile_and_interpret(vm, name, source)) {
		should_update_locals = true;
	}

	return true;
}

static bool attempt_to_require_combined(LitVm* vm, LitValue* args, uint arg_count, const char* a, const char* b, bool ignore_previous) {
	size_t a_length = strlen(a);
	size_t b_length = strlen(b);
	size_t total_length = a_length + b_length + 1;

	char path[total_length + 1];

	memcpy((void*) path, a, a_length);
	memcpy((void*) path + a_length + 1, b, b_length);

	path[a_length] = '.';
	path[total_length] = '\0';

	return attempt_to_require(vm, args, arg_count, (const char*) &path, ignore_previous);
}

LIT_NATIVE_PRIMITIVE(require) {
	LitString* name = LIT_CHECK_OBJECT_STRING(0);
	bool ignore_previous = arg_count > 1 && IS_BOOL(args[1]) && AS_BOOL(args[1]);

	// First check, if a file with this name exists in the local path
	if (attempt_to_require(vm, args, arg_count, name->chars, ignore_previous)) {
		return should_update_locals;
	}

	// If not, we join the path of the current module to it (the path goes all the way from the root)
	LitString* module_name = vm->fiber->module->name;

	// We need to get rid of the module name (test.folder.module -> test.folder)
	char* index = strrchr(module_name->chars, '.');

	if (index != NULL) {
		size_t length = index - module_name->chars;

		char buffer[length + 1];
		memcpy((void*) buffer, module_name->chars, length);
		buffer[length] = '\0';

		if (attempt_to_require_combined(vm, args, arg_count, (const char*) &buffer, name->chars, ignore_previous)) {
			return should_update_locals;
		}
	}

	lit_runtime_error(vm, "Failed to require module '%s'", name->chars);
	return false;
}

void lit_open_core_library(LitState* state) {
	LIT_BEGIN_CLASS("Class")
		LIT_BIND_METHOD("toString", class_toString)
		LIT_BIND_METHOD("[]", class_subscript)

		LIT_BIND_STATIC_METHOD("toString", class_toString)
		LIT_BIND_STATIC_METHOD("iterator", class_iterator)
		LIT_BIND_STATIC_METHOD("iteratorValue", class_iteratorValue)

		LIT_BIND_GETTER("super", class_super)
		LIT_BIND_STATIC_GETTER("super", class_super)
		LIT_BIND_STATIC_GETTER("name", class_name)

		state->class_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Object")
		LIT_INHERIT_CLASS(state->class_class)

		LIT_BIND_METHOD("toString", object_toString)
		LIT_BIND_METHOD("[]", object_subscript)
		LIT_BIND_METHOD("iterator", object_iterator)
		LIT_BIND_METHOD("iteratorValue", object_iteratorValue)
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

		LIT_BIND_METHOD("+", string_plus)
		LIT_BIND_METHOD("toString", string_toString)
		LIT_BIND_METHOD("toNumber", string_toNumber)
		LIT_BIND_METHOD("toUpperCase", string_toUpperCase)
		LIT_BIND_METHOD("toLowerCase", string_toLowerCase)
		LIT_BIND_METHOD("contains", string_contains)
		LIT_BIND_METHOD("startsWith", string_startsWith)
		LIT_BIND_METHOD("endsWith", string_endsWith)
		LIT_BIND_METHOD("replace", string_replace)
		LIT_BIND_METHOD("substring", string_substring)
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

		LIT_BIND_METHOD("toString", function_toString)
		LIT_BIND_GETTER("name", function_name)

		state->function_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Fiber")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_CONSTRUCTOR(fiber_constructor)
		LIT_BIND_PRIMITIVE("run", fiber_run)
		LIT_BIND_PRIMITIVE("try", fiber_try)
		LIT_BIND_GETTER("done", fiber_done)
		LIT_BIND_GETTER("error", fiber_error)

		LIT_BIND_STATIC_PRIMITIVE("yield", fiber_yield)
		LIT_BIND_STATIC_PRIMITIVE("yeet", fiber_yeet)
		LIT_BIND_STATIC_PRIMITIVE("abort", fiber_abort)
		LIT_BIND_STATIC_GETTER("current", fiber_current)

		state->fiber_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Module")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_SET_STATIC_FIELD("loaded", OBJECT_VALUE(state->vm->modules))
		LIT_BIND_STATIC_GETTER("privates", module_privates)
		LIT_BIND_METHOD("toString", module_toString)
		LIT_BIND_GETTER("name", module_name)

		state->module_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Array")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_METHOD("[]", array_subscript)
		LIT_BIND_METHOD("add", array_add)
		LIT_BIND_METHOD("insert", array_insert)
		LIT_BIND_METHOD("slice", array_slice)
		LIT_BIND_METHOD("addAll", array_addAll)
		LIT_BIND_METHOD("remove", array_remove)
		LIT_BIND_METHOD("removeAt", array_removeAt)
		LIT_BIND_METHOD("indexOf", array_indexOf)
		LIT_BIND_METHOD("contains", array_contains)
		LIT_BIND_METHOD("clear", array_clear)
		LIT_BIND_METHOD("iterator", array_iterator)
		LIT_BIND_METHOD("iteratorValue", array_iteratorValue)
		LIT_BIND_METHOD("join", array_join)
		LIT_BIND_METHOD("clone", array_clone)
		LIT_BIND_METHOD("toString", array_toString)

		LIT_BIND_GETTER("length", array_length)

		state->array_class = klass;
	LIT_END_CLASS()
	LIT_BEGIN_CLASS("Map")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_METHOD("[]", map_subscript)
		LIT_BIND_METHOD("addAll", map_addAll)
		LIT_BIND_METHOD("clear", map_clear)
		LIT_BIND_METHOD("iterator", map_iterator)
		LIT_BIND_METHOD("iteratorValue", map_iteratorValue)
		LIT_BIND_METHOD("clone", map_clone)
		LIT_BIND_METHOD("toString", map_toString)

		LIT_BIND_GETTER("length", map_length)

		state->map_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Range")
		LIT_INHERIT_CLASS(state->object_class)

		LIT_BIND_METHOD("iterator", range_iterator)
		LIT_BIND_METHOD("iteratorValue", range_iteratorValue)
		LIT_BIND_METHOD("toString", range_toString)

		LIT_BIND_FIELD("from", range_from, range_set_from)
		LIT_BIND_FIELD("to", range_to, range_set_to)
		LIT_BIND_GETTER("length", range_length)

		state->range_class = klass;
	LIT_END_CLASS()

	lit_define_native(state, "time", time_native);
	lit_define_native(state, "systemTime", systemTime_native);
	lit_define_native(state, "print", print_native);

	lit_define_native_primitive(state, "require", require_primitive);
	lit_define_native_primitive(state, "eval", eval_primitive);

	lit_set_global(state, CONST_STRING(state, "globals"), OBJECT_VALUE(state->vm->globals));
}