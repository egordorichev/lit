# Map
LIT_INHERIT_CLASS(state->object_class)
LIT_BIND_CONSTRUCTOR(map_constructor)

LIT_BIND_METHOD("[]", map_subscript)
LIT_BIND_METHOD("addAll", map_addAll)
LIT_BIND_METHOD("clear", map_clear)
LIT_BIND_METHOD("iterator", map_iterator)
LIT_BIND_METHOD("iteratorValue", map_iteratorValue)
LIT_BIND_METHOD("clone", map_clone)
LIT_BIND_METHOD("toString", map_toString)

LIT_BIND_GETTER("length", map_length)