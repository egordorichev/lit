#include "std/lit_network.h"
#include "api/lit_api.h"
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
	request_data->message = lit_reallocate(state, request_data->message, request_data->message_length, 0);
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

	if (token) {
		host_port = (char*) lit_reallocate(state, NULL, 0, strlen(token) + 1);
		strcpy(host_port, token);
	} else {
		host_port = (char*) lit_reallocate(state, NULL, 0, 1);
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

		parsed_url->path = (char*) lit_reallocate(state, NULL, 0, strlen(path) + 1);
		strncpy(parsed_url->path, path, strlen(path));

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
	lit_reallocate(state, host_port, strlen(host_port) + 1, 0);

	if (token != NULL) {
		lit_runtime_error(state->vm, "Url parsing fail");
	}

	return 0;
}

LIT_METHOD(networkRequest_contructor) {
	LitNetworkRequest* data = LIT_INSERT_DATA(LitNetworkRequest, cleanup_request);

	const char* url = LIT_CHECK_STRING(0);
	const char* method = LIT_CHECK_STRING(1);

	bool get = false;

	if (strcmp(method, "get") == 0) {
		get = true;
	} else if (strcmp(method, "post") != 0) {
		lit_runtime_error(vm, "Method (argument #2) must be either 'post' or 'get'");
	}

	LitUrl url_data;

	url_data.host = NULL;
	url_data.path = NULL;
	url_data.query_string = NULL;
	url_data.protocol = NULL;
	url_data.port = 80;

	parse_url(vm->state, (char*) url, &url_data);

	data->bytes = 0;
	data->inited_read = false;
	data->message_length = 4096;
	data->message = lit_reallocate(vm->state, NULL, 0, data->message_length);

	sprintf(data->message, "%s %s HTTP/1.0\r\n\r\n", get ? "GET" : "POST", url_data.path);

	data->socket = socket(AF_INET, SOCK_STREAM, 0);

	if (data->socket < 0) {
		free_parsed_url(vm->state, &url_data);
		lit_runtime_error(vm, "Error opening socket");
	}

	struct hostent* server = gethostbyname(url_data.host);

	if (server == NULL) {
		free_parsed_url(vm->state, &url_data);
		lit_runtime_error(vm, "Error resolving the host");
	}

	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));

	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(url_data.port);

	memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length);

	if (connect(data->socket, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
		free_parsed_url(vm->state, &url_data);
		lit_runtime_error(vm, "Connection error");
	}

	data->total_length = data->message_length;
	free_parsed_url(vm->state, &url_data);

	return instance;
}

LIT_METHOD(networkRequest_write) {
	LitNetworkRequest* data = LIT_EXTRACT_DATA(LitNetworkRequest);

	int bytes = write(data->socket,data->message + data->bytes, data->total_length - data->bytes);

	if (bytes < 0) {
		lit_runtime_error(vm, "Error writing message to the socket");
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

	if (!data->inited_read) {
		memset(data->message, 0, data->message_length);

		data->total_length = data->message_length - 1;
		data->bytes = 0;
		data->inited_read = true;
	}

	int bytes = read(data->socket, data->message + data->bytes, data->total_length - data->bytes);

	if (bytes < 0) {
		lit_runtime_error(vm, "Error reading response");
	}

	if (bytes != 0) {
		data->bytes += bytes;

		if (data->bytes < data->total_length) {
			return NULL_VALUE;
		}
	}

	if (data->bytes == data->total_length) {
		lit_runtime_error(vm, "Failed to store the response");
	}

	close(data->socket);
	return OBJECT_VALUE(lit_copy_string(vm->state, data->message, data->total_length));
}

void lit_open_network_library(LitState* state) {
	LIT_BEGIN_CLASS("NetworkRequest")
		LIT_BIND_CONSTRUCTOR(networkRequest_contructor)
		LIT_BIND_METHOD("write", networkRequest_write)
		LIT_BIND_METHOD("read", networkRequest_read)
	LIT_END_CLASS()
}