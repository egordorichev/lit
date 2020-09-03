#include <lit/optimizer/lit_optimizer.h>
#include <lit/util/lit_fs.h>
#include <lit/lit.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

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

bool lit_file_exists(const char* path) {
	struct stat buffer;
	return stat(path, &buffer) == 0 && S_ISREG(buffer.st_mode);
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
	return (uint16_t) (lit_read_euint8_t(file) | (lit_read_euint8_t(file) << 8u));
}

uint32_t lit_read_euint32_t(LitEmulatedFile* file) {
	return (uint32_t) (lit_read_euint8_t(file) | (lit_read_euint8_t(file) << 8u) | (lit_read_euint8_t(file) << 16u) | (lit_read_euint8_t(file) << 24u));
}

double lit_read_edouble(LitEmulatedFile* file) {
	uint8_t values[8];
	double result;

	for (uint i = 0; i < 8; i++) {
		values[i] = lit_read_euint8_t(file);
	}

	memcpy(&result, values, 8);
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

static void save_chunk(FILE* file, LitChunk* chunk);
static void load_chunk(LitState* state, LitEmulatedFile* file, LitModule* module, LitChunk* chunk);

static void save_function(FILE* file, LitFunction* function) {
	save_chunk(file, &function->chunk);
	lit_write_string(file, function->name);

	lit_write_uint8_t(file, function->arg_count);
	lit_write_uint16_t(file, function->upvalue_count);
	lit_write_uint8_t(file, (uint8_t) function->vararg);
	lit_write_uint16_t(file, (uint16_t) function->max_slots);
}

static LitFunction* load_function(LitState* state, LitEmulatedFile* file, LitModule* module) {
	LitFunction* function = lit_create_function(state, module);

	load_chunk(state, file, module, &function->chunk);
	function->name = lit_read_estring(state, file);

	function->arg_count = lit_read_euint8_t(file);
	function->upvalue_count = lit_read_euint16_t(file);
	function->vararg = (bool) lit_read_euint8_t(file);
	function->max_slots = lit_read_euint16_t(file);

	return function;
}

static void save_chunk(FILE* file, LitChunk* chunk) {
	lit_write_uint32_t(file, chunk->count);

	for (uint i = 0; i < chunk->count; i++) {
		lit_write_uint8_t(file, chunk->code[i]);
	}

	if (chunk->has_line_info) {
		uint c = chunk->line_count * 2 + 2;
		lit_write_uint32_t(file, c);

		for (uint i = 0; i < c; i++) {
			lit_write_uint16_t(file, chunk->lines[i]);
		}
	} else {
		lit_write_uint32_t(file, 0);
	}

	lit_write_uint32_t(file, chunk->constants.count);

	for (uint i = 0; i < chunk->constants.count; i++) {
		LitValue constant = chunk->constants.values[i];

		if (IS_OBJECT(constant)) {
			LitObjectType type = AS_OBJECT(constant)->type;
			lit_write_uint8_t(file, (uint8_t) (type + 1));

			switch (type) {
				case OBJECT_STRING: {
					lit_write_string(file, AS_STRING(constant));
					break;
				}

				case OBJECT_FUNCTION: {
					save_function(file, AS_FUNCTION(constant));
					break;
				}

				default: {
					UNREACHABLE
					break;
				}
			}
		} else {
			lit_write_uint8_t(file, 0);
			lit_write_double(file, AS_NUMBER(constant));
		}
	}
}

static void load_chunk(LitState* state, LitEmulatedFile* file, LitModule* module, LitChunk* chunk) {
	lit_init_chunk(chunk);

	uint count = lit_read_euint32_t(file);
	chunk->code = (uint8_t*) lit_reallocate(state, NULL, 0, sizeof(uint8_t) * count);
	chunk->count = count;
	chunk->capacity = count;

	for (uint i = 0; i < count; i++) {
		chunk->code[i] = lit_read_euint8_t(file);
	}

	count = lit_read_euint32_t(file);

	if (count > 0) {
		chunk->lines = (uint16_t*) lit_reallocate(state, NULL, 0, sizeof(uint16_t) * count);
		chunk->line_count = count;
		chunk->line_capacity = count;

		for (uint i = 0; i < count; i++) {
			chunk->lines[i] = lit_read_euint16_t(file);
		}
	} else {
		chunk->has_line_info = false;
	}

	count = lit_read_euint32_t(file);
	chunk->constants.values = (LitValue*) lit_reallocate(state, NULL, 0, sizeof(LitValue) * count);
	chunk->constants.count = count;
	chunk->constants.capacity = count;

	for (uint i = 0; i < count; i++) {
		uint8_t type = lit_read_euint8_t(file);

		if (type == 0) {
			chunk->constants.values[i] = NUMBER_VALUE(lit_read_edouble(file));
		} else {
			switch ((LitObjectType) (type - 1)) {
				case OBJECT_STRING: {
					chunk->constants.values[i] = OBJECT_VALUE(lit_read_estring(state, file));
					break;
				}

				case OBJECT_FUNCTION: {
					chunk->constants.values[i] = OBJECT_VALUE(load_function(state, file, module));
					break;
				}

				default: {
					UNREACHABLE
					break;
				}
			}
		}
	}
}

void lit_save_module(LitModule* module, FILE* file) {
	lit_write_string(file, module->name);

	LitTable* privates = lit_is_optimization_enabled(OPTIMIZATION_PRIVATE_NAMES) ? 0 : &module->private_names->values;
	lit_write_uint16_t(file, privates->count);

	for (int i = 0; i < privates->capacity; i++) {
		if (privates->entries[i].key != NULL) {
			lit_write_string(file, privates->entries[i].key);
			lit_write_uint16_t(file, (uint16_t) AS_NUMBER(privates->entries[i].value));
		}
	}

	save_function(file, module->main_function);
}

LitModule* lit_load_module(LitState* state, const char* input) {
	LitEmulatedFile file;
	lit_init_emulated_file(&file, input);

	if (lit_read_euint16_t(&file) != LIT_BYTECODE_MAGIC_NUMBER) {
		lit_error(state, COMPILE_ERROR, "Failed to read compiled code, unknown magic number");
		return NULL;
	}

	uint8_t bytecode_version = lit_read_euint8_t(&file);

	if (bytecode_version > LIT_BYTECODE_VERSION) {
		lit_error(state, COMPILE_ERROR, "Failed to read compiled code, unknown bytecode version '%i'", (int) bytecode_version);
		return NULL;
	}

	uint16_t module_count = lit_read_euint16_t(&file);
	LitModule* first = NULL;

	for (uint16_t j = 0; j < module_count; j++) {
		LitModule *module = lit_create_module(state, lit_read_estring(state, &file));
		LitTable *privates = &module->private_names->values;

		uint16_t privates_count = lit_read_euint16_t(&file);
		module->privates = LIT_ALLOCATE(state, LitValue, privates_count);

		for (uint16_t i = 0; i < privates_count; i++) {
			LitString *name = lit_read_estring(state, &file);
			uint16_t id = lit_read_euint16_t(&file);

			lit_table_set(state, privates, name, NUMBER_VALUE(id));
		}

		module->main_function = load_function(state, &file, module);
		lit_table_set(state, &state->vm->modules->values, module->name, OBJECT_VALUE(module));

		if (j == 0) {
			first = module;
		}
	}

	if (lit_read_euint16_t(&file) != LIT_BYTECODE_END_NUMBER) {
		lit_error(state, COMPILE_ERROR, "Failed to read compiled code, unknown end number");
		return NULL;
	}

	return first;
}