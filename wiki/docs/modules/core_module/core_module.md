# Core module
## Classes

* [Class](/docs/module/core_module/class)
* [Object](/docs/module/core_module/object)
* [Number](/docs/module/core_module/number)
* [String](/docs/module/core_module/string)
* [Bool](/docs/module/core_module/bool)
* [Function](/docs/module/core_module/function)
* [Fiber](/docs/module/core_module/fiber)
* [Module](/docs/module/core_module/module)
* [Array](/docs/module/core_module/array)
* [Map](/docs/module/core_module/map)
* [Range](/docs/module/core_module/range)

## Globals

lit_set_global(state, CONST_STRING(state, "globals"), OBJECT_VALUE(state->vm->globals));

## Functions

lit_define_native(state, "time", time_native);
lit_define_native(state, "systemTime", systemTime_native);
lit_define_native(state, "print", print_native);

lit_define_native_primitive(state, "require", require_primitive);
lit_define_native_primitive(state, "eval", eval_primitive);