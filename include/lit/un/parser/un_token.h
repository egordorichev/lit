#ifndef UN_TOKEN_H
#define UN_TOKEN_H

typedef enum {
	UTOKEN_IDENTIFIER,
	UTOKEN_NUMBER,
	UTOKEN_STRING,

	UTOKEN_COMMA,
	UTOKEN_COLON
} UnTokenType;

typedef struct {
	const char* start;

	UnTokenType type;
	uint length;
	uint line;
} UnToken;

#endif