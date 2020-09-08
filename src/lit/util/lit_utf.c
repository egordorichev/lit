#include <lit/util/lit_utf.h>
#include <lit/vm/lit_object.h>

#include <wchar.h>

#define is_utf(c) (((c) & 0xC0) != 0x80)

int lit_ustring_length(LitString* string){
	int length = 0;

	for (uint32_t i = 0; i < string->length; i++) {
		i += lit_ustring_num_bytes(string->chars[i]);
		length++;
	}

	return length;
}

LitString* lit_ustring_code_point_at(LitState* state, LitString* string, uint32_t index) {
	if (index >= string->length) {
		return NULL;
	}

	int code_point = lit_ustring_decode((uint8_t*) string->chars + index, string->length - index);

	if (code_point == -1) {
		char bytes[2];

		bytes[0] = string->chars[index];
		bytes[1] = '\0';

		return lit_copy_string(state, bytes, 1);
	}

	return lit_ustring_from_code_point(state, code_point);
}

LitString* lit_ustring_from_code_point(LitState* state, int value) {
	int length = lit_ustring_num_bytes(value);
	char bytes[length + 1];

	lit_ustring_encode(value, (uint8_t*) bytes);
	return lit_copy_string(state, bytes, length);
}

LitString* lit_ustring_from_range(LitState* state, LitString* source, int start, uint32_t count, int step) {
	uint8_t* from = (uint8_t*) source->chars;
	int length = 0;

	for (uint32_t i = 0; i < count; i++) {
		length += lit_ustring_num_bytes(from[start + i * step]);
	}

	char bytes[length];
	uint8_t* to = (uint8_t*) bytes;

	for (uint32_t i = 0; i < source->length; i++) {
		int index = start + i * step;
		int code_point = lit_ustring_decode(from + index, source->length - index);

		if (code_point != -1) {
			to += lit_ustring_encode(code_point, to);

			if ((uint8_t*) bytes - to >= (long) count) {
				break;
			}
		}
	}

	return lit_copy_string(state, bytes, length);
}

int lit_ustring_num_bytes(int value) {
	if (value <= 0x7f) {
		return 1;
	}

	if (value <= 0x7ff) {
		return 2;
	}

	if (value <= 0xffff) {
		return 3;
	}

	if (value <= 0x10ffff) {
		return 4;
	}

	return 0;
}

int lit_ustring_encode(int value, uint8_t* bytes) {
	if (value <= 0x7f) {
		*bytes = value & 0x7f;
		return 1;
	} else if (value <= 0x7ff) {
		*bytes = 0xc0 | ((value & 0x7c0) >> 6);
		bytes++;
		*bytes = 0x80 | (value & 0x3f);
		return 2;
	} else if (value <= 0xffff) {
		*bytes = 0xe0 | ((value & 0xf000) >> 12);
		bytes++;
		*bytes = 0x80 | ((value & 0xfc0) >> 6);
		bytes++;
		*bytes = 0x80 | (value & 0x3f);

		return 3;
	} else if (value <= 0x10ffff) {
		*bytes = 0xf0 | ((value & 0x1c0000) >> 18);
		bytes++;
		*bytes = 0x80 | ((value & 0x3f000) >> 12);
		bytes++;
		*bytes = 0x80 | ((value & 0xfc0) >> 6);
		bytes++;
		*bytes = 0x80 | (value & 0x3f);

		return 4;
	}

	UNREACHABLE
	return 0;
}

int lit_ustring_decode(const uint8_t* bytes, uint32_t length) {
	if (*bytes <= 0x7f) {
		return *bytes;
	}

	int value;
	uint32_t remaining_bytes;
	
	if ((*bytes & 0xe0) == 0xc0) {
		value = *bytes & 0x1f;
		remaining_bytes = 1;
	} else if ((*bytes & 0xf0) == 0xe0) {
		value = *bytes & 0x0f;
		remaining_bytes = 2;
	} else if ((*bytes & 0xf8) == 0xf0) {
		value = *bytes & 0x07;
		remaining_bytes = 3;
	} else {
		return -1;
	}

	if (remaining_bytes > length - 1) {
		return -1;
	}

	while (remaining_bytes > 0) {
		bytes++;
		remaining_bytes--;

		if ((*bytes & 0xc0) != 0x80) {
			return -1;
		}

		value = value << 6 | (*bytes & 0x3f);
	}

	return value;
}

int lit_uchar_offset(char *str, int index) {
	int offset = 0;

	while (index > 0 && str[offset]) {
		(void) (is_utf(str[++offset]) || is_utf(str[++offset]) || is_utf(str[++offset]) || ++offset);
		index--;
	}

	return offset;
}

#undef is_utf