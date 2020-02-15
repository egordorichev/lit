#ifndef LIT_OBJECT_H
#define LIT_OBJECT_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_chunk.h>
#include <lit/lit.h>

#define OBJECT_TYPE(value) (AS_OBJECT(value)->type)

#define IS_OBJECTS_TYPE(value, t) (IS_OBJECT(value) && AS_OBJECT(value)->type == t)
#define IS_STRING(value) IS_OBJECTS_TYPE(value, OBJECT_STRING)
#define IS_FUNCTION(value) IS_OBJECTS_TYPE(value, OBJECT_FUNCTION)
#define IS_NATIVE(value) IS_OBJECTS_TYPE(value, OBJECT_NATIVE)
#define IS_MODULE(value) IS_OBJECTS_TYPE(value, OBJECT_MODULE)

#define AS_STRING(value) ((LitString*) AS_OBJECT(value))
#define AS_CSTRING(value) (((LitString*) AS_OBJECT(value))->chars)
#define AS_FUNCTION(value) ((LitFunction*) AS_OBJECT(value))
#define AS_NATIVE(value) (((LitNative*) AS_OBJECT(value))->function)
#define AS_MODULE(value) ((LitModule*) AS_OBJECT(value))

#define ALLOCATE_OBJECT(state, type, objectType) (type*) lit_allocate_object(state, sizeof(type), objectType)

typedef enum {
	OBJECT_STRING,
	OBJECT_FUNCTION,
	OBJECT_NATIVE,
	OBJECT_FIBER,
	OBJECT_MODULE
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

typedef enum {
	FUNCTION_REGULAR,
	FUNCTION_SCRIPT
} LitFunctionType;

typedef struct {
	LitObject object;

	LitChunk chunk;
	LitString* name;
	uint arg_count;
} LitFunction;

LitFunction* lit_create_function(LitState* state);

typedef LitValue (*LitNativeFn)(LitVm* vm, uint arg_count, LitValue* args);

typedef struct {
	LitObject object;
	LitNativeFn function;
} LitNative;

LitNative* lit_create_native(LitState* state, LitNativeFn function);

typedef struct {
	LitFunction* function;
	uint8_t* ip;
	LitValue* slots;
} LitCallFrame;

typedef struct {
	LitObject object;

	LitValue return_value;
	LitString* name;

	uint privates_count;
	LitValue* privates;

	LitFunction* main_function;
} LitModule;

LitModule* lit_create_module(LitState* state, LitString* name);

typedef struct LitFiber {
	LitObject object;

	struct LitFiber* parent;

	LitValue stack[LIT_STACK_MAX];
	LitValue* stack_top;

	LitCallFrame frames[LIT_CALL_FRAMES_MAX];
	uint frame_count;

	LitModule* module;
} LitFiber;

LitFiber* lit_create_fiber(LitState* state, LitModule* module, LitFunction* function);

#endif