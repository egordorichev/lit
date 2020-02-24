#include <lit/std/lit_math.h>
#include <lit/api/lit_api.h>

#include <math.h>
#include <stdlib.h>

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

void lit_open_math_library(LitState* state) {
	LIT_BEGIN_CLASS("Math")
		LIT_BIND_STATIC_FIELD("Pi", NUMBER_VALUE(M_PI))
		LIT_BIND_STATIC_FIELD("Tau", NUMBER_VALUE(M_PI * 2))

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
}