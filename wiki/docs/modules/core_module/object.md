# Object
LIT_INHERIT_CLASS(state->class_class)

LIT_BIND_METHOD("toString", object_toString)
LIT_BIND_METHOD("[]", object_subscript)
LIT_BIND_METHOD("iterator", object_iterator)
LIT_BIND_METHOD("iteratorValue", object_iteratorValue)
LIT_BIND_GETTER("class", object_class)