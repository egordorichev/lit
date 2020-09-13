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

bool lit_dir_exists(const char* path) {
	struct stat buffer;
	return stat(path, &buffer) == 0 && S_ISDIR(buffer.st_mode);
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

	for (uint16_t i = 0; i < c; i++) {
		lit_write_uint8_t(file, (uint8_t) string->chars[i] ^ LIT_STRING_KEY);
	}
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

	for (uint16_t i = 0; i < length; i++) {
		line[i] = (char) lit_read_uint8_t(file) ^ LIT_STRING_KEY;
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
		line[i] = (char) lit_read_euint8_t(file) ^ LIT_STRING_KEY;
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
	bool disabled = lit_is_optimization_enabled(OPTIMIZATION_PRIVATE_NAMES);

	lit_write_string(file, module->name);
	lit_write_uint16_t(file, module->private_count);
	lit_write_uint8_t(file, (uint8_t) disabled);

	if (!disabled) {
		LitTable* privates = &module->private_names->values;

		for (uint i = 0; i < module->private_count; i++) {
			if (privates->entries[i].key != NULL) {
				lit_write_string(file, privates->entries[i].key);
				lit_write_uint16_t(file, (uint16_t) AS_NUMBER(privates->entries[i].value));
			}
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
		bool enabled = !((bool) lit_read_euint8_t(&file));

		module->privates = LIT_ALLOCATE(state, LitValue, privates_count);
		module->private_count = privates_count;

		for (uint16_t i = 0; i < privates_count; i++) {
			module->privates[i] = NULL_VALUE;

			if (enabled) {
				LitString* name = lit_read_estring(state, &file);
				lit_table_set(state, privates, name, NUMBER_VALUE(lit_read_euint16_t(&file)));
			}
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

bool lit_generate_source_file(const char* file, const char* output) {
	FILE* in = fopen(file, "r");

	if (in == NULL) {
		return false;
	}

	FILE* out = fopen(output, "w");

	if (out == NULL) {
		return false;
	}

	fprintf(out, "#include <stdlib.h>\nconst char bytecode[] = {\n");

	unsigned char buf[256];
	size_t num_read = 0;
	size_t line_count = 0;

	do {
		num_read = fread(buf, 1, sizeof(buf), in);

		for (size_t i = 0; i < num_read; i++) {
			fprintf(out, "0x%02x,", buf[i]);

			if (++line_count == 10) {
				fprintf(out, "\n");
				line_count = 0;
			}
		}
	} while (num_read > 0);

	if (line_count > 0) {
		fprintf(out, "\n");
	}

	fprintf(out, "};");

	fclose(in);
	fclose(out);

	return true;
}

static char* format(const char* message, ...) {
	va_list args;
	va_start(args, message);

	va_list args_copy;
	va_copy(args_copy, args);
	size_t needed = vsnprintf(NULL, 0, message, args_copy) + 1;
	va_end(args_copy);

	char* buffer = malloc(needed);

	vsnprintf(buffer, needed, message, args);
	va_end(args);

	return buffer;
}

void lit_build_native_runner(const char* bytecode_file) {
	const char* data_location = getenv("HOME");

	if (data_location == NULL) {
		fprintf(stderr, "Failed to locate data directory.");
		return;
	}

	char* dir = format("%s/.lit/", data_location);

	if (!lit_dir_exists(dir)) {
		printf("Clonning lit...\n");

		char* git_clone = format("git clone %s %s -q", LIT_REPOSITORY, dir);
		int result = system(git_clone);
		free(git_clone);

		if (result != 0) {
			fprintf(stderr, "Failed to clone lit.");
			return;
		}
	}

	printf("Updating lit...\n");
	char* git_pull = format("git pull -q");
	int result = system(git_pull);
	free(git_pull);

	if (result != 0) {
		fprintf(stderr, "Failed to update lit.");
		return;
	}

	char* output = format("%sbytecode.c", dir);
	bool r = lit_generate_source_file(bytecode_file, output);

	free(output);
	free(dir);

	if (!r)	{
		fprintf(stderr, "Failed generate bytecode wrapper.");
		return;
	}

	printf("Compiling lit...\n");
	char* cmake = format("cmake -DSTANDALONE=ON .");
	result = system(cmake);
	free(cmake);

	if (result != 0) {
		fprintf(stderr, "Failed to compile lit.");
		return;
	}

	char* make = format("make");
	result = system(make);
	free(make);

	if (result != 0) {
		fprintf(stderr, "Failed to compile lit.");
		return;
	}

	free(dir);
}