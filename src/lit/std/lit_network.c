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

LIT_METHOD(networkRequest_contructor) {
	LitNetworkRequest* data = LIT_INSERT_DATA(LitNetworkRequest, cleanup_request);

	int portno = 80;
	char *host = "postman-echo.com";

	struct hostent *server;
	struct sockaddr_in server_address;

	data->bytes = 0;
	data->inited_read = false;
	data->message_length = 4096;
	data->message = lit_reallocate(vm->state, NULL, 0, data->message_length);

	sprintf(data->message, "POST /post HTTP/1.0\r\n\r\n");

	data->socket = socket(AF_INET, SOCK_STREAM, 0);

	if (data->socket < 0) {
		lit_runtime_error(vm, "Error opening socket");
	}

	server = gethostbyname(host);

	if (server == NULL) {
		lit_runtime_error(vm, "Error resolving the host");
	}

	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(portno);
	memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length);

	if (connect(data->socket, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
		lit_runtime_error(vm, "Connection error");
	}

	data->total_length = data->message_length;
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