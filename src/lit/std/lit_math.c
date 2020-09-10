#include <lit/std/lit_math.h>
#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>

#include <math.h>
#include <stdlib.h>
#include <time.h>

LIT_METHOD(math_abs) {
	return NUMBER_VALUE(fabs(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_cos) {
	return NUMBER_VALUE(cos(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_sin) {
	return NUMBER_VALUE(sin(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_tan) {
	return NUMBER_VALUE(tan(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_acos) {
	return NUMBER_VALUE(acos(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_asin) {
	return NUMBER_VALUE(asin(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_atan) {
	return NUMBER_VALUE(atan(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_atan2) {
	return NUMBER_VALUE(atan2(LIT_CHECK_NUMBER(0), LIT_CHECK_NUMBER(1)));
}

LIT_METHOD(math_floor) {
	return NUMBER_VALUE(floor(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_ceil) {
	return NUMBER_VALUE(ceil(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_round) {
	return NUMBER_VALUE(round(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_min) {
	return NUMBER_VALUE(fmin(LIT_CHECK_NUMBER(0), LIT_CHECK_NUMBER(1)));
}

LIT_METHOD(math_max) {
	return NUMBER_VALUE(fmax(LIT_CHECK_NUMBER(0), LIT_CHECK_NUMBER(1)));
}

LIT_METHOD(math_mid) {
	double x = LIT_CHECK_NUMBER(0);
	double y = LIT_CHECK_NUMBER(1);
	double z = LIT_CHECK_NUMBER(2);

	if (x > y) {
		return NUMBER_VALUE(fmax(x, fmin(y, z)));
	} else {
		return NUMBER_VALUE(fmax(y, fmin(x, z)));
	}
}

LIT_METHOD(math_toRadians) {
	return NUMBER_VALUE(LIT_CHECK_NUMBER(0) * M_PI / 180.0);
}

LIT_METHOD(math_toDegrees) {
	return NUMBER_VALUE(LIT_CHECK_NUMBER(0) * 180.0 / M_PI);
}

LIT_METHOD(math_sqrt) {
	return NUMBER_VALUE(sqrt(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_log) {
	return NUMBER_VALUE(exp(LIT_CHECK_NUMBER(0)));
}

LIT_METHOD(math_exp) {
	return NUMBER_VALUE(exp(LIT_CHECK_NUMBER(0)));
}

/*
 * Random
 */

static uint static_random_data;

static uint* extract_random_data(LitState* state, LitValue instance) {
	if (IS_CLASS(instance)) {
		return &static_random_data;
	}

	LitValue data;

	if (!lit_table_get(&AS_INSTANCE(instance)->fields, CONST_STRING(state, "_data"), &data)) {
		return 0;
	}

	return (uint*) AS_USERDATA(data)->data;
}

LIT_METHOD(random_constructor) {
	LitUserdata* userdata = lit_create_userdata(vm->state, sizeof(uint));
	lit_table_set(vm->state, &AS_INSTANCE(instance)->fields, CONST_STRING(vm->state, "_data"), OBJECT_VALUE(userdata));

	uint* data = (uint*) userdata->data;

	if (arg_count == 1) {
		uint number = (uint) LIT_CHECK_NUMBER(0);
		*data = number;
	} else {
		*data = time(NULL);
	}

	return OBJECT_VALUE(instance);
}

LIT_METHOD(random_setSeed) {
	uint* data = extract_random_data(vm->state, instance);

	if (arg_count == 1) {
		uint number = (uint) LIT_CHECK_NUMBER(0);
		*data = number;
	} else {
		*data = time(NULL);
	}

	return NULL_VALUE;
}

LIT_METHOD(random_int) {
	uint* data = extract_random_data(vm->state, instance);

	if (arg_count == 1) {
		int bound = (int) LIT_GET_NUMBER(0, 0);
		return NUMBER_VALUE(rand_r(data) % bound);
	} else if (arg_count == 2) {
		int min = (int) LIT_GET_NUMBER(0, 0);
		int max = (int) LIT_GET_NUMBER(1, 1);

		if (max - min == 0) {
			return NUMBER_VALUE(max);
		}

		return NUMBER_VALUE(min + rand_r(data) % (max - min));
	}

	return NUMBER_VALUE(rand_r(data));
}

LIT_METHOD(random_float) {
	uint* data = extract_random_data(vm->state, instance);
	double value = (double) rand_r(data) / RAND_MAX;

	if (arg_count == 1) {
		int bound = (int) LIT_GET_NUMBER(0, 0);
		return NUMBER_VALUE(value * bound);
	} else if (arg_count == 2) {
		int min = (int) LIT_GET_NUMBER(0, 0);
		int max = (int) LIT_GET_NUMBER(1, 1);

		if (max - min == 0) {
			return NUMBER_VALUE(max);
		}

		return NUMBER_VALUE(min + value * (max - min));
	}

	return NUMBER_VALUE(value);
}

LIT_METHOD(random_bool) {
	return BOOL_VALUE(rand_r(extract_random_data(vm->state, instance)) % 2);
}

LIT_METHOD(random_chance) {
	float c = LIT_GET_NUMBER(0, 50);
	return BOOL_VALUE((((float) rand_r(extract_random_data(vm->state, instance))) / RAND_MAX * 100) <= c);
}

LIT_METHOD(random_pick) {
	int value = rand_r(extract_random_data(vm->state, instance));

	if (arg_count == 1) {
		if (IS_ARRAY(args[0])) {
			LitArray* array = AS_ARRAY(args[0]);

			if (array->values.count == 0) {
				return NULL_VALUE;
			}

			return array->values.values[value % array->values.count];
		} else if (IS_MAP(args[0])) {
			LitMap* map = AS_MAP(args[0]);
			uint length = map->values.count;
			uint capacity = map->values.capacity;

			if (length == 0) {
				return NULL_VALUE;
			}

			uint target = value % length;
			uint index = 0;

			for (uint i = 0; i < capacity; i++) {
				if (map->values.entries[i].key != NULL) {
					if (index == target) {
						return map->values.entries[i].value;
					}

					index++;
				}
			}

			return NULL_VALUE;
		} else {
			lit_runtime_error(vm, "Expected map or array as the argument");
			return NULL_VALUE;
		}
	} else {
		return args[value % arg_count];
	}
}

void lit_open_math_library(LitState* state) {
	LIT_BEGIN_CLASS("Math")
		LIT_SET_STATIC_FIELD("Pi", NUMBER_VALUE(M_PI))
		LIT_SET_STATIC_FIELD("Tau", NUMBER_VALUE(M_PI * 2))

		LIT_BIND_STATIC_METHOD("abs", math_abs)
		LIT_BIND_STATIC_METHOD("sin", math_sin)
		LIT_BIND_STATIC_METHOD("cos", math_cos)
		LIT_BIND_STATIC_METHOD("tan", math_tan)
		LIT_BIND_STATIC_METHOD("asin", math_asin)
		LIT_BIND_STATIC_METHOD("acos", math_acos)
		LIT_BIND_STATIC_METHOD("atan", math_atan)
		LIT_BIND_STATIC_METHOD("atan2", math_atan2)
		LIT_BIND_STATIC_METHOD("floor", math_floor)
		LIT_BIND_STATIC_METHOD("ceil", math_ceil)
		LIT_BIND_STATIC_METHOD("round", math_round)
		LIT_BIND_STATIC_METHOD("min", math_min)
		LIT_BIND_STATIC_METHOD("max", math_max)
		LIT_BIND_STATIC_METHOD("mid", math_mid)
		LIT_BIND_STATIC_METHOD("toRadians", math_toRadians)
		LIT_BIND_STATIC_METHOD("toDegrees", math_toDegrees)
		LIT_BIND_STATIC_METHOD("sqrt", math_sqrt)
		LIT_BIND_STATIC_METHOD("log", math_log)
		LIT_BIND_STATIC_METHOD("exp", math_exp)
	LIT_END_CLASS()

	srand(time(NULL));
	static_random_data = time(NULL);

	LIT_BEGIN_CLASS("Random")
		LIT_BIND_CONSTRUCTOR(random_constructor)

		LIT_BIND_METHOD("setSeed", random_setSeed)
		LIT_BIND_METHOD("int", random_int)
		LIT_BIND_METHOD("float", random_float)
		LIT_BIND_METHOD("chance", random_chance)
		LIT_BIND_METHOD("pick", random_pick)

		LIT_BIND_STATIC_METHOD("setSeed", random_setSeed)
		LIT_BIND_STATIC_METHOD("int", random_int)
		LIT_BIND_STATIC_METHOD("float", random_float)
		LIT_BIND_STATIC_METHOD("bool", random_bool)
		LIT_BIND_STATIC_METHOD("chance", random_chance)
		LIT_BIND_STATIC_METHOD("pick", random_pick)
	LIT_END_CLASS()
}