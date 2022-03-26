# Class

LIT_BIND_METHOD("toString", class_toString)
LIT_BIND_METHOD("[]", class_subscript)

LIT_BIND_STATIC_METHOD("toString", class_toString)
LIT_BIND_STATIC_METHOD("iterator", class_iterator)
LIT_BIND_STATIC_METHOD("iteratorValue", class_iteratorValue)

LIT_BIND_GETTER("super", class_super)
LIT_BIND_STATIC_GETTER("super", class_super)
LIT_BIND_STATIC_GETTER("name", class_name)