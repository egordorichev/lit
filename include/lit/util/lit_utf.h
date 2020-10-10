#ifndef LIT_UTF_H
#define LIT_UTF_H

#include "lit_common.h"
#include "vm/lit_value.h"

int lit_decode_num_bytes(uint8_t byte);
int lit_ustring_length(LitString* string);
int lit_encode_num_bytes(int value);
int lit_ustring_decode(const uint8_t* bytes, uint32_t length);
int lit_ustring_encode(int value, uint8_t* bytes);

LitString* lit_ustring_code_point_at(LitState* state, LitString* string, uint32_t index);
LitString* lit_ustring_from_code_point(LitState* state, int value);
LitString* lit_ustring_from_range(LitState* state, LitString* source, int start, uint32_t count);

int lit_uchar_offset(char *str, int index);

static inline bool lit_is_digit(char c) {
	return c >= '0' && c <= '9';
}

static inline bool lit_is_alpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

#endif