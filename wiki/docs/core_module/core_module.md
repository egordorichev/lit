# Core module
## Classes

* [Class](/docs/core_module/class)
* [Object](/docs/core_module/object)
* [Number](/docs/core_module/number)
* [String](/docs/core_module/string)
* [Bool](/docs/core_module/bool)
* [Function](/docs/core_module/function)
* [Fiber](/docs/core_module/fiber)
* [Module](/docs/core_module/module)
* [Array](/docs/core_module/array)
* [Map](/docs/core_module/map)
* [Range](/docs/core_module/range)

## Globals

lit_set_global(state, CONST_STRING(state, "globals"), OBJECT_VALUE(state->vm->globals));

## Functions

lit_define_native(state, "time", time_native);
lit_define_native(state, "systemTime", systemTime_native);
lit_define_native(state, "print", print_native);

lit_define_native_primitive(state, "require", require_primitive);
lit_define_native_primitive(state, "eval", eval_primitive);