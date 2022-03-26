# Module
LIT_INHERIT_CLASS(state->object_class)
LIT_BIND_CONSTRUCTOR(invalid_constructor)

LIT_SET_STATIC_FIELD("loaded", OBJECT_VALUE(state->vm->modules))
LIT_BIND_STATIC_GETTER("privates", module_privates)
LIT_BIND_STATIC_GETTER("current", module_current)

LIT_BIND_METHOD("toString", module_toString)
LIT_BIND_GETTER("name", module_name)
LIT_BIND_GETTER("privates", module_privates)