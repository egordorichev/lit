# File
LIT_BIND_STATIC_METHOD("exists", file_exists)
LIT_BIND_STATIC_METHOD("getLastModified", file_getLastModified)

LIT_BIND_CONSTRUCTOR(file_constructor)
LIT_BIND_METHOD("close", file_close)
LIT_BIND_METHOD("write", file_write)

LIT_BIND_METHOD("writeByte", file_writeByte)
LIT_BIND_METHOD("writeShort", file_writeShort)
LIT_BIND_METHOD("writeNumber", file_writeNumber)
LIT_BIND_METHOD("writeBool", file_writeBool)
LIT_BIND_METHOD("writeString", file_writeString)

LIT_BIND_METHOD("readAll", file_readAll)
LIT_BIND_METHOD("readLine", file_readLine)

LIT_BIND_METHOD("readByte", file_readByte)
LIT_BIND_METHOD("readShort", file_readShort)
LIT_BIND_METHOD("readNumber", file_readNumber)
LIT_BIND_METHOD("readBool", file_readBool)
LIT_BIND_METHOD("readString", file_readString)

LIT_BIND_METHOD("getLastModified", file_getLastModified)

LIT_BIND_GETTER("exists", file_exists)