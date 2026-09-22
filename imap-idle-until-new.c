#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netdb.h>

#define TAG         "A%04d"
#define COMMAND_LOG "======== %s"

enum {IDLE_NEWMAIL, IDLE_REISSUE, IDLE_ERROR};

int sock;
SSL_CTX *ctx;
SSL *ssl;

const char *cert_path;
const char *host;
const char *port;
const char *user;
const char *pass;

void initialize_openssl() {
	SSL_load_error_strings();
	OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
	EVP_cleanup();
}

void initialize_context() {
	ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) {
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
	if (SSL_CTX_load_verify_locations(ctx, cert_path, NULL) <= 0) {
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
}

void initialize_sock(void) {
	struct addrinfo hints, *res;
	int error = 0;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &res) != 0) {
		perror("getaddrinfo error");
		exit(EXIT_FAILURE);
	}

	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
		perror("Socket creation error");
		goto cleanup;
	}

	if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		perror("Connection error");
		close(sock);
		goto cleanup;
	}

	if (0) {
cleanup:
		error = 1;
	}

	freeaddrinfo(res);

	if (error)
		exit(EXIT_FAILURE);
}

int process_response(const char *ok, const char *ng) {
	char buffer[1024];
	int bytes;

	while ((bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytes] = 0;
		printf("%s", buffer);

		if (strstr(buffer, ok))
			return 1;
		if (ng && strstr(buffer, ng))
			return 0;
	}

	if (bytes < 0)
		ERR_print_errors_fp(stderr);

	return 0;
}

int process_idle(void) {
	char buffer[1024];
	int bytes, cnt = 0;

	time_t last = time(NULL);
	if (last == (time_t)(-1)) {
		perror("Failed to get current time");
		return IDLE_ERROR;
	}

	while ((bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytes] = 0;
		printf("%s", buffer);

		if (strstr(buffer, "* OK Still here") == buffer) {
			time_t now = time(NULL);
			if (now == (time_t)(-1)) {
				perror("Failed to get current time");
				return IDLE_ERROR;
			}

			cnt++;
			printf(" %2d. interval: %3ld secs\n", cnt, now - last);
			last = now;

			if (cnt > 14)
				return IDLE_REISSUE;
		} else if (strstr(buffer, "* ") == buffer) {
			return IDLE_NEWMAIL;
		}
	}

	if (bytes < 0)
		ERR_print_errors_fp(stderr);

	return IDLE_ERROR;
}

int main(void) {
	if (!(cert_path = getenv("CERT_PATH"))) {
		fprintf(stderr, "$CERT_PATH is not set\n");
		return 2;
	}
	if (!(host = getenv("HOST"))) {
		fprintf(stderr, "$HOST is not set\n");
		return 2;
	}
	if (!(port = getenv("PORT"))) {
		fprintf(stderr, "$PORT is not set\n");
		return 2;
	}
	if (!(user = getenv("USER"))) {
		fprintf(stderr, "$USER is not set\n");
		return 2;
	}
	if (!(pass = getenv("PASS"))) {
		fprintf(stderr, "$PASS is not set\n");
		return 2;
	}

	initialize_openssl();
	initialize_context();
	initialize_sock();

	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, sock);

	if (SSL_connect(ssl) <= 0) {
		ERR_print_errors_fp(stderr);
		goto cleanup;
	}

	printf("Connected to %s over TLS.\n", host);

	if (!process_response("* OK", NULL))
		goto cleanup;

	int ret = 0, seq = 0, len;
	char buffer[100], ok[20], ng[20];

	seq++;
	len = snprintf(buffer, sizeof(buffer), TAG " LOGIN %s %s\r\n", seq, user, pass);
	snprintf(ok, sizeof(ok), TAG " OK", seq);
	snprintf(ng, sizeof(ng), TAG " NO", seq);

	SSL_write(ssl, buffer, len);
	snprintf(buffer, sizeof(buffer), TAG " LOGIN %s ******\r\n", seq, user);
	printf(COMMAND_LOG, buffer);

	if (!process_response(ok, ng))
		goto cleanup;

	seq++;
	len = snprintf(buffer, sizeof(buffer), TAG " SELECT INBOX\r\n", seq);
	snprintf(ok, sizeof(ok), TAG " OK", seq);
	snprintf(ng, sizeof(ng), TAG " NO", seq);

	SSL_write(ssl, buffer, len);
	printf(COMMAND_LOG, buffer);

	if (!process_response(ok, ng))
		goto cleanup;

	while (1) {
		seq++;
		len = snprintf(buffer, sizeof(buffer), TAG " IDLE\r\n", seq);

		SSL_write(ssl, buffer, len);
		printf(COMMAND_LOG, buffer);

		int status = process_idle();

		len = snprintf(buffer, sizeof(buffer), "DONE\r\n");
		snprintf(ok, sizeof(ok), TAG " OK", seq);
		snprintf(ng, sizeof(ng), TAG " NO", seq);

		SSL_write(ssl, buffer, len);
		printf(COMMAND_LOG, buffer);

		if (!process_response(ok, ng))
			goto cleanup;

		switch (status) {
			case IDLE_NEWMAIL:
				goto logout;
			case IDLE_REISSUE:
				continue;
			default:
				goto cleanup;
		}
	}

logout:
	seq++;
	len = snprintf(buffer, sizeof(buffer), TAG " LOGOUT\r\n", seq);
	snprintf(ok, sizeof(ok), TAG " OK", seq);
	snprintf(ng, sizeof(ng), TAG " NO", seq);

	SSL_write(ssl, buffer, len);
	printf(COMMAND_LOG, buffer);

	if (!process_response(ok, ng))
		goto cleanup;

	if (0) {
cleanup:
		ret = 1;
	}

	SSL_free(ssl);
	close(sock);
	SSL_CTX_free(ctx);
	cleanup_openssl();

	return ret;
}
