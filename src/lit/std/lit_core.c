#include "lit/std/lit_core.h"
#include "lit/std/lit_math.h"
#include "lit/std/lit_file.h"
#include "lit/std/lit_gc.h"
#include "lit/std/lit_json.h"
#include "lit/std/lit_time.h"
#include "lit/std/lit_network.h"
#include "lit/api/lit_api.h"
#include "lit/vm/lit_vm.h"
#include "lit/vm/lit_object.h"
#include "lit/util/lit_fs.h"
#include "lit/util/lit_utf.h"

#include <time.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

void lit_open_libraries(LitState* state) {
	lit_open_math_library(state);
	lit_open_file_library(state);
	lit_open_gc_library(state);
	lit_open_json_library(state);
	lit_open_event_library(state);
}

LIT_METHOD(invalid_constructor) {
	lit_runtime_error_exiting(vm, "Can't create an instance of built-in type", AS_INSTANCE(instance)->klass->name);
	return NULL_VALUE;
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

	if (number >= (int) table->capacity) {
		return -1;
	}

	number++;

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
	int index = args[0] == NULL_VALUE ? -1 : AS_NUMBER(args[0]);
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
			lit_runtime_error_exiting(vm, "Class index must be a string");
		}

		lit_table_set(vm->state, &klass->static_fields, AS_STRING(args[0]), args[1]);
		return args[1];
	}

	if (!IS_STRING(args[0])) {
		lit_runtime_error_exiting(vm, "Class index must be a string");
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
	LitState* state = vm->state;
	LitClass* klass = lit_get_class_for(vm->state, instance);

	if (klass != state->object_class) {
		return lit_string_format(state, "@ instance", OBJECT_VALUE(klass->name));
	}

	LitTable* values = &AS_INSTANCE(instance)->fields;

	if (values->count == 0) {
		return OBJECT_CONST_STRING(state, "{}");
	}

	uint value_amount = values->count;

	LitString* values_converted[value_amount];
	LitString* keys[value_amount];

	uint indentation = LIT_GET_NUMBER(0, 0) + 1;
	uint string_length = indentation + 2;

	uint i = 0;
	uint index = 0;

	do {
		LitTableEntry* entry = &values->entries[index++];

		if (entry->key != NULL) {
			LitString* value = lit_to_string(state, entry->value, indentation);

			lit_push_root(state, (LitObject*) value);

			if (IS_STRING(entry->value)) {
				value = AS_STRING(lit_string_format(state, "\"@\"", OBJECT_VALUE(value)));
				lit_pop_root(state);
				lit_push_root(state, (LitObject*) value);
			}

			values_converted[i] = value;
			keys[i] = entry->key;
			string_length += entry->key->length + 2 + value->length + (i == value_amount - 1 ? 1 : 2) + indentation;

			i++;
		}
	} while (i < value_amount);

	char buffer[string_length + 1];
	memcpy(buffer, "{\n", 2);

	uint buffer_index = 2;

	for (i = 0; i < value_amount; i++) {
		LitString *key = keys[i];
		LitString *value = values_converted[i];

		for (uint j = 0; j < indentation; j++) {
			buffer[buffer_index++] = '\t';
		}

		memcpy(&buffer[buffer_index], key->chars, key->length);
		buffer_index += key->length;

		memcpy(&buffer[buffer_index], ": ", 2);
		buffer_index += 2;

		memcpy(&buffer[buffer_index], value->chars, value->length);
		buffer_index += value->length;

		if (i == value_amount - 1) {
			buffer[buffer_index++] = '\n';

			for (uint j = 0; j < indentation - 1; j++) {
				buffer[buffer_index++] = '\t';
			}

			buffer[buffer_index++] = '}';
		} else {
			memcpy(&buffer[buffer_index], ",\n", 2);
		}

		buffer_index += 2;

		lit_pop_root(state);
	}

	buffer[string_length] = '\0';
	return OBJECT_VALUE(lit_copy_string(vm->state, buffer, string_length));
}

LIT_METHOD(object_subscript) {
	if (!IS_INSTANCE(instance)) {
		LitObjectType type = OBJECT_TYPE(instance);
		lit_runtime_error_exiting(vm, "Can't modify built-in types");
	}

	LitInstance* inst = AS_INSTANCE(instance);

	if (!IS_STRING(args[0])) {
		lit_runtime_error_exiting(vm, "Object index must be a string");
	}

	if (arg_count == 2) {
		lit_table_set(vm->state, &inst->fields, AS_STRING(args[0]), args[1]);
		return args[1];
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

	int index = args[0] == NULL_VALUE ? -1 : AS_NUMBER(args[0]);
	int value = table_iterator(&self->fields, index);

	return value == -1 ? NULL_VALUE : NUMBER_VALUE(value);
}

LIT_METHOD(object_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	LitInstance* self = AS_INSTANCE(instance);

	return table_iterator_key(&self->fields, index);
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
		string_value = lit_to_string(vm->state, value, 0);
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
		lit_runtime_error_exiting(vm, "Expected 2 string arguments");
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
	int length = lit_ustring_length(string);

	if (from < 0) {
		from = length + from;
	}

	if (to < 0) {
		to = length + to;
	}

	from = fmax(from, 0);
	to = fmin(to, length - 1);

	if (from > to) {
		lit_runtime_error_exiting(vm, "String splice from bound is larger that to bound");
	}

	from = lit_uchar_offset(string->chars, from);
	to = lit_uchar_offset(string->chars, to);

	return OBJECT_VALUE(lit_ustring_from_range(vm->state, string, from, to - from + 1));
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
		lit_runtime_error_exiting(vm, "Can't modify strings with the subscript op");
	}

	if (index < 0) {
		index = lit_ustring_length(string) + index;

		if (index < 0) {
			return NULL_VALUE;
		}
	}

	LitString* c = lit_ustring_code_point_at(vm->state, string, lit_uchar_offset(string->chars, index));

	return c == NULL ? NULL_VALUE : OBJECT_VALUE(c);
}

LIT_METHOD(string_less) {
	return BOOL_VALUE(strcmp(AS_STRING(instance)->chars, LIT_CHECK_STRING(0)) < 0);
}

LIT_METHOD(string_greater) {
	return BOOL_VALUE(strcmp(AS_STRING(instance)->chars, LIT_CHECK_STRING(0)) > 0);
}

LIT_METHOD(string_length) {
	return NUMBER_VALUE(lit_ustring_length(AS_STRING(instance)));
}

LIT_METHOD(string_iterator) {
	LitString* string = AS_STRING(instance);

	if (IS_NULL(args[0])) {
		if (string->length == 0) {
			return NULL_VALUE;
		}

		return NUMBER_VALUE(0);
	}

	int index = LIT_CHECK_NUMBER(0);

	if (index < 0) {
		return NULL_VALUE;
	}

	do {
		index++;

		if (index >= (int) string->length) {
			return NULL_VALUE;
		}
	} while ((string->chars[index] & 0xc0) == 0x80);

	return NUMBER_VALUE(index);
}

LIT_METHOD(string_iteratorValue) {
	LitString* string = AS_STRING(instance);
	uint32_t index = LIT_CHECK_NUMBER(0);

	if (index == UINT32_MAX) {
		return false;
	}

	return OBJECT_VALUE(lit_ustring_code_point_at(vm->state, string, index));
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
	LitValue arg = args[0];

	if (arg_count < 1 || (!IS_FUNCTION(arg) && !IS_CLOSURE(arg))) {
		lit_runtime_error_exiting(vm, "Fiber constructor expects a function as its argument");
	}

	LitModule* module = vm->fiber->module;

	LitFiber* fiber;

	if (IS_FUNCTION(arg)) {
		fiber = lit_create_fiber(vm->state, module, AS_FUNCTION(arg));
	} else {
		fiber = lit_create_fiber_with_closure(vm->state, module, AS_CLOSURE(arg));
	}

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
		lit_runtime_error_exiting(vm, "Fiber already finished executing");
	}

	fiber->parent = vm->fiber;
	fiber->catcher = catcher;

	vm->fiber = fiber;

	LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];

	if (frame->ip == frame->function->chunk.code) {
		fiber->arg_count = arg_count;
		LitFunction* function = frame->function;

		LitValue* start = fiber->frame_count > 1 ? fiber->frames[fiber->frame_count - 2].slots + fiber->frames[fiber->frame_count - 2].function->max_registers : fiber->registers;
		lit_ensure_fiber_registers(vm->state, fiber, start - fiber->registers + function->max_registers);

		frame->slots = fiber->frame_count > 1 ? fiber->frames[fiber->frame_count - 2].slots + fiber->frames[fiber->frame_count - 2].function->max_registers : fiber->registers;

		for (int i = arg_count + 1; i < function->max_registers; i++) {
			frame->slots[i] = NULL_VALUE;
		}

		frame->slots[0] = OBJECT_VALUE(function);

		for (uint8_t i = 0; i < arg_count; i++) {
			frame->slots[i + 1] = args[i];
		}

		bool vararg = frame->function->vararg;
		uint function_arg_count = function->arg_count;

		fiber->arg_count = function_arg_count;

		if (vararg) {
			if (function_arg_count == arg_count && IS_VARARG_ARRAY(*(frame->slots + function_arg_count))) {
				// No need to repack the arguments
			} else {
				LitArray *array = &lit_create_vararg_array(vm->state)->array;
				lit_push_root(vm->state, (LitObject*) array);
				*(frame->slots + function_arg_count) = OBJECT_VALUE(array);

				int vararg_count = arg_count - function_arg_count + 1;

				if (vararg_count > 0) {
					lit_values_ensure_size(vm->state, &array->values, vararg_count);

					for (int i = 0; i < vararg_count; i++) {
						array->values.values[i] = args[i + function_arg_count - 1];
					}
				}

				lit_pop_root(vm->state);
			}
		}
	}

#ifdef LIT_TRACE_EXECUTION
	printf("fiber start:\n");
#endif
}

LIT_PRIMITIVE(fiber_run) {
	vm->fiber->return_address = args - 1;
	run_fiber(vm, AS_FIBER(instance), args, arg_count, false);
	return true;
}

LIT_PRIMITIVE(fiber_try) {
	vm->fiber->return_address = args - 1;
	run_fiber(vm, AS_FIBER(instance), args, arg_count, true);
	return true;
}

LIT_PRIMITIVE(fiber_yield) {
	if (vm->fiber->parent == NULL) {
		lit_handle_runtime_error(vm, arg_count == 0 ? CONST_STRING(vm->state, "Fiber was yielded") : lit_to_string(vm->state, args[0], 0));
		return true;
	}

	vm->fiber = vm->fiber->parent;
	*vm->fiber->return_address = arg_count == 0 ? NULL_VALUE : OBJECT_VALUE(lit_to_string(vm->state, args[0], 0));

	return true;
}

LIT_PRIMITIVE(fiber_yeet) {
	if (vm->fiber->parent == NULL) {
		lit_handle_runtime_error(vm, arg_count == 0 ? CONST_STRING(vm->state, "Fiber was yeeted") : lit_to_string(vm->state, args[0], 0));
		return true;
	}

	vm->fiber = vm->fiber->parent;
	*vm->fiber->return_address = arg_count == 0 ? NULL_VALUE : OBJECT_VALUE(lit_to_string(vm->state, args[0], 0));

	return true;
}

LIT_PRIMITIVE(fiber_abort) {
	LitString* value = arg_count == 0 ? CONST_STRING(vm->state, "Fiber was aborted") : lit_to_string(vm->state, args[0], 0);

	lit_handle_runtime_error(vm, value);
	*vm->fiber->return_address = OBJECT_VALUE(value);

	return true;
}

/*
 * Module
 */

LitValue access_private(LitVm* vm, struct sLitMap* map, LitString* name, LitValue* val) {
	LitValue value;
	LitString* id = CONST_STRING(vm->state, "_module");

	if (!lit_table_get(&map->values, id, &value) || !IS_MODULE(value)) {
		return NULL_VALUE;
	}

	LitModule* module = AS_MODULE(value);

	if (id == name) {
		return OBJECT_VALUE(module);
	}

	if (lit_table_get(&module->private_names->values, name, &value)) {
		int index = (int) AS_NUMBER(value);

		if (index > -1 && index < (int) module->private_count) {
			if (val != NULL) {
				module->privates[index] = *val;
				return *val;
			}

			return module->privates[index];
		}
	}

	return NULL_VALUE;
}

LIT_METHOD(module_privates) {
	LitModule* module = IS_MODULE(instance) ? AS_MODULE(instance) : vm->fiber->module;
	LitMap* map = module->private_names;

	if (map->index_fn == NULL) {
		map->index_fn = access_private;
		lit_table_set(vm->state, &map->values, CONST_STRING(vm->state, "_module"), OBJECT_VALUE(module));
	}

	return OBJECT_VALUE(map);
}

LIT_METHOD(module_current) {
	return OBJECT_VALUE(vm->fiber->module);
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

LIT_METHOD(array_constructor) {
	return OBJECT_VALUE(lit_create_array(vm->state));
}

static LitValue array_splice(LitVm* vm, LitArray* array, int from, int to) {
	uint length = array->values.count;

	if (from < 0) {
		from = (int) length + from;
	}

	if (to < 0) {
		to = (int) length + to;
	}

	if (from > to) {
		lit_runtime_error_exiting(vm, "String splice from bound is larger that to bound");
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
			lit_runtime_error_exiting(vm, "Array index must be a number");
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

		lit_runtime_error_exiting(vm, "Array index must be a number");
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
		lit_runtime_error_exiting(vm, "Expected array as the argument");
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

LIT_METHOD(array_forEach) {
	LIT_ENSURE_ARGS(1)
	LitValue callback = args[0];

	if (!IS_CALLABLE_FUNCTION(callback)) {
		lit_runtime_error_exiting(vm, "Expected a function as the callback");
	}

	LitValues* values = &AS_ARRAY(instance)->values;

	for (uint i = 0; i < values->count; i++) {
		lit_call(vm->state, callback, &values->values[i], 1);
	}

	return NULL_VALUE;
}

LIT_METHOD(array_join) {
	LitValues* values = &AS_ARRAY(instance)->values;
	LitString* strings[values->count];

	uint length = 0;

	for (uint i = 0; i < values->count; i++) {
		LitString* string = lit_to_string(vm->state, values->values[i], 0);

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

static inline bool compare(LitState* state, LitValue a, LitValue b) {
	if (IS_NUMBER(a) && IS_NUMBER(b)) {
		return AS_NUMBER(a) < AS_NUMBER(b);
	}

	return !lit_is_falsey(lit_find_and_call_method(state, a, CONST_STRING(state, "<"), (LitValue[1]) { b }, 1).result);
}

static void basic_quick_sort(LitState* state, LitValue *l, int length) {
	if (length < 2) {
		return;
	}

	int pivot_index = length / 2;
	int i;
	int j;

	LitValue pivot = l[pivot_index];

	for (i = 0, j = length - 1; ; i++, j--) {
		while (i < pivot_index && compare(state, l[i], pivot)) {
			i++;
		}

		while (j > pivot_index && compare(state, pivot, l[j])) {
			j--;
		}

		if (i >= j) {
			break;
		}

		LitValue tmp = l[i];
		l[i] = l[j];
		l[j] = tmp;
	}

	basic_quick_sort(state, l, i);
	basic_quick_sort(state, l + i, length - i);
}

static void custom_quick_sort(LitVm* vm, LitValue *l, int length, LitValue callee) {
	if (length < 2) {
		return;
	}

	LitState* state = vm->state;

	int pivot_index = length / 2;
	int i;
	int j;
	LitValue pivot = l[pivot_index];

	#define COMPARE(a, b) ({ LitInterpretResult r = lit_call(state, callee, (LitValue[2]) { a, b }, 2); \
    if (r.type != INTERPRET_OK) return; \
		!lit_is_falsey(r.result); })

	for (i = 0, j = length - 1; ; i++, j--) {
		while (i < pivot_index && COMPARE(l[i], pivot)) {
			i++;
		}

		while (j > pivot_index && COMPARE(pivot, l[j])) {
			j--;
		}

		if (i >= j) {
			break;
		}

		LitValue tmp = l[i];
		l[i] = l[j];
		l[j] = tmp;
	}

	#undef COMPARE

	custom_quick_sort(vm, l, i, callee);
	custom_quick_sort(vm, l + i, length - i, callee);
}

LIT_METHOD(array_sort) {
	LitValues* values = &AS_ARRAY(instance)->values;

	if (arg_count == 1 && IS_CALLABLE_FUNCTION(args[0])) {
		custom_quick_sort(vm, values->values, values->count, args[0]);
	} else {
		basic_quick_sort(vm->state, values->values, values->count);
	}

	return instance;
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
	uint indentation = LIT_SINGLE_LINE_MAPS_ENABLED ? 0 : LIT_GET_NUMBER(0, 0) + 1;

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
		LitValue field = values->values[(has_more && i == value_amount - 1) ? values->count - 1 : i];
		LitString* value = lit_to_string(state, field, indentation);

		lit_push_root(state, (LitObject*) value);

		if (IS_STRING(field)) {
			value = AS_STRING(lit_string_format(state, "\"@\"", OBJECT_VALUE(value)));
		}

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

		lit_pop_root(state);
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

LIT_METHOD(map_constructor) {
	return OBJECT_VALUE(lit_create_map(vm->state));
}

LIT_METHOD(map_subscript) {
	if (!IS_STRING(args[0])) {
		lit_runtime_error_exiting(vm, "Map index must be a string");
	}

	LitMap* map = AS_MAP(instance);
	LitString* index = AS_STRING(args[0]);

	if (arg_count == 2) {
		LitValue val = args[1];

		if (map->index_fn != NULL) {
			return map->index_fn(vm, map, index, &val);
		}

		lit_map_set(vm->state, map, index, val);
		return val;
	}

	LitValue value;

	if (map->index_fn != NULL) {
		return map->index_fn(vm, map, index, NULL);
	}

	if (!lit_table_get(&map->values, index, &value)) {
		return NULL_VALUE;
	}

	return value;
}

LIT_METHOD(map_addAll) {
	LIT_ENSURE_ARGS(1)

	if (!IS_MAP(args[0])) {
		lit_runtime_error_exiting(vm, "Expected map as the argument");
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
	int index = args[0] == NULL_VALUE ? -1 : AS_NUMBER(args[0]);

	int value = table_iterator(&AS_MAP(instance)->values, index);
	return value == -1 ? NULL_VALUE : NUMBER_VALUE(value);
}

LIT_METHOD(map_iteratorValue) {
	uint index = LIT_CHECK_NUMBER(0);
	return table_iterator_key(&AS_MAP(instance)->values, index);
}

LIT_METHOD(map_forEach) {
	LIT_ENSURE_ARGS(1)
	LitValue callback = args[0];

	if (!IS_CALLABLE_FUNCTION(callback)) {
		lit_runtime_error_exiting(vm, "Expected a function as the callback");
	}

	LitTable* values = &AS_MAP(instance)->values;

	for (int i = 0; i < values->capacity; i++) {
		LitTableEntry* entry = &values->entries[i];

		if (entry->key != NULL) {
			lit_call(vm->state, callback, (LitValue[2]) { OBJECT_VALUE(entry->key), entry->value }, 2);
		}
	}

	return NULL_VALUE;
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

	uint indentation = LIT_SINGLE_LINE_MAPS_ENABLED ? 0 : LIT_GET_NUMBER(0, 0) + 1;
	uint string_length = (LIT_SINGLE_LINE_MAPS_ENABLED ? 3 : 2) + indentation;

	if (has_more) {
		string_length += LIT_SINGLE_LINE_MAPS_ENABLED ? 5 : 6;
	}

	uint i = 0;
	uint index = 0;

	do {
		LitTableEntry* entry = &values->entries[index++];

		if (entry->key != NULL) {
			// Special hidden key
			LitValue field = has_wrapper ? map->index_fn(vm, map, entry->key, NULL) : entry->value;
			// This check is required to prevent infinite loops when playing with Module.privates and such
			LitString* value = (IS_MAP(field) && AS_MAP(field)->index_fn != NULL) ? CONST_STRING(state, "map") : lit_to_string(state, field, indentation);

			lit_push_root(state, (LitObject*) value);

			if (IS_STRING(field)) {
				value = AS_STRING(lit_string_format(state, "\"@\"", OBJECT_VALUE(value)));
			}

			values_converted[i] = value;
			keys[i] = entry->key;
			string_length += entry->key->length + 2 + value->length +
				(i == value_amount - 1 ? 1 : 2) + indentation;

			i++;
		}
	} while (i < value_amount);

	char buffer[string_length + 1];

	#ifdef LIT_SINGLE_LINE_MAPS
	memcpy(buffer, "{ ", 2);
	#else
	memcpy(buffer, "{\n", 2);
	#endif

	uint buffer_index = 2;

	for (i = 0; i < value_amount; i++) {
		LitString *key = keys[i];
		LitString *value = values_converted[i];

		for (uint j = 0; j < indentation; j++) {
			buffer[buffer_index++] = '\t';
		}

		memcpy(&buffer[buffer_index], key->chars, key->length);
		buffer_index += key->length;

		memcpy(&buffer[buffer_index], ": ", 2);
		buffer_index += 2;

		memcpy(&buffer[buffer_index], value->chars, value->length);
		buffer_index += value->length;

		if (has_more && i == value_amount - 1) {
			#ifdef LIT_SINGLE_LINE_MAPS
			memcpy(&buffer[buffer_index], ", ... }", 7);
			#else
			memcpy(&buffer[buffer_index], ",\n\t...\n", 7);
			buffer_index += 7;

			for (uint j = 0; j < indentation - 1; j++) {
				buffer[buffer_index++] = '\t';
			}

			buffer[buffer_index++] = '}';
			#endif
		} else {
			#ifdef LIT_SINGLE_LINE_MAPS
			memcpy(&buffer[buffer_index], (i == value_amount - 1) ? " }" : ", ", 2);
			#else
			if (i == value_amount - 1) {
				buffer[buffer_index++] = '\n';

				for (uint j = 0; j < indentation - 1; j++) {
					buffer[buffer_index++] = '\t';
				}

				buffer[buffer_index++] = '}';
			} else {
				memcpy(&buffer[buffer_index], ",\n", 2);
			}
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

		if (range->to > range->from ? number >= range->to : number <= range->to) {
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
		lit_printf(vm->state, "%s\n", lit_to_string(vm->state, args[i], 0)->chars);
	}

	return NULL_VALUE;
}

LIT_NATIVE(openLibrary) {
	const char* name = LIT_CHECK_STRING(0);

	if (strcmp(name, "network") == 0) {
		lit_open_network_library(vm->state);
	} else {
		lit_runtime_error_exiting(vm, "Unknown built-in library %s", name);
	}

	return NULL_VALUE;
}

static bool interpret(LitVm* vm, LitModule* module) {
	LitFunction* function = module->main_function;
	LitFiber* fiber = lit_create_fiber(vm->state, module, function);

	fiber->parent = vm->fiber;
	vm->fiber = fiber;

	return true;
}

static bool compile_and_interpret(LitVm* vm, LitString* module_name, char* source) {
	LitModule *module = lit_compile_module(vm->state, module_name, source);

	if (module == NULL) {
		return false;
	}

	module->ran = true;
	return interpret(vm, module);
}

LIT_NATIVE_PRIMITIVE(eval) {
	char* code = (char*) LIT_CHECK_STRING(0);
	return compile_and_interpret(vm, vm->fiber->module->name, code);
}

static bool file_exists(const char* filename) {
	struct stat buffer;
	return stat(filename, &buffer) == 0;
}

static bool should_update_locals;
static bool attempt_to_require_combined(LitVm* vm, LitValue* args, uint arg_count, const char* a, const char* b, bool ignore_previous);
typedef void (*library_loader)(LitState*);

static bool attempt_to_require(LitVm* vm, LitValue* args, uint arg_count, const char* path, bool ignore_previous, bool folders) {
	size_t length = strlen(path);
	should_update_locals = false;

	if (path[length - 2] == '.' && path[length - 1] == '*') {
		if (folders) {
			lit_runtime_error_exiting(vm, "Can't recursively require folders (beg @egordorichev for mercy)");
		}

		char dir_path[length - 1];
		dir_path[length - 2] = '\0';
		memcpy((void*) dir_path, path, length - 2);

		return attempt_to_require(vm, args, arg_count, dir_path, ignore_previous, true);
	}

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

	// You can require dirs if they have init.lit in them
	module_name[length] = '\0';

	if (lit_dir_exists(module_name)) {
		if (folders) {
			struct dirent* ep;
			DIR* dir = opendir(module_name);

			if (dir == NULL) {
				lit_runtime_error_exiting(vm, "Failed to open folder '%s'", module_name);
			}

			bool found = false;

      // TODO: was rewritten to work on windows, needs checks!!!
      while ((ep = readdir(dir))) {
        const char* name = ep->d_name;
        int name_length = (int) strlen(name);

        if (name_length <= 4 || !(strcmp(name + name_length - 4, ".lit") == 0 || strcmp(name + name_length - 4, ".lbc") == 0 || strcmp(name + name_length - 3, ".so") == 0 || strcmp(name + name_length - 4, ".dll") == 0)) {
          continue;
        }

        char dir_path[length + name_length - 2];
        dir_path[length + name_length - 3] = '\0';

        memcpy((void*) dir_path, path, length);
        memcpy((void*) dir_path + length + 1, name, name_length - 4);
        dir_path[length] = '/';

        struct stat st;
        stat(dir_path, &st);

        if (S_ISREG(st.st_mode)) {
          dir_path[length] = '.';

          if (!attempt_to_require(vm, args + arg_count, 0, dir_path, false, false)) {
            lit_runtime_error_exiting(vm, "Failed to require module '%s'", name);
          } else {
            found = true;
          }
				}
			}

			if (!found) {
				lit_runtime_error_exiting(vm, "Folder '%s' contains no modules that can be required", module_name);
			}

			return found;
		} else {
			char dir_name[length + 6];
			dir_name[length + 5] = '\0';

			memcpy((void *) dir_name, module_name, length);
			memcpy((void *) dir_name + length, ".init", 5);

			if (attempt_to_require(vm, args, arg_count, dir_name, ignore_previous, false)) {
				return true;
			}
		}
	} else if (folders) {
		return false;
	}

	module_name[length] = '.';
	LitString *name = lit_copy_string(vm->state, module_name_dotted, length);

	if (!ignore_previous) {
		LitValue existing_module;

		if (lit_table_get(&vm->modules->values, name, &existing_module)) {
			LitModule* loaded_module = AS_MODULE(existing_module);

			if (loaded_module->ran) {
				args[-1] = AS_MODULE(existing_module)->return_value;
				should_update_locals = true;
			} else {
				if (interpret(vm, loaded_module)) {
					should_update_locals = true;
				}
			}

			return true;
		}
	}

	bool library = false;

	if (!file_exists(module_name)) {
		// .lit -> .lbc
		memcpy((void*) module_name + length + 2, "bc", 2);

		if (!file_exists(module_name)) {
			// .lbc -> .dll
			memcpy((void*) module_name + length + 1, "dll", 3);
			library = true;

			if (!file_exists(module_name)) {
				// .dll -> .so
				memcpy((void*) module_name + length + 1, "so", 3);

				if (!file_exists(module_name)) {
					return false;
				}
			}
		}
	}

	if (library) {
		void* handle = NULL;
		library_loader function = NULL;

		char full_path_buffer[1024];
		char* full_path;

        #ifdef _WIN32
            char** lpp_part;
            GetFullPathName(module_name, 1024, full_path_buffer, lpp_part);

            handle = LoadLibrary(full_path);

            if (handle == NULL) {
                lit_runtime_error_exiting(vm, "Unable to require '%s' library", module_name);
            }

            function = (library_loader) GetProcAddress(handle, "open_lit_library");
        #else
		    full_path = realpath(module_name, full_path_buffer);

            handle = dlopen(full_path, RTLD_NOW | RTLD_GLOBAL);

            if (handle == NULL) {
                lit_runtime_error_exiting(vm, "Unable to require '%s' library %s", module_name, dlerror());
            }

            function = dlsym(handle, "open_lit_library");
        #endif

        if (function == NULL) {
            lit_runtime_error_exiting(vm, "Unable to require '%s' library: it's missing 'open_lit_library()'", module_name);
            return -1;
        }

		function(vm->state);

		should_update_locals = true;
		return true;
	}

	char* source = lit_read_file(module_name);

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

	return attempt_to_require(vm, args, arg_count, (const char*) &path, ignore_previous, false);
}


typedef struct LitBuiltinModule {
	const char* name;
	const char* source;
} LitBuiltinModule;

extern const char lit_promise[];
extern const char lit_http[];

LitBuiltinModule modules[] = {
	{ "promise", (const char*) lit_promise },
	{ "http", (const char*) lit_http },
	{ NULL, NULL }
};

LIT_NATIVE_PRIMITIVE(require) {
	vm->fiber->return_address = args - 1;

	LitString* name = LIT_CHECK_OBJECT_STRING(0);

	for (uint i = 0;; i++) {
		LitBuiltinModule* module = &modules[i];

		if (module->name == NULL) {
			break;
		}

		if (strcmp(name->chars, module->name) == 0) {
			LitValue existing_module;

			if (lit_table_get(&vm->modules->values, name, &existing_module)) {
				LitModule* loaded_module = AS_MODULE(existing_module);

				if (loaded_module->ran) {
					args[-1] = AS_MODULE(existing_module)->return_value;
					should_update_locals = true;
				} else {
					if (interpret(vm, loaded_module)) {
						should_update_locals = true;
					}
				}

				return false;
			}

			if (compile_and_interpret(vm, name, (char*) module->source)) {
				should_update_locals = true;
			}

			return false;
		}
	}

	bool ignore_previous = arg_count > 1 && IS_BOOL(args[1]) && AS_BOOL(args[1]);

	// First check, if a file with this name exists in the local path
	if (attempt_to_require(vm, args, arg_count, name->chars, ignore_previous, false)) {
		return !should_update_locals;
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
			return !should_update_locals;
		}
	}

	lit_runtime_error_exiting(vm, "Failed to require module '%s'", name->chars);
	return true;
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
	LIT_END_CLASS_IGNORING()

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
		LIT_BIND_CONSTRUCTOR(invalid_constructor)

		LIT_BIND_METHOD("toString", number_toString)
		state->number_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("String")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_CONSTRUCTOR(invalid_constructor)

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
		LIT_BIND_METHOD("iterator", string_iterator)
		LIT_BIND_METHOD("iteratorValue", string_iteratorValue)
		LIT_BIND_METHOD("[]", string_subscript)
		LIT_BIND_METHOD("<", string_less)
		LIT_BIND_METHOD(">", string_greater)

		LIT_BIND_GETTER("length", string_length)

		state->string_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Bool")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_CONSTRUCTOR(invalid_constructor)

		LIT_BIND_METHOD("toString", bool_toString)
		state->bool_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Function")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_CONSTRUCTOR(invalid_constructor)

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
		LIT_BIND_CONSTRUCTOR(invalid_constructor)

		LIT_SET_STATIC_FIELD("loaded", OBJECT_VALUE(state->vm->modules))
		LIT_BIND_STATIC_GETTER("privates", module_privates)
		LIT_BIND_STATIC_GETTER("current", module_current)

		LIT_BIND_METHOD("toString", module_toString)
		LIT_BIND_GETTER("name", module_name)
		LIT_BIND_GETTER("privates", module_privates)

		state->module_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Array")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_CONSTRUCTOR(array_constructor)

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
		LIT_BIND_METHOD("forEach", array_forEach)
		LIT_BIND_METHOD("join", array_join)
		LIT_BIND_METHOD("sort", array_sort)
		LIT_BIND_METHOD("clone", array_clone)
		LIT_BIND_METHOD("toString", array_toString)

		LIT_BIND_GETTER("length", array_length)

		state->array_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Map")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_CONSTRUCTOR(map_constructor)

		LIT_BIND_METHOD("[]", map_subscript)
		LIT_BIND_METHOD("addAll", map_addAll)
		LIT_BIND_METHOD("clear", map_clear)
		LIT_BIND_METHOD("iterator", map_iterator)
		LIT_BIND_METHOD("iteratorValue", map_iteratorValue)
		LIT_BIND_METHOD("forEach", map_forEach)
		LIT_BIND_METHOD("clone", map_clone)
		LIT_BIND_METHOD("toString", map_toString)

		LIT_BIND_GETTER("length", map_length)

		state->map_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Range")
		LIT_INHERIT_CLASS(state->object_class)
		LIT_BIND_CONSTRUCTOR(invalid_constructor)

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
	lit_define_native(state, "openLibrary", openLibrary_native);

	lit_define_native_primitive(state, "require", require_primitive);
	lit_define_native_primitive(state, "eval", eval_primitive);

	lit_set_global(state, CONST_STRING(state, "globals"), OBJECT_VALUE(state->vm->globals));
}