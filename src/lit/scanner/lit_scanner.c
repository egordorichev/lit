#include <lit/scanner/lit_scanner.h>
#include <string.h>

void lit_setup_scanner(LitScanner* scanner, const char* source) {
	scanner->line = 1;
	scanner->start = source;
	scanner->current = source;
}

static bool is_at_end(LitScanner* scanner) {
	return *scanner->current == '\0';
}

static LitToken make_token(LitScanner* scanner, LitTokenType type) {
	LitToken token;

	token.type = type;
	token.start = scanner->start;
	token.length = (uint) (scanner->current - scanner->start);
	token.line = scanner->line;

	return token;
}

static LitToken make_error_token(LitScanner* scanner, const char* message) {
	LitToken token;

	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int) strlen(message);
	token.line = scanner->line;

	return token;
}

LitToken lit_scan_token(LitScanner* scanner) {
	scanner->start = scanner->current;

	if (is_at_end(scanner)) {
		return make_token(scanner, TOKEN_EOF);
	}

	return make_error_token(scanner, "Unexpected character");
}