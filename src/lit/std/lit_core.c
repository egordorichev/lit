#include <lit/std/lit_core.h>
#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>

#include <time.h>

void lit_open_libraries(LitState* state) {

}

LIT_METHOD(object_toString) {
	return OBJECT_VALUE(lit_string_format(vm->state, "@ instance", OBJECT_VALUE(lit_get_class_for(vm->state, instance)->name)));
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

void lit_open_core_library(LitState* state) {
	LIT_BEGIN_CLASS("Class")
		state->class_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Object")
		LIT_BIND_METHOD("toString", object_toString)
		state->object_class = klass;
		state->object_class->super = (struct LitClass *) state->class_class;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Number")
		LIT_INHERIT_CLASS(state->object_class)
		state->number_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("String")
		LIT_INHERIT_CLASS(state->object_class)
		state->string_class = klass;
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Bool")
		LIT_INHERIT_CLASS(state->object_class)state->bool_class = klass;
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

	lit_define_native(state, "time", time_native);
	lit_define_native(state, "print", print_native);
}