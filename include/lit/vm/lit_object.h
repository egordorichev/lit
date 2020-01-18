#ifndef LIT_OBJECT_H
#define LIT_OBJECT_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>

#define OBJECT_TYPE(value) (AS_OBJECT(value)->type)

#define IS_OBJECTS_TYPE(value, t) (IS_OBJECT(value) && AS_OBJECT(value)->type == t)
#define IS_STRING(value) IS_OBJECTS_TYPE(value, OBJECT_STRING)

#define AS_STRING(value) ((LitString*) AS_OBJECT(value))
#define AS_CSTRING(value) (((LitString*) AS_OBJECT(value))->chars)

#define ALLOCATE_OBJECT(state, type, objectType) (type*) lit_allocate_object(state, sizeof(type), objectType)

typedef enum {
	OBJECT_STRING
} LitObjectType;

typedef struct sLitObject {
	LitObjectType type;
	struct sLitObject* next;
} sLitObject;

LitObject* lit_allocate_object(LitState* state, size_t size, LitObjectType type);

typedef struct sLitString {
	LitObject object;

	uint length;
	uint32_t hash;
	char* chars;
} sLitString;

LitString* lit_copy_string(LitState* state, const char* chars, uint length);

#endif