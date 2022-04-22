#include "scanner/lit_scanner.h"
#include "parser/lit_error.h"
#include "util/lit_utf.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

void lit_init_scanner(LitState* state, LitScanner* scanner, const char* file_name, const char* source) {
	scanner->line = 1;
	scanner->start = source;
	scanner->current = source;
	scanner->file_name = file_name;
	scanner->state = state;
	scanner->num_braces = 0;
	scanner->had_error = false;
}

static LitToken make_token(LitScanner* scanner, LitTokenType type) {
	LitToken token;

	token.type = type;
	token.start = scanner->start;
	token.length = (uint) (scanner->current - scanner->start);
	token.line = scanner->line;

	return token;
}

static LitToken make_error_token(LitScanner* scanner, LitError error, ...) {
	scanner->had_error = true;

	va_list args;
	va_start(args, error);
	LitString* result = lit_vformat_error(scanner->state, scanner->line, error, args);
	va_end(args);

	LitToken token;

	token.type = LTOKEN_ERROR;
	token.start = result->chars;
	token.length = result->length;
	token.line = scanner->line;

	return token;
}

static bool is_at_end(LitScanner* scanner) {
	return *scanner->current == '\0';
}

static char advance(LitScanner* scanner) {
	scanner->current++;
	return scanner->current[-1];
}

static bool match(LitScanner* scanner, char expected) {
	if (is_at_end(scanner)) {
		return false;
	}

	if (*scanner->current != expected) {
		return false;
	}

	scanner->current++;
	return true;
}

static LitToken match_token(LitScanner* scanner, char c, LitTokenType a, LitTokenType b) {
	return make_token(scanner, match(scanner, c) ? a : b);
}

static LitToken match_tokens(LitScanner* scanner, char cr, char cb, LitTokenType a, LitTokenType b, LitTokenType c) {
	return make_token(scanner, match(scanner, cr) ? a : (match(scanner, cb) ? b : c));
}

static char peek(LitScanner* scanner) {
	return *scanner->current;
}

static char peek_next(LitScanner* scanner) {
	if (is_at_end(scanner)) {
		return '\0';
	}

	return scanner->current[1];
}

static bool skip_whitespace(LitScanner* scanner) {
	while (true) {
		char c = peek(scanner);

		switch (c) {
			case ' ':
			case '\r':
			case '\t': {
				advance(scanner);
				break;
			}

			case '\n': {
				scanner->start = scanner->current;
				advance(scanner);

				return true;
			}

			case '/': {
				if (peek_next(scanner) == '/') {
					while (peek(scanner) != '\n' && !is_at_end(scanner)) {
						advance(scanner);
					}

					return skip_whitespace(scanner);
				} else if (peek_next(scanner) == '*') {
					advance(scanner);
					advance(scanner);

					while ((peek(scanner) != '*' || peek_next(scanner) != '/') && !is_at_end(scanner)) {
						if (peek(scanner) == '\n') {
							scanner->line++;
						}

						advance(scanner);
					}

					advance(scanner);
					advance(scanner);

					return skip_whitespace(scanner);
				}

				return false;
			}

			default: return false;
		}
	}
}

static LitToken parse_string(LitScanner* scanner, bool interpolation) {
	LitState* state = scanner->state;
	LitTokenType string_type = LTOKEN_STRING;

	LitBytes bytes;
	lit_init_bytes(&bytes);

	while (true) {
		char c = advance(scanner);

		if (c == '\"') {
			break;
		} else if (interpolation && c == '{') {
			if (scanner->num_braces >= LIT_INTERPOLATION_NESTING_MAX) {
				return make_error_token(scanner, ERROR_INTERPOLATION_NESTING_TOO_DEEP, LIT_INTERPOLATION_NESTING_MAX);
			}

			string_type = LTOKEN_INTERPOLATION;
			scanner->braces[scanner->num_braces++] = 1;

			break;
		}

		switch (c) {
			case '\0': return make_error_token(scanner, ERROR_UNTERMINATED_STRING);

			case '\n': {
				scanner->line++;
				lit_bytes_write(state, &bytes, c);

				break;
			}

			case '\\': {
				switch (advance(scanner)) {
					case '\"': lit_bytes_write(state, &bytes, '\"'); break;
					case '\\': lit_bytes_write(state, &bytes, '\\'); break;
					case '0': lit_bytes_write(state, &bytes, '\0'); break;
					case '{': lit_bytes_write(state, &bytes, '{'); break;
					case 'a': lit_bytes_write(state, &bytes, '\a'); break;
					case 'b': lit_bytes_write(state, &bytes, '\b'); break;
					case 'f': lit_bytes_write(state, &bytes, '\f'); break;
					case 'n': lit_bytes_write(state, &bytes, '\n'); break;
					case 'r': lit_bytes_write(state, &bytes, '\r'); break;
					case 't': lit_bytes_write(state, &bytes, '\t'); break;
					case 'v': lit_bytes_write(state, &bytes, '\v'); break;

					default: {
						return make_error_token(scanner, ERROR_INVALID_ESCAPE_CHAR, scanner->current[-1]);
					}
				}

				break;
			}

			default: {
				lit_bytes_write(state, &bytes, c);
				break;
			}
		}
	}

	LitToken token = make_token(scanner, string_type);
	token.value = OBJECT_VALUE(lit_copy_string(state, (const char*) bytes.values, bytes.count));
	lit_free_bytes(state, &bytes);

	return token;
}

static int parse_hex_digit(LitScanner* scanner) {
	char c = advance(scanner);

	if (c >= '0' && c <= '9') {
		return c - '0';
	}

	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}

	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	scanner->current--;
	return -1;
}

static int parse_binary_digit(LitScanner* scanner) {
	char c = advance(scanner);

	if (c >= '0' && c <= '1') {
		return c - '0';
	}

	scanner->current--;
	return -1;
}

static LitToken make_number_token(LitScanner* scanner, bool is_hex, bool is_binary) {
	errno = 0;
	LitValue value;

	if (is_hex) {
		value = NUMBER_VALUE((double) strtoll(scanner->start, NULL, 16));
	} else if (is_binary) {
		value = NUMBER_VALUE((int) strtoll(scanner->start + 2, NULL, 2));
	} else {
		value = NUMBER_VALUE(strtod(scanner->start, NULL));
	}

	if (errno == ERANGE) {
		errno = 0;
		return make_error_token(scanner, ERROR_NUMBER_IS_TOO_BIG);
	}

	LitToken token = make_token(scanner, LTOKEN_NUMBER);
	token.value = value;
	return token;
}

static LitToken parse_number(LitScanner* scanner) {
	if (match(scanner, 'x')) {
		while (parse_hex_digit(scanner) != -1) {
			continue;
		}

		return make_number_token(scanner, true, false);
	}

	if (match(scanner, 'b')) {
		while (parse_binary_digit(scanner) != -1) {
			continue;
		}

		return make_number_token(scanner, false, true);
	}

	while (lit_is_digit(peek(scanner))) {
		advance(scanner);
	}

	// Look for a fractional part.
	if (peek(scanner) == '.' && lit_is_digit(peek_next(scanner))) {
		// Consume the '.'
		advance(scanner);

		while (lit_is_digit(peek(scanner))) {
			advance(scanner);
		}
	}

	return make_number_token(scanner, false, false);
}

static LitTokenType check_keyword(LitScanner* scanner, int start, int length, const char* rest, LitTokenType type) {
	if (scanner->current - scanner->start == start + length && memcmp(scanner->start + start, rest, length) == 0) {
		return type;
	}

	return LTOKEN_IDENTIFIER;
}

static LitTokenType parse_identifier_type(LitScanner* scanner) {
	switch (scanner->start[0]) {
		case 'b': return check_keyword(scanner, 1, 4, "reak", LTOKEN_BREAK);

		case 'c': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'l': return check_keyword(scanner, 2, 3, "ass", LTOKEN_CLASS);

					case 'o': {
						if (scanner->current - scanner->start > 3) {
							switch (scanner->start[3]) {
								case 's': return check_keyword(scanner, 2, 3, "nst", LTOKEN_CONST);
								case 't': return check_keyword(scanner, 2, 6, "ntinue", LTOKEN_CONTINUE);
							}
						}
					}
				}
			}

			break;
		}

		case 'e': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'l': return check_keyword(scanner, 2, 2, "se", LTOKEN_ELSE);
					case 'x': return check_keyword(scanner, 2, 4, "port", LTOKEN_EXPORT);
				}
			}

			break;
		}


		case 'f': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'a': return check_keyword(scanner, 2, 3, "lse", LTOKEN_FALSE);
					case 'o': return check_keyword(scanner, 2, 1, "r", LTOKEN_FOR);
					case 'u': return check_keyword(scanner, 2, 6, "nction", LTOKEN_FUNCTION);
				}
			}

			break;
		}

		case 'i': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 's': return check_keyword(scanner, 2, 0, "", LTOKEN_IS);
					case 'f': return check_keyword(scanner, 2, 0, "", LTOKEN_IF);
					case 'n': return check_keyword(scanner, 2, 0, "", LTOKEN_IN);
				}
			}

			break;
		}

		case 'n': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'u': return check_keyword(scanner, 2, 2, "ll", LTOKEN_NULL);
					case 'e': return check_keyword(scanner, 2, 1, "w", LTOKEN_NEW);
				}
			}

			break;
		}

		case 'r': {
			if (scanner->current - scanner->start > 2) {
				switch (scanner->start[2]) {
					case 'f': return check_keyword(scanner, 3, 0, "", LTOKEN_REF);
					case 't': return check_keyword(scanner, 3, 3, "urn", LTOKEN_RETURN);
				}
			}

			break;
		}

		case 'o': return check_keyword(scanner, 1, 7, "perator", LTOKEN_OPERATOR);

		case 's': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'u': return check_keyword(scanner, 2, 3, "per", LTOKEN_SUPER);
					case 't': return check_keyword(scanner, 2, 4, "atic", LTOKEN_STATIC);
				}
			}

			break;
		}

		case 't': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'h': return check_keyword(scanner, 2, 2, "is", LTOKEN_THIS);
					case 'r': return check_keyword(scanner, 2, 2, "ue", LTOKEN_TRUE);
				}
			}

			break;
		}

		case 'v': return check_keyword(scanner, 1, 2, "ar", LTOKEN_VAR);
		case 'w': return check_keyword(scanner, 1, 4, "hile", LTOKEN_WHILE);
	}

	return LTOKEN_IDENTIFIER;
}

static LitToken parse_identifier(LitScanner* scanner) {
	while (lit_is_alpha(peek(scanner)) || lit_is_digit(peek(scanner))) {
		advance(scanner);
	}

	return make_token(scanner, parse_identifier_type(scanner));
}

LitToken lit_scan_token(LitScanner* scanner) {
	if (skip_whitespace(scanner)) {
		LitToken token = make_token(scanner, LTOKEN_NEW_LINE);
		scanner->line++;

		return token;
	}

	scanner->start = scanner->current;

	if (is_at_end(scanner)) {
		return make_token(scanner, LTOKEN_EOF);
	}

	char c = advance(scanner);

	if (lit_is_digit(c)) {
		return parse_number(scanner);
	}

	if (lit_is_alpha(c)) {
		return parse_identifier(scanner);
	}

	switch (c) {
		case '(': return make_token(scanner, LTOKEN_LEFT_PAREN);
		case ')': return make_token(scanner, LTOKEN_RIGHT_PAREN);

		case '{': {
			if (scanner->num_braces > 0) {
				scanner->braces[scanner->num_braces - 1]++;
			}

			return make_token(scanner, LTOKEN_LEFT_BRACE);
		}

		case '}': {
			if (scanner->num_braces > 0 && --scanner->braces[scanner->num_braces - 1] == 0) {
				scanner->num_braces--;
				return parse_string(scanner, true);
			}

			return make_token(scanner, LTOKEN_RIGHT_BRACE);
		}

		case '[': return make_token(scanner, LTOKEN_LEFT_BRACKET);
		case ']': return make_token(scanner, LTOKEN_RIGHT_BRACKET);
		case ';': return make_token(scanner, LTOKEN_SEMICOLON);
		case ',': return make_token(scanner, LTOKEN_COMMA);
		case ':': return make_token(scanner, LTOKEN_COLON);
		case '~': return make_token(scanner, LTOKEN_TILDE);

		case '+': return match_tokens(scanner, '=', '+', LTOKEN_PLUS_EQUAL, LTOKEN_PLUS_PLUS, LTOKEN_PLUS);
		case '-': return match(scanner, '>') ? make_token(scanner, LTOKEN_SMALL_ARROW) : match_tokens(scanner, '=', '-', LTOKEN_MINUS_EQUAL, LTOKEN_MINUS_MINUS, LTOKEN_MINUS);
		case '/': return match_token(scanner, '=', LTOKEN_SLASH_EQUAL, LTOKEN_SLASH);
		case '#': return match_token(scanner, '=', LTOKEN_SHARP_EQUAL, LTOKEN_SHARP);
		case '!': return match_token(scanner, '=', LTOKEN_BANG_EQUAL, LTOKEN_BANG);
		case '?': return match_token(scanner, '?', LTOKEN_QUESTION_QUESTION, LTOKEN_QUESTION);
		case '%': return match_token(scanner, '=', LTOKEN_PERCENT_EQUAL, LTOKEN_PERCENT);
		case '^': return match_token(scanner, '=', LTOKEN_CARET_EQUAL, LTOKEN_CARET);

		case '>': return match_tokens(scanner, '=', '>', LTOKEN_GREATER_EQUAL, LTOKEN_GREATER_GREATER, LTOKEN_GREATER);
		case '<': return match_tokens(scanner, '=', '<', LTOKEN_LESS_EQUAL, LTOKEN_LESS_LESS, LTOKEN_LESS);
		case '*': return match_tokens(scanner, '=', '*', LTOKEN_STAR_EQUAL, LTOKEN_STAR_STAR, LTOKEN_STAR);
		case '=': return match_tokens(scanner, '=', '>', LTOKEN_EQUAL_EQUAL, LTOKEN_ARROW, LTOKEN_EQUAL);
		case '|': return match_tokens(scanner, '=', '|', LTOKEN_BAR_EQUAL, LTOKEN_BAR_BAR, LTOKEN_BAR);
		case '&': return match_tokens(scanner, '=', '&', LTOKEN_AMPERSAND_EQUAL, LTOKEN_AMPERSAND_AMPERSAND, LTOKEN_AMPERSAND);

		case '.': {
			if (!match(scanner, '.')) {
				return make_token(scanner, LTOKEN_DOT);
			}

			return match_token(scanner, '.', LTOKEN_DOT_DOT_DOT, LTOKEN_DOT_DOT);
		}

		case '$': {
			if (!match(scanner, '\"')) {
				return make_error_token(scanner, ERROR_CHAR_EXPECTATION_UNMET, '\"', '$', peek(scanner));
			}

			return parse_string(scanner, true);
		}

		case '"': return parse_string(scanner, false);
	}

	return make_error_token(scanner, ERROR_UNEXPECTED_CHAR, c);
}