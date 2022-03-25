OPCODE(MOVE, "MOVE", LIT_INSTRUCTION_ABC) // R(A) := RC(B)
OPCODE(LOAD_NULL, "LOAD_NULL", LIT_INSTRUCTION_ABC) // R(A) := null
OPCODE(LOAD_BOOL, "LOAD_BOOL", LIT_INSTRUCTION_ABC) // R(A) := (bool) B
OPCODE(CLOSURE, "CLOSURE", LIT_INSTRUCTION_ABX) // R(A) := PrC[Bx]
OPCODE(ARRAY, "ARRAY", LIT_INSTRUCTION_ABX) // R(A) := new Array(Bx)

OPCODE(RETURN, "RETURN", LIT_INSTRUCTION_ABC) // return R(A)

OPCODE(ADD, "ADD", LIT_INSTRUCTION_ABC) // R(A) := RC(B) + RC(C)
OPCODE(SUBTRACT, "SUBTRACT", LIT_INSTRUCTION_ABC) // R(A) := RC(B) - RC(C)
OPCODE(MULTIPLY, "MULTIPLY", LIT_INSTRUCTION_ABC) // R(A) := RC(B) * RC(C)
OPCODE(DIVIDE, "DIVIDE", LIT_INSTRUCTION_ABC) // R(A) := RC(B) / RC(C)

OPCODE(JUMP, "JUMP", LIT_INSTRUCTION_ASBX) // PC += sBx
OPCODE(TRUE_JUMP, "TRUE_JUMP", LIT_INSTRUCTION_ABX) // if (R(A)) PC += Bx
OPCODE(FALSE_JUMP, "FALSE_JUMP", LIT_INSTRUCTION_ABX) // if (not R(A)) PC += Bx
OPCODE(NON_NULL_JUMP, "NON_NULL_JUMP", LIT_INSTRUCTION_ABX) // if (R(A) != null) PC += Bx

OPCODE(EQUAL, "EQUAL", LIT_INSTRUCTION_ABC) // R(A) := RC(B) == RC(C)
OPCODE(LESS, "LESS", LIT_INSTRUCTION_ABC) // R(A) := RC(B) < RC(C)
OPCODE(LESS_EQUAL, "LESS_EQUAL", LIT_INSTRUCTION_ABC) // R(A) := RC(B) <= RC(C)

OPCODE(NEGATE, "NEGATE", LIT_INSTRUCTION_ABC) // R(A) := -RC(B)
OPCODE(NOT, "NOT", LIT_INSTRUCTION_ABC) // R(A) := !RC(B)

OPCODE(SET_GLOBAL, "SET_GLOBAL", LIT_INSTRUCTION_ABX) // G[C(A)] := RC(BX)
OPCODE(GET_GLOBAL, "GET_GLOBAL", LIT_INSTRUCTION_ABX) // R(A) := G[C(Bx)]
OPCODE(SET_UPVALUE, "SET_UPVALUE", LIT_INSTRUCTION_ABX) // U[A] := RC(Bx)
OPCODE(GET_UPVALUE, "GET_UPVALUE", LIT_INSTRUCTION_ABX) // R(A) := U[C(Bx)]
OPCODE(SET_PRIVATE, "SET_PRIVATE", LIT_INSTRUCTION_ABX) // P[A] := RC(Bx)
OPCODE(GET_PRIVATE, "GET_PRIVATE", LIT_INSTRUCTION_ABX) // R(A) := P[C(Bx)]

OPCODE(CALL, "CALL", LIT_INSTRUCTION_ABC) // R(A) := R(A)(R(A + 1), ..., R(A + B - 1))
OPCODE(CLOSE_UPVALUE, "CLOSE_UPVALUE", LIT_INSTRUCTION_ABC) // close_upvalue(R(A))

OPCODE(CLASS, "CLASS", LIT_INSTRUCTION_ABC) // G[C(A)] = R[C] = new_class(C(A), C(B - 1))
OPCODE(STATIC_FIELD, "STATIC_FIELD", LIT_INSTRUCTION_ABC) // R(A)[C(B)] = RC(C)
OPCODE(METHOD, "METHOD", LIT_INSTRUCTION_ABC) // R(A).Methods[C(B)] = RC(C)
OPCODE(GET_FIELD, "GET_FIELD", LIT_INSTRUCTION_ABC) // R(A) = R(B)[C(C)]
OPCODE(SET_FIELD, "SET_FIELD", LIT_INSTRUCTION_ABC) // R(A)[C(B)] = R(C)
OPCODE(IS, "IS", LIT_INSTRUCTION_ABC) // R(A) := RC(B) is G[C(C)]
OPCODE(INVOKE, "INVOKE", LIT_INSTRUCTION_ABC) // R(A) := R(A)[C(C)](R(A + 1), ..., R(A + B - 1))
OPCODE(SUBSCRIPT_GET, "SUBSCRIPT_GET", LIT_INSTRUCTION_ABC) // R(A) := R(A)[RC(B)]
OPCODE(SUBSCRIPT_SET, "SUBSCRIPT_SET", LIT_INSTRUCTION_ABC) // R(A)[RC(B)] := R(C)

OPCODE(PUSH_ARRAY_ELEMENT, "PUSH_ARRAY_ELEMENT", LIT_INSTRUCTION_ABX) // R(A)[R(A).count++] = RC(Bx)