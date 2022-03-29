# Core module
## Classes

* [Class](/docs/modules/core_module/class)
* [Object](/docs/modules/core_module/object)
* [Number](/docs/modules/core_module/number)
* [String](/docs/modules/core_module/string)
* [Bool](/docs/modules/core_module/bool)
* [Function](/docs/modules/core_module/function)
* [Fiber](/docs/moduless/core_module/fiber)
* [Module](/docs/modules/core_module/module)
* [Array](/docs/modules/core_module/array)
* [Map](/docs/modules/core_module/map)
* [Range](/docs/modules/core_module/range)

## Globals

lit_set_global(state, CONST_STRING(state, "globals"), OBJECT_VALUE(state->vm->globals));

## Functions

lit_define_native(state, "time", time_native);
lit_define_native(state, "systemTime", systemTime_native);
lit_define_native(state, "print", print_native);

lit_define_native_primitive(state, "require", require_primitive);
lit_define_native_primitive(state, "eval", eval_primitive);