#include <lit/util/lit_fs.h>

#include <stdlib.h>
#include <stdio.h>

static uint8_t btmp;
static uint16_t stmp;
static uint32_t itmp;
static double dtmp;

const char* lit_read_file(const char* path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		return NULL;
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*) malloc(fileSize + 1);
	size_t bytes_read = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytes_read] = '\0';

	fclose(file);
	return buffer;
}

void lit_write_uint8_t(FILE* file, uint8_t byte) {
	fwrite(&byte, sizeof(uint8_t), 1, file);
}

void lit_write_uint16_t(FILE* file, uint16_t byte) {
	fwrite(&byte, sizeof(uint16_t), 1, file);
}

void lit_write_uint32_t(FILE* file, uint32_t byte) {
	fwrite(&byte, sizeof(uint32_t), 1, file);
}

void lit_write_double(FILE* file, double byte) {
	fwrite(&byte, sizeof(double), 1, file);
}

void lit_write_string(FILE* file, LitString* string) {
	uint16_t c = string->length;

	fwrite(&c, 2, 1, file);
	fwrite(string->chars, string->length, 1, file);
}

uint8_t lit_read_uint8_t(FILE* file) {
	fread(&btmp, sizeof(uint8_t), 1, file);
	return btmp;
}

uint16_t lit_read_uint16_t(FILE* file) {
	fread(&stmp, sizeof(uint16_t), 1, file);
	return stmp;
}

uint32_t lit_read_uint32_t(FILE* file) {
	fread(&itmp, sizeof(uint32_t), 1, file);
	return itmp;
}

double lit_read_double(FILE* file) {
	fread(&dtmp, sizeof(double), 1, file);
	return dtmp;
}

LitString* lit_read_string(LitState* state, FILE* file) {
	uint16_t length;
	fread(&length, 2, 1, file);

	if (length < 1) {
		return NULL;
	}

	char line[length];

	if (!fread(line, length, 1, file)) {
		return NULL;
	}

	return lit_copy_string(state, line, length);
}

void lit_init_emulated_file(LitEmulatedFile* file, const char* source) {
	file->source = source;
	file->position = 0;
}

uint8_t lit_read_euint8_t(LitEmulatedFile* file) {
	return (uint8_t) file->source[file->position++];
}

uint16_t lit_read_euint16_t(LitEmulatedFile* file) {
	return (uint16_t) ((lit_read_euint8_t(file) << 8u) | lit_read_euint8_t(file));
}

uint32_t lit_read_euint32_t(LitEmulatedFile* file) {
	return (uint32_t) ((lit_read_euint8_t(file) << 24u) | (lit_read_euint8_t(file) << 16u) | (lit_read_euint8_t(file) << 8u) | lit_read_euint8_t(file));
}

double lit_read_edouble(LitEmulatedFile* file) {
	double result = 0;

	for (int i = 0; i < 8; i++) {
		result += lit_read_euint8_t(file) << (i * 8);
	}

	return result;
}

LitString* lit_read_estring(LitState* state, LitEmulatedFile* file) {
	uint16_t length = lit_read_euint16_t(file);

	if (length < 1) {
		return NULL;
	}

	char line[length];

	for (uint16_t i = 0; i < length; i++) {
		line[i] = (char) lit_read_euint8_t(file);
	}

	return lit_copy_string(state, line, length);
}

static void save_function(FILE* file, LitFunction* function) {
	lit_save_chunk(file, &function->chunk);
	lit_write_string(file, function->name);

	lit_write_uint8_t(file, function->arg_count);
	lit_write_uint16_t(file, function->upvalue_count);
}

bool lit_save_module(LitModule* module, const char* output_file) {
	FILE* file = fopen(output_file, "w+b");

	if (file == NULL) {
		return false;
	}

	lit_write_string(file, module->name);

	LitTable* privates = &module->private_names;
	lit_write_uint16_t(file, privates->count);

	for (int i = 0; i < privates->capacity; i++) {
		if (privates->entries[i].key != NULL) {
			lit_write_string(file, privates->entries[i].key);
			lit_write_uint16_t(file, (uint16_t) AS_NUMBER(privates->entries[i].value));
		}
	}

	save_function(file, module->main_function);
	fclose(file);

	return true;
}