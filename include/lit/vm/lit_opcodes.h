OPCODE(MOVE, "MOVE", LIT_INSTRUCTION_ABC) // R(A) := R(B)
OPCODE(LOADK, "LOADK", LIT_INSTRUCTION_ABX) // R(A) := K(Bx)
OPCODE(RETURN, "RETURN", LIT_INSTRUCTION_ABC) // return R(A), ..., R(A + B - 2)

OPCODE(ADD, "ADD", LIT_INSTRUCTION_ABC) // R(A) := R(B) + R(C)
// OPCODE(SUB, "SUB", LIT_INSTRUCTION_ABC) // R(A) := R(B) - R(C)
// OPCODE(MUL, "MUL", LIT_INSTRUCTION_ABC) // R(A) := R(B) * R(C)
// OPCODE(DIV, "DIV", LIT_INSTRUCTION_ABC) // R(A) := R(B) / R(C)