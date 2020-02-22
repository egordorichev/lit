#include <lit/scanner/lit_scanner.h>

#include <string.h>
#include <stdio.h>

void lit_setup_scanner(LitScanner* scanner, const char* file_name, const char* source) {
	scanner->line = 1;
	scanner->start = source;
	scanner->current = source;
	scanner->file_name = file_name;
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
				}

				return false;
			}

			default: return false;
		}
	}
}

static LitToken parse_string(LitScanner* scanner) {
	while (peek(scanner) != '"' && !is_at_end(scanner)) {
		if (peek(scanner) == '\n') {
			scanner->line++;
		}

		advance(scanner);
	}

	if (is_at_end(scanner)) {
		return make_error_token(scanner, "Unterminated string");
	}

	// Closing "
	advance(scanner);
	return make_token(scanner, TOKEN_STRING);
}

static bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

static bool is_alpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static LitToken parse_number(LitScanner* scanner) {
	while (is_digit(peek(scanner))) {
		advance(scanner);
	}

	// Look for a fractional part.
	if (peek(scanner) == '.' && is_digit(peek_next(scanner))) {
		// Consume the '.'
		advance(scanner);

		while (is_digit(peek(scanner))) {
			advance(scanner);
		}
	}

	return make_token(scanner, TOKEN_NUMBER);
}

static LitTokenType check_keyword(LitScanner* scanner, int start, int length, const char* rest, LitTokenType type) {
	if (scanner->current - scanner->start == start + length && memcmp(scanner->start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static LitTokenType parse_identifier_type(LitScanner* scanner) {
	switch (scanner->start[0]) {
		case 'b': return check_keyword(scanner, 1, 4, "reak", TOKEN_BREAK);

		case 'c': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'l': return check_keyword(scanner, 2, 3, "ass", TOKEN_CLASS);
					case 'o': return check_keyword(scanner, 2, 6, "ntinue", TOKEN_CONTINUE);
				}
			}

			break;
		}

		case 'e': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'l': return check_keyword(scanner, 2, 2, "se", TOKEN_ELSE);
					case 'x': return check_keyword(scanner, 2, 4, "port", TOKEN_EXPORT);
				}
			}

			break;
		}


		case 'f': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'a': return check_keyword(scanner, 2, 3, "lse", TOKEN_FALSE);
					case 'o': return check_keyword(scanner, 2, 1, "r", TOKEN_FOR);
					case 'u': return check_keyword(scanner, 2, 6, "nction", TOKEN_FUNCTION);
				}
			}

			break;
		}

		case 'i': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 's': return TOKEN_IS;
					case 'f': return TOKEN_IF;
				}
			}

			break;
		}

		case 'n': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'u': return check_keyword(scanner, 2, 2, "ll", TOKEN_NULL);
					case 'e': return check_keyword(scanner, 2, 1, "w", TOKEN_NEW);
				}
			}

			break;
		}

		case 'r': {
			if (scanner->current - scanner->start > 2) {
				switch (scanner->start[2]) {
					case 't': return check_keyword(scanner, 3, 3, "urn", TOKEN_RETURN);
					case 'q': return check_keyword(scanner, 3, 4, "uire", TOKEN_REQUIRE);
				}
			}

			break;
		}

		case 'o': return check_keyword(scanner, 1, 7, "perator", TOKEN_OPERATOR);

		case 's': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'u': return check_keyword(scanner, 2, 3, "per", TOKEN_SUPER);
					case 't': return check_keyword(scanner, 2, 4, "atic", TOKEN_STATIC);
				}
			}

			break;
		}

		case 't': {
			if (scanner->current - scanner->start > 1) {
				switch (scanner->start[1]) {
					case 'h': return check_keyword(scanner, 2, 2, "is", TOKEN_THIS);
					case 'r': return check_keyword(scanner, 2, 2, "ue", TOKEN_TRUE);
				}
			}

			break;
		}

		case 'v': return check_keyword(scanner, 1, 2, "ar", TOKEN_VAR);
		case 'w': return check_keyword(scanner, 1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static LitToken parse_identifier(LitScanner* scanner) {
	while (is_alpha(peek(scanner)) || is_digit(peek(scanner))) {
		advance(scanner);
	}

	return make_token(scanner, parse_identifier_type(scanner));
}

LitToken lit_scan_token(LitScanner* scanner) {
	if (skip_whitespace(scanner)) {
		LitToken token = make_token(scanner, TOKEN_NEW_LINE);
		scanner->line++;

		return token;
	}

	scanner->start = scanner->current;

	if (is_at_end(scanner)) {
		return make_token(scanner, TOKEN_EOF);
	}

	char c = advance(scanner);

	if (is_digit(c)) {
		return parse_number(scanner);
	}

	if (is_alpha(c)) {
		return parse_identifier(scanner);
	}

	switch (c) {
		case '(': return make_token(scanner, TOKEN_LEFT_PAREN);
		case ')': return make_token(scanner, TOKEN_RIGHT_PAREN);
		case '{': return make_token(scanner, TOKEN_LEFT_BRACE);
		case '}': return make_token(scanner, TOKEN_RIGHT_BRACE);
		case '[': return make_token(scanner, TOKEN_LEFT_BRACKET);
		case ']': return make_token(scanner, TOKEN_RIGHT_BRACKET);
		case ';': return make_token(scanner, TOKEN_SEMICOLON);
		case ',': return make_token(scanner, TOKEN_COMMA);
		case '.': return make_token(scanner, TOKEN_DOT);
		case ':': return make_token(scanner, TOKEN_COLON);

		case '+': return match_tokens(scanner, '=', '+', TOKEN_PLUS_EQUAL, TOKEN_PLUS_PLUS, TOKEN_PLUS);
		case '-': return match_tokens(scanner, '=', '-', TOKEN_MINUS_EQUAL, TOKEN_MINUS_MINUS, TOKEN_MINUS);
		case '/': return match_token(scanner, '=', TOKEN_SLASH_EQUAL, TOKEN_SLASH);
		case '!': return match_token(scanner, '=', TOKEN_BANG_EQUAL, TOKEN_BANG);
		case '>': return match_token(scanner, '=', TOKEN_GREATER_EQUAL, TOKEN_GREATER);
		case '<': return match_token(scanner, '=', TOKEN_LESS_EQUAL, TOKEN_LESS);
		case '?': return match_token(scanner, '?', TOKEN_QUESTION_QUESTION, TOKEN_QUESTION);
		case '%': return match_token(scanner, '=', TOKEN_PERCENT_EQUAL, TOKEN_PERCENT);

		case '*': return match_tokens(scanner, '=', '*', TOKEN_STAR_EQUAL, TOKEN_STAR_STAR, TOKEN_STAR);
		case '=': return match_tokens(scanner, '=', '>', TOKEN_EQUAL_EQUAL, TOKEN_ARROW, TOKEN_EQUAL);
		case '|': return match_tokens(scanner, '=', '|', TOKEN_BAR_EQUAL, TOKEN_BAR_BAR, TOKEN_BAR);
		case '&': return match_tokens(scanner, '=', '&', TOKEN_AMPERSAND_EQUAL, TOKEN_AMPERSAND_AMPERSAND, TOKEN_AMPERSAND);

		case '"': return parse_string(scanner);
	}

	return make_error_token(scanner, "Unexpected character");
}