#include <lit/std/lit_file.h>
#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/util/lit_fs.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#ifdef LIT_OS_WINDOWS
#define stat _stat
#endif

/*
 * File
 */

typedef struct {
	char* path;
	FILE* file;
} LitFileData;

static LitFileData* extract_file_data(LitState* state, LitValue instance) {
	LitValue data;

	if (!lit_table_get(&AS_INSTANCE(instance)->fields, CONST_STRING(state, "_data"), &data)) {
		return 0;
	}

	return (LitFileData*) AS_USERDATA(data)->data;
}

void cleanup_file(LitState* state, LitUserdata* data) {
	LitFileData* file_data = ((LitFileData*) data->data);

	if (file_data->file != NULL) {
		fclose(file_data->file);
		file_data->file = NULL;
	}
}

LIT_METHOD(file_constructor) {
	const char* path = LIT_CHECK_STRING(0);
	const char* mode = LIT_GET_STRING(1, "r");

	LitUserdata* userdata = lit_create_userdata(vm->state, sizeof(LitFileData));
	userdata->cleanup_fn = cleanup_file;

	lit_table_set(vm->state, &AS_INSTANCE(instance)->fields, CONST_STRING(vm->state, "_data"), OBJECT_VALUE(userdata));
	LitFileData* data = (LitFileData*) userdata->data;

	data->path = (char*) path;

	if ((data->file = fopen(path, mode)) == NULL) {
		lit_runtime_error(vm, "Failed to open file %s with mode %s (C error: %s)", path, mode, strerror(errno));
	}

	return instance;
}

LIT_METHOD(file_close) {
	LitFileData* data = extract_file_data(vm->state, instance);

	if (data->file != NULL) {
		fclose(data->file);
	}

	return NULL_VALUE;
}

LIT_METHOD(file_exists) {
	char* file_name = NULL;

	if (IS_INSTANCE(instance)) {
		file_name = extract_file_data(vm->state, instance)->path;
	} else {
		file_name = (char*) LIT_CHECK_STRING(0);
	}

	return BOOL_VALUE(lit_file_exists(file_name));
}

/*
 * ==
 * File writing
 */

LIT_METHOD(file_write) {
	LIT_ENSURE_ARGS(1)

	LitString* value = lit_to_string(vm->state, args[0]);
	fwrite(value->chars, value->length, 1, extract_file_data(vm->state, instance)->file);

	return NULL_VALUE;
}

LIT_METHOD(file_writeByte) {
	uint8_t byte = (uint8_t) LIT_CHECK_NUMBER(0);
	lit_write_uint8_t(extract_file_data(vm->state, instance)->file, byte);

	return NULL_VALUE;
}

LIT_METHOD(file_writeShort) {
	uint16_t shrt = (uint16_t) LIT_CHECK_NUMBER(0);
	lit_write_uint16_t(extract_file_data(vm->state, instance)->file, shrt);

	return NULL_VALUE;
}

LIT_METHOD(file_writeNumber) {
	float num = (float) LIT_CHECK_NUMBER(0);
	lit_write_uint32_t(extract_file_data(vm->state, instance)->file, num);

	return NULL_VALUE;
}

LIT_METHOD(file_writeBool) {
	bool value = LIT_CHECK_BOOL(0);

	lit_write_uint8_t(extract_file_data(vm->state, instance)->file, (uint8_t) value ? '1' : '0');
	return NULL_VALUE;
}

LIT_METHOD(file_writeString) {
	if (LIT_CHECK_STRING(0) == NULL) {
		return NULL_VALUE;
	}

	LitString* string = AS_STRING(args[0]);
	LitFileData* data = extract_file_data(vm->state, instance);

	lit_write_string(data->file, string);
	return NULL_VALUE;
}

/*
 * ==
 * File reading
 */

LIT_METHOD(file_readAll) {
	LitFileData* data = extract_file_data(vm->state, instance);

	fseek(data->file, 0, SEEK_END);
	uint length = ftell(data->file);
	fseek(data->file, 0, SEEK_SET);

	LitString* result = lit_allocate_empty_string(vm->state, length);

	result->chars = LIT_ALLOCATE(vm->state, char, length + 1);
	result->chars[length] = '\0';

	fread(result->chars, 1, length, data->file);

	result->hash = lit_hash_string(result->chars, result->length);
	lit_register_string(vm->state, result);

	return OBJECT_VALUE(result);
}

LIT_METHOD(file_readLine) {
	uint max_length = (uint) LIT_GET_NUMBER(0, 128);
	LitFileData* data = extract_file_data(vm->state, instance);

	char line[max_length];

	if (!fgets(line, max_length, data->file)) {
		return NULL_VALUE;
	}

	return OBJECT_VALUE(lit_copy_string(vm->state, line, strlen(line) - 1));
}

LIT_METHOD(file_readByte) {
	return NUMBER_VALUE(lit_read_uint8_t(extract_file_data(vm->state, instance)->file));
}

LIT_METHOD(file_readShort) {
	return NUMBER_VALUE(lit_read_uint16_t(extract_file_data(vm->state, instance)->file));
}

LIT_METHOD(file_readNumber) {
	return NUMBER_VALUE(lit_read_uint32_t(extract_file_data(vm->state, instance)->file));
}

LIT_METHOD(file_readBool) {
	return BOOL_VALUE((char) lit_read_uint8_t(extract_file_data(vm->state, instance)->file) == '1');
}

LIT_METHOD(file_readString) {
	LitFileData* data = extract_file_data(vm->state, instance);
	LitString* string = lit_read_string(vm->state, data->file);

	return string == NULL ? NULL_VALUE : OBJECT_VALUE(string);
}

LIT_METHOD(file_getLastModified) {
	struct stat buffer;
	char* file_name = NULL;

	if (IS_INSTANCE(instance)) {
		file_name = extract_file_data(vm->state, instance)->path;
	} else {
		file_name = (char*) LIT_CHECK_STRING(0);
	}

	if (stat(file_name, &buffer) != 0) {
		return NUMBER_VALUE(0);
	}

	return NUMBER_VALUE( buffer.st_mtim.tv_sec);
}


/*
 * Directory
 */

LIT_METHOD(directory_exists) {
	const char* directory_name = LIT_CHECK_STRING(0);
	struct stat buffer;

	return BOOL_VALUE(stat(directory_name, &buffer) == 0 && S_ISDIR(buffer.st_mode));
}

LIT_METHOD(directory_listFiles) {
	struct dirent* ep;

	LitState* state = vm->state;
	DIR* dir = opendir(LIT_CHECK_STRING(0));
	LitArray* array = lit_create_array(state);

	if (dir == NULL) {
		return OBJECT_VALUE(array);
	}

	while ((ep = readdir(dir))) {
		if (ep->d_type == DT_REG) {
			lit_values_write(state, &array->values, OBJECT_CONST_STRING(state, ep->d_name));
		}
	}

	return OBJECT_VALUE(array);
}

LIT_METHOD(directory_listDirectories) {
	struct dirent* ep;

	LitState* state = vm->state;
	DIR* dir = opendir(LIT_CHECK_STRING(0));
	LitArray* array = lit_create_array(state);

	if (dir == NULL) {
		return OBJECT_VALUE(array);
	}

	while ((ep = readdir(dir))) {
		if (ep->d_type == DT_DIR && strcmp(ep->d_name, "..") != 0 && strcmp(ep->d_name, ".") != 0) {
			lit_values_write(state, &array->values, OBJECT_CONST_STRING(state, ep->d_name));
		}
	}

	return OBJECT_VALUE(array);
}

void lit_open_file_library(LitState* state) {
	LIT_BEGIN_CLASS("File")
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
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Directory")
		LIT_BIND_STATIC_METHOD("exists", directory_exists)
		LIT_BIND_STATIC_METHOD("listFiles", directory_listFiles)
		LIT_BIND_STATIC_METHOD("listDirectories", directory_listDirectories)
	LIT_END_CLASS()
}