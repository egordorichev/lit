#ifndef LIT_OBJECT_H
#define LIT_OBJECT_H

#include "lit_common.h"
#include "lit_predefines.h"
#include "mem/lit_mem.h"
#include "vm/lit_chunk.h"
#include "util/lit_table.h"
#include "vm/lit_value.h"
#include "lit_config.h"

#define OBJECT_TYPE(value) (AS_OBJECT(value)->type)

#define IS_OBJECTS_TYPE(value, t) (IS_OBJECT(value) && AS_OBJECT(value)->type == t)
#define IS_STRING(value) IS_OBJECTS_TYPE(value, OBJECT_STRING)
#define IS_FUNCTION(value) IS_OBJECTS_TYPE(value, OBJECT_FUNCTION)
#define IS_NATIVE_FUNCTION(value) IS_OBJECTS_TYPE(value, OBJECT_NATIVE_FUNCTION)
#define IS_NATIVE_PRIMITIVE(value) IS_OBJECTS_TYPE(value, OBJECT_NATIVE_PRIMITIVE)
#define IS_NATIVE_METHOD(value) IS_OBJECTS_TYPE(value, OBJECT_NATIVE_METHOD)
#define IS_PRIMITIVE_METHOD(value) IS_OBJECTS_TYPE(value, OBJECT_PRIMITIVE_METHOD)
#define IS_MODULE(value) IS_OBJECTS_TYPE(value, OBJECT_MODULE)
#define IS_CLOSURE(value) IS_OBJECTS_TYPE(value, OBJECT_CLOSURE)
#define IS_CLOSURE_PROTOTYPE(value) IS_OBJECTS_TYPE(value, OBJECT_CLOSURE_PROTOTYPE)
#define IS_UPVALUE(value) IS_OBJECTS_TYPE(value, OBJECT_UPVALUE)
#define IS_CLASS(value) IS_OBJECTS_TYPE(value, OBJECT_CLASS)
#define IS_INSTANCE(value) IS_OBJECTS_TYPE(value, OBJECT_INSTANCE)
#define IS_ARRAY(value) (IS_OBJECTS_TYPE(value, OBJECT_ARRAY) || IS_OBJECTS_TYPE(value, OBJECT_VARARG_ARRAY))
#define IS_VARARG_ARRAY(value) IS_OBJECTS_TYPE(value, OBJECT_VARARG_ARRAY)
#define IS_MAP(value) IS_OBJECTS_TYPE(value, OBJECT_MAP)
#define IS_BOUND_METHOD(value) IS_OBJECTS_TYPE(value, OBJECT_BOUND_METHOD)
#define IS_USERDATA(value) IS_OBJECTS_TYPE(value, OBJECT_USERDATA)
#define IS_RANGE(value) IS_OBJECTS_TYPE(value, OBJECT_RANGE)
#define IS_FIELD(value) IS_OBJECTS_TYPE(value, OBJECT_FIELD)
#define IS_REFERENCE(value) IS_OBJECTS_TYPE(value, OBJECT_REFERENCE)

bool lit_is_callable_function(LitValue value);
#define IS_CALLABLE_FUNCTION(value) lit_is_callable_function(value)

#define AS_STRING(value) ((LitString*) AS_OBJECT(value))
#define AS_CSTRING(value) (((LitString*) AS_OBJECT(value))->chars)
#define AS_FUNCTION(value) ((LitFunction*) AS_OBJECT(value))
#define AS_NATIVE_FUNCTION(value) ((LitNativeFunction*) AS_OBJECT(value))
#define AS_NATIVE_PRIMITIVE(value) ((LitNativePrimitive*) AS_OBJECT(value))
#define AS_NATIVE_METHOD(value) ((LitNativeMethod*) AS_OBJECT(value))
#define AS_PRIMITIVE_METHOD(value) ((LitPrimitiveMethod*) AS_OBJECT(value))
#define AS_MODULE(value) ((LitModule*) AS_OBJECT(value))
#define AS_CLOSURE(value) ((LitClosure*) AS_OBJECT(value))
#define AS_CLOSURE_PROTOTYPE(value) ((LitClosurePrototype*) AS_OBJECT(value))
#define AS_UPVALUE(value) ((LitUpvalue*) AS_OBJECT(value))
#define AS_CLASS(value) ((LitClass*) AS_OBJECT(value))
#define AS_INSTANCE(value) ((LitInstance*) AS_OBJECT(value))
#define AS_ARRAY(value) ((LitArray*) AS_OBJECT(value))
#define AS_MAP(value) ((LitMap*) AS_OBJECT(value))
#define AS_BOUND_METHOD(value) ((LitBoundMethod*) AS_OBJECT(value))
#define AS_USERDATA(value) ((LitUserdata*) AS_OBJECT(value))
#define AS_RANGE(value) ((LitRange*) AS_OBJECT(value))
#define AS_FIELD(value) ((LitField*) AS_OBJECT(value))
#define AS_FIBER(value) ((LitFiber*) AS_OBJECT(value))
#define AS_REFERENCE(value) ((LitReference*) AS_OBJECT(value))

#define ALLOCATE_OBJECT(state, type, objectType) (type*) lit_allocate_object(state, sizeof(type), objectType)
#define OBJECT_CONST_STRING(state, text) OBJECT_VALUE(lit_copy_string((state), (text), strlen(text)))
#define CONST_STRING(state, text) lit_copy_string((state), (text), strlen(text))

typedef enum {
	OBJECT_STRING,
	OBJECT_FUNCTION,
	OBJECT_NATIVE_FUNCTION,
	OBJECT_NATIVE_PRIMITIVE,
	OBJECT_NATIVE_METHOD,
	OBJECT_PRIMITIVE_METHOD,
	OBJECT_FIBER,
	OBJECT_MODULE,
	OBJECT_CLOSURE,
	OBJECT_CLOSURE_PROTOTYPE,
	OBJECT_UPVALUE,
	OBJECT_CLASS,
	OBJECT_INSTANCE,
	OBJECT_BOUND_METHOD,
	OBJECT_ARRAY,
	OBJECT_VARARG_ARRAY,
	OBJECT_MAP,
	OBJECT_USERDATA,
	OBJECT_RANGE,
	OBJECT_FIELD,
	OBJECT_REFERENCE
} LitObjectType;

static const char* lit_object_type_names[] = {
	"string",
	"function",
	"native_function",
	"native_primitive",
	"native_method",
	"primitive_method",
	"fiber",
	"module",
	"closure",
	"closure_prototype",
	"upvalue",
	"class",
	"instance",
	"bound_method",
	"array",
	"array",
	"map",
	"userdata",
	"range",
	"field",
	"reference"
};

typedef struct sLitObject {
	LitObjectType type;
	struct sLitObject* next;

	bool marked;
} sLitObject;

LitObject* lit_allocate_object(LitState* state, size_t size, LitObjectType type);

typedef struct sLitString {
	LitObject object;

	uint length;
	uint32_t hash;
	char* chars;
} sLitString;

LitString* lit_copy_string(LitState* state, const char* chars, uint length);
LitString* lit_take_string(LitState* state, const char* chars, uint length);
LitValue lit_string_format(LitState* state, const char* format, ...);
LitValue lit_number_to_string(LitState* state, double value);
void lit_register_string(LitState* state, LitString* string);
uint32_t lit_hash_string(const char* key, uint length);
LitString* lit_allocate_empty_string(LitState* state, uint length);

typedef enum {
	FUNCTION_REGULAR,
	FUNCTION_SCRIPT,
	FUNCTION_METHOD,
	FUNCTION_STATIC_METHOD,
	FUNCTION_CONSTRUCTOR
} LitFunctionType;

typedef struct {
	LitObject object;

	LitChunk chunk;
	LitString* name;

	uint8_t arg_count;
	uint16_t upvalue_count;
	uint8_t max_registers;

	bool vararg;

	struct sLitModule* module;
} LitFunction;

LitFunction* lit_create_function(LitState* state, LitModule* module);
LitValue lit_get_function_name(LitVm* vm, LitValue instance);

typedef struct sLitUpvalue {
	LitObject object;

	LitValue* location;
	LitValue closed;

	struct sLitUpvalue* next;
} LitUpvalue;

LitUpvalue* lit_create_upvalue(LitState* state, LitValue* slot);

typedef struct {
	LitObject object;
	LitFunction* function;

	LitUpvalue** upvalues;
	uint upvalue_count;
} LitClosure;

LitClosure* lit_create_closure(LitState* state, LitFunction* function);

typedef struct {
	LitObject object;
	LitFunction* function;

	bool* local;
	uint8_t* indexes;

	uint upvalue_count;
} LitClosurePrototype;

LitClosurePrototype* lit_create_closure_prototype(LitState* state, LitFunction* function);

typedef LitValue (*LitNativeFunctionFn)(LitVm* vm, uint arg_count, LitValue* args);

typedef struct {
	LitObject object;

	LitNativeFunctionFn function;
	LitString* name;
} LitNativeFunction;

LitNativeFunction* lit_create_native_function(LitState* state, LitNativeFunctionFn function, LitString* name);

typedef bool (*LitNativePrimitiveFn)(LitVm* vm, uint arg_count, LitValue* args);

typedef struct {
	LitObject object;

	LitNativePrimitiveFn function;
	LitString* name;
} LitNativePrimitive;

LitNativePrimitive* lit_create_native_primitive(LitState* state, LitNativePrimitiveFn function, LitString* name);

typedef LitValue (*LitNativeMethodFn)(LitVm* vm, LitValue instance, uint arg_count, LitValue* args);

typedef struct {
	LitObject object;

	LitNativeMethodFn method;
	LitString* name;
} LitNativeMethod;

LitNativeMethod* lit_create_native_method(LitState* state, LitNativeMethodFn function, LitString* name);

typedef bool (*LitPrimitiveMethodFn)(LitVm* vm, LitValue instance, uint arg_count, LitValue* args);

typedef struct {
	LitObject object;

	LitPrimitiveMethodFn method;
	LitString* name;
} LitPrimitiveMethod;

LitPrimitiveMethod* lit_create_primitive_method(LitState* state, LitPrimitiveMethodFn method, LitString* name);

typedef struct {
	LitFunction* function;
	LitClosure* closure;

	uint64_t* ip;
	LitValue* slots;
	LitValue* return_address;

	bool result_ignored;
	bool return_to_c;
} LitCallFrame;

typedef LitValue (*LitMapIndexFn)(LitVm* vm, struct sLitMap* map, LitString* index, LitValue* value);

typedef struct sLitMap {
	LitObject object;
	LitTable values;

	LitMapIndexFn index_fn;
} LitMap;

LitMap* lit_create_map(LitState* state);

bool lit_map_set(LitState* state, LitMap* map, LitString* key, LitValue value);
bool lit_map_get(LitMap* map, LitString* key, LitValue* value);
bool lit_map_delete(LitMap* map, LitString* key);
void lit_map_add_all(LitState* state, LitMap* from, LitMap* to);

typedef struct sLitModule {
	LitObject object;

	LitValue return_value;
	LitString* name;

	LitValue* privates;
	LitMap* private_names;
	uint private_count;

	LitFunction* main_function;
	struct sLitFiber* main_fiber;

	bool ran;
} LitModule;

LitModule* lit_create_module(LitState* state, LitString* name);

typedef struct sLitFiber {
	LitObject object;

	struct sLitFiber* parent;

	LitValue* registers;
	uint registers_allocated;

	LitCallFrame* frames;
	uint frame_capacity;
	uint frame_count;
	uint arg_count;

	LitValue* return_address;
	LitUpvalue* open_upvalues;
	LitModule* module;
	LitValue error;

	bool abort;
	bool catcher;
} LitFiber;

LitFiber* lit_create_fiber(LitState* state, LitModule* module, LitFunction* function);
LitFiber* lit_create_fiber_with_closure(LitState* state, LitModule* module, LitClosure* closure);

void lit_ensure_fiber_registers(LitState* state, LitFiber* fiber, uint needed);

typedef struct sLitClass {
	LitObject object;

	LitString* name;
	LitObject* init_method;

	LitTable methods;
	LitTable static_fields;

	struct sLitClass* super;
} LitClass;

LitClass* lit_create_class(LitState* state, LitString* name);

typedef struct {
	LitObject object;

	LitClass* klass;
	LitTable fields;
} LitInstance;

LitInstance* lit_create_instance(LitState* state, LitClass* klass);

typedef struct {
	LitObject object;

	LitValue receiver;
	LitValue method;
} LitBoundMethod;

LitBoundMethod* lit_create_bound_method(LitState* state, LitValue receiver, LitValue method);

typedef struct {
	LitObject object;
	LitValues values;
} LitArray;

LitArray* lit_create_array(LitState* state);

typedef struct {
	LitArray array;
} LitVarargArray;

LitVarargArray* lit_create_vararg_array(LitState* state);

typedef void (*LitCleanupFn)(LitState* state, LitUserdata* userdata, bool mark);

typedef struct sLitUserdata {
	LitObject object;

	void* data;
	size_t size;

	LitCleanupFn cleanup_fn;
} LitUserdata;

LitUserdata* lit_create_userdata(LitState* state, size_t size);

typedef struct {
	LitObject object;

	double from;
	double to;
} LitRange;

LitRange* lit_create_range(LitState* state, double from, double to);

typedef struct {
	LitObject object;

	LitObject* getter;
	LitObject* setter;
} LitField;

LitField* lit_create_field(LitState* state, LitObject* getter, LitObject* setter);

typedef struct {
	LitObject object;
	LitValue* slot;
} LitReference;

LitReference* lit_create_reference(LitState* state, LitValue* slot);

#endif