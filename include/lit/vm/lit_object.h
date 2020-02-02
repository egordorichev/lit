#ifndef LIT_OBJECT_H
#define LIT_OBJECT_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_chunk.h>

#define OBJECT_TYPE(value) (AS_OBJECT(value)->type)

#define IS_OBJECTS_TYPE(value, t) (IS_OBJECT(value) && AS_OBJECT(value)->type == t)
#define IS_STRING(value) IS_OBJECTS_TYPE(value, OBJECT_STRING)
#define IS_FUNCTION(value) IS_OBJECTS_TYPE(value, OBJECT_FUNCTION)

#define AS_STRING(value) ((LitString*) AS_OBJECT(value))
#define AS_CSTRING(value) (((LitString*) AS_OBJECT(value))->chars)
#define AS_FUNCTION(value) ((LitFunction*) AS_OBJECT(value))

#define ALLOCATE_OBJECT(state, type, objectType) (type*) lit_allocate_object(state, sizeof(type), objectType)

typedef enum {
	OBJECT_STRING,
	OBJECT_FUNCTION
} LitObjectType;

typedef struct sLitObject {
	LitObjectType type;
	struct sLitObject* next;
} sLitObject;

LitObject* lit_allocate_object(LitState* state, size_t size, LitObjectType type);

typedef struct {
	LitObject object;

	LitChunk chunk;
	LitString* name;
	uint arg_count;
} LitFunction;

typedef enum {
	FUNCTION_REGULAR,
	FUNCTION_SCRIPT
} LitFunctionType;

LitFunction* lit_create_function(LitState* state);

typedef struct sLitString {
	LitObject object;

	uint length;
	uint32_t hash;
	char* chars;
} sLitString;

LitString* lit_copy_string(LitState* state, const char* chars, uint length);

#endif