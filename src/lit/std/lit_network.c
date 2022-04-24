#include "std/lit_network.h"
#include "api/lit_api.h"
#include "std/lit_json.h"
#include "state/lit_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

typedef struct LitNetworkRequest {
	int socket;
	uint bytes;

	char* message;
	uint message_length;
	uint total_length;

	bool inited_read;
} LitNetworkRequest;

void cleanup_request(LitState* state, LitUserdata* data, bool mark) {
	if (mark) {
		return;
	}

	LitNetworkRequest* request_data = ((LitNetworkRequest*) data->data);

	close(request_data->socket);
	request_data->message = lit_reallocate(state, request_data->message, request_data->total_length, 0);
}

typedef struct LitUrl {
	char* protocol;
	char* host;
	int port;
	char* path;
	char* query_string;
} LitUrl;

void free_parsed_url(LitState* state, LitUrl* url_parsed) {
	if (url_parsed->protocol) {
		lit_reallocate(state, url_parsed->protocol, strlen(url_parsed->protocol) + 1, 0);
	}

	if (url_parsed->host) {
		lit_reallocate(state, url_parsed->host, strlen(url_parsed->host) + 1, 0);
	}

	if (url_parsed->path) {
		lit_reallocate(state, url_parsed->path, strlen(url_parsed->path) + 1, 0);
	}

	if (url_parsed->query_string) {
		lit_reallocate(state, url_parsed->query_string, strlen(url_parsed->query_string) + 1, 0);
	}
}

int parse_url(LitState* state, char* url, LitUrl *parsed_url) {
	uint local_url_size = strlen(url) + 1;

	char* local_url = (char*) lit_reallocate(state, NULL, 0, local_url_size);
	char* token;
	char* token_host;
	char* host_port;
	char* token_ptr;
	char* host_token_ptr;

	char* path = NULL;

	strcpy(local_url, url);

	token = strtok_r(local_url, ":", &token_ptr);
	parsed_url->protocol = (char*) lit_reallocate(state, NULL, 0, strlen(token) + 1);
	strcpy(parsed_url->protocol, token);

	token = strtok_r(NULL, "/", &token_ptr);
	uint host_port_size;

	if (token) {
		host_port_size = strlen(token) + 1;
		host_port = (char*) lit_reallocate(state, NULL, 0, host_port_size);

		strcpy(host_port, token);
	} else {
		host_port_size = 1;
		host_port = (char*) lit_reallocate(state, NULL, 0, host_port_size);

		strcpy(host_port, "");
	}

	token_host = strtok_r(host_port, ":", &host_token_ptr);

	if (token_host) {
		parsed_url->host = (char*) lit_reallocate(state, NULL, 0, strlen(token_host) + 1);
		strcpy(parsed_url->host, token_host);
	} else {
		parsed_url->host = NULL;
	}

	token_host = strtok_r(NULL, ":", &host_token_ptr);

	if (token_host) {
		parsed_url->port = atoi(token_host);;
	}

	token_host = strtok_r(NULL, ":", &host_token_ptr);

	token = strtok_r(NULL, "?", &token_ptr);
	parsed_url->path = NULL;

	if (token) {
		uint path_size = strlen(token) + 2;

		path = (char*) lit_reallocate(state, NULL, 0, path_size);
		strcpy(path, "/");
		strcat(path, token);

		uint path_length = strlen(path);

		parsed_url->path = (char*) lit_reallocate(state, NULL, 0, path_length + 1);
		strncpy(parsed_url->path, path, path_length + 1);

		lit_reallocate(state, path, path_size, 0);
	} else {
		parsed_url->path = (char*) lit_reallocate(state, NULL, 0, 2);
		strcpy(parsed_url->path, "/");
	}

	token = strtok_r(NULL, "?", &token_ptr);

	if (token) {
		parsed_url->query_string = (char*) lit_reallocate(state, NULL, 0, strlen(token) + 1);
		strncpy(parsed_url->query_string, token, strlen(token));
	} else {
		parsed_url->query_string = NULL;
	}

	token = strtok_r(NULL, "?", &token_ptr);

	lit_reallocate(state, local_url, local_url_size, 0);
	lit_reallocate(state, host_port, host_port_size, 0);

	if (token != NULL) {
		lit_runtime_error_exiting(state->vm, "Url parsing fail");
	}

	return 0;
}

LIT_METHOD(networkRequest_contructor) {
	LitNetworkRequest* data = LIT_INSERT_DATA(LitNetworkRequest, cleanup_request);
	LitState* state = vm->state;

	// To make sure that errors don't cause memory corruption
	data->total_length = 0;
	data->socket = -1;

	const char* url = LIT_CHECK_STRING(0);
	const char* method = LIT_CHECK_STRING(1);

	LitString* body = NULL;
	LitTable* headers = NULL;
	bool allocated_headers = false;

	if (arg_count > 3 && !IS_NULL(args[3])) {
		if (!IS_INSTANCE(args[3])) {
			lit_runtime_error_exiting(vm, "Headers (argument #3) must be an object");
		}

		headers = &AS_INSTANCE(args[3])->fields;
	} else {
		allocated_headers = true;
		headers = lit_reallocate(state, NULL, 0, sizeof(LitTable));

		lit_init_table(headers);
	}

	#define FREE_HEADERS() \
		lit_free_table(state, headers); \
		if (allocated_headers) { \
      headers = lit_reallocate(state, headers, sizeof(LitTable), 0); \
		}

	bool get = false;

	if (strcmp(method, "get") == 0) {
		get = true;
	} else if (strcmp(method, "post") != 0) {
		FREE_HEADERS()
		lit_runtime_error_exiting(vm, "Method (argument #2) must be either 'post' or 'get'");
	}

	if (arg_count > 2 && !IS_NULL(args[2])) {
		LitValue body_arg = args[2];

		if (IS_MAP(body_arg) || (!get && IS_ARRAY(body_arg)) || IS_INSTANCE(body_arg)) {
			if (get) {
				LitTable* values;

				if (IS_MAP(body_arg)) {
					values = &AS_MAP(body_arg)->values;
				} else {
					values = &AS_INSTANCE(body_arg)->fields;
				}

				uint value_amount = values->count;

				LitString* values_converted[value_amount];
				LitString* keys[value_amount];

				uint string_length = 0;
				uint i = 0;
				uint index = 0;

				do {
					LitTableEntry* entry = &values->entries[index++];

					if (entry->key != NULL) {
						LitString* value = lit_to_string(state, entry->value, 0);

						lit_push_root(state, (LitObject*) value);

						values_converted[i] = value;
						keys[i] = entry->key;
						string_length += entry->key->length + value->length + 2;

						i++;
					}
				} while (i < value_amount);

				char buffer[string_length + 1];
				uint buffer_index = 0;

				for (i = 0; i < value_amount; i++) {
					LitString *key = keys[i];
					LitString *value = values_converted[i];

					buffer[buffer_index++] = (i == 0 ? '?' : '&');
					memcpy(&buffer[buffer_index], key->chars, key->length);
					buffer_index += key->length;

					buffer[buffer_index++] = '=';

					memcpy(&buffer[buffer_index], value->chars, value->length);
					buffer_index += value->length;

					lit_pop_root(state);
				}

				buffer[string_length] = '\0';
				body = lit_copy_string(vm->state, buffer, string_length);
			} else {
				body = lit_json_to_string(vm, body_arg, 0);
				lit_table_set(state, headers, CONST_STRING(state, "Content-Type"), OBJECT_CONST_STRING(state, "application/json"));
			}
		} else {
			body = lit_to_string(state, body_arg, 0);
		}
	}

	if (!get && body != NULL) {
		lit_table_set(state, headers, CONST_STRING(state, "Content-Length"), lit_string_format(state, "#", (double) body->length));
	}

	LitUrl url_data;

	url_data.host = NULL;
	url_data.path = NULL;
	url_data.query_string = NULL;
	url_data.protocol = NULL;
	url_data.port = 80;

	parse_url(state, (char*) url, &url_data);

	data->bytes = 0;
	data->inited_read = false;

	const char* method_string = get ? "GET" : "POST";
	const char* protocol_string = strcmp(url_data.protocol, "https") == 0 ? "HTTPS" : "HTTP";

	uint request_line_length = strlen(method_string) + strlen(url_data.path) + strlen(protocol_string) + (get ? body->length : 0) + 9;

	data->message_length = request_line_length + 2 + (!get && body != NULL ? 4 + body->length : 0);
	data->total_length = data->message_length - 1;

	LitString* header_values[headers->capacity];
	uint value_index = 0;

	for (int i = 0; i <= headers->capacity; i++) {
		LitTableEntry* entry = &headers->entries[i];

		if (entry->key != NULL) {
			LitString* value_string = lit_to_string(state, entry->value, 0);

			header_values[value_index++] = value_string;
			data->message_length += entry->key->length + value_string->length + 4;
		}
	}

	data->message = lit_reallocate(state, NULL, 0, data->message_length);
	uint buffer_offset = request_line_length - 1;

	sprintf(data->message, "%s %s%s %s/1.0\r\n", method_string, url_data.path, get && body != NULL ? body->chars : "", protocol_string);
	value_index = 0;

	for (int i = 0; i <= headers->capacity; i++) {
		LitTableEntry* entry = &headers->entries[i];

		if (entry->key != NULL) {
			LitString* value_string = header_values[value_index++];

			sprintf(data->message + buffer_offset, "%s: %s\r\n", entry->key->chars, value_string->chars);
			buffer_offset += entry->key->length + value_string->length + 4;
		}
	}

	if (!get && body != NULL) {
		sprintf(data->message + buffer_offset, "\r\n%s\r\n", body->chars);
		buffer_offset += body->length + 4;
	}

	memcpy(data->message + buffer_offset, "\r\n", 2);
	data->socket = socket(AF_INET, SOCK_STREAM, 0);

	if (data->socket < 0) {
		free_parsed_url(state, &url_data);
		FREE_HEADERS()
		lit_runtime_error_exiting(vm, "Error opening socket");
	}

	struct hostent* server = gethostbyname(url_data.host);

	if (server == NULL) {
		free_parsed_url(state, &url_data);
		FREE_HEADERS()
		lit_runtime_error_exiting(vm, "Error resolving the host");
	}

	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));

	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(url_data.port);

	memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length);

	if (connect(data->socket, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
		free_parsed_url(state, &url_data);
		FREE_HEADERS()
		lit_runtime_error_exiting(vm, "Connection error");
	}

	data->total_length = data->message_length;
	free_parsed_url(state, &url_data);

	FREE_HEADERS()
	#undef FREE_HEADERS

	return instance;
}

LIT_METHOD(networkRequest_write) {
	LitNetworkRequest* data = LIT_EXTRACT_DATA(LitNetworkRequest);
	int bytes = write(data->socket,data->message + data->bytes, data->total_length - data->bytes);

	if (bytes < 0) {
		lit_runtime_error_exiting(vm, "Error writing message to the socket");
	}

	if (bytes != 0) {
		data->bytes += bytes;

		if ((data->bytes < data->total_length)) {
			return FALSE_VALUE;
		}
	}

	return TRUE_VALUE;
}

LIT_METHOD(networkRequest_read) {
	LitNetworkRequest* data = LIT_EXTRACT_DATA(LitNetworkRequest);
	LitState* state = vm->state;

	if (!data->inited_read) {
		int length = 256;

		data->message = lit_reallocate(state, data->message, data->message_length, length);
		data->total_length = length;
		data->bytes = 0;
		data->inited_read = true;

		memset(data->message, 0, length);
	}

	int bytes = read(data->socket, data->message + data->bytes, data->total_length - data->bytes);

	if (bytes < 0) {
		lit_runtime_error_exiting(vm, "Error reading response");
	}

	if (bytes != 0) {
		data->bytes += bytes;

		if (data->bytes <= data->total_length) {
			uint length = lit_closest_power_of_two(data->bytes * 2);

			if (length != data->total_length) {
				data->message = lit_reallocate(state, data->message, data->total_length, length);
				data->total_length = length;
			}

			return NULL_VALUE;
		}
	}

	close(data->socket);

	LitInstance* response = lit_create_instance(state, state->object_class);
	LitTable* response_table = &response->fields;

	LitInstance* headers = lit_create_instance(state, state->object_class);
	LitTable* headers_table = &headers->fields;

	lit_table_set(state, response_table, CONST_STRING(state, "headers"), OBJECT_VALUE(headers));

	char* token = NULL;
	char* body_token = NULL;

	bool parsing_body = false;
	bool parsed_status = false;

	token = strtok(data->message, "\r\n");

	while (token) {
		if (!parsed_status) {
			parsed_status = true;
			char *start = token;

			while (*start++ != ' ' && *start != '\0') {

			}

			lit_table_set(state, response_table, CONST_STRING(state, "status"), NUMBER_VALUE(strtod(start, NULL)));
		} else if (parsing_body) {
			body_token = token;
			*(body_token + strlen(token)) = ' ';

			break;
		} else {
			char *start = token;

			while (*start++ != ':' && *start != '\0') {

			}

			if (*start != '\0') {
				uint key_length = start - token - 1;

				LitString *key = lit_copy_string(state, token, key_length);
				LitString *value = lit_copy_string(state, start + 1, strlen(token) - key_length - 2);

				lit_table_set(state, headers_table, key, OBJECT_VALUE(value));
			}
		}

		parsing_body = *(token + strlen(token) + 2) == '\r';
		token = strtok(NULL, "\r\n");
	}

	if (body_token != NULL) {
		LitString *body_string = lit_copy_string(state, body_token, strlen(body_token));
		LitValue body_value;
		LitValue content_type;

		if (lit_table_get(headers_table, CONST_STRING(state, "Content-Type"), &content_type) && IS_STRING(content_type) && memcmp(AS_CSTRING(content_type), "application/json", 16) == 0) {
			body_value = lit_json_parse(vm, body_string);
		} else {
			body_value = OBJECT_VALUE(body_string);
		}

		lit_table_set(state, response_table, CONST_STRING(state, "body"), body_value);
	}

	return OBJECT_VALUE(response);
}

void lit_open_network_library(LitState* state) {
	LIT_BEGIN_CLASS("NetworkRequest")
		LIT_BIND_CONSTRUCTOR(networkRequest_contructor)
		LIT_BIND_METHOD("write", networkRequest_write)
		LIT_BIND_METHOD("read", networkRequest_read)
	LIT_END_CLASS()
}