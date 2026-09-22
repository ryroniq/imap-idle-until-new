#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>

#define TAG         "A%04d"
#define COMMAND_LOG "======== %s"
#define LINEBUF_SIZE 4096

enum {IDLE_NEWMAIL, IDLE_REISSUE, IDLE_ERROR};

typedef struct {
	char buf[LINEBUF_SIZE];
	int start;
	int len;
} linebuf_t;

int sock;

const char *host;
const char *port;
const char *user;
const char *pass;

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

/*
 * Reads one CRLF- or LF-terminated line at a time out of an internal
 * buffer, issuing further read() calls as needed. A line that is split
 * across multiple TCP reads is reassembled before being handed back, and
 * any bytes read past the end of the line are kept for the next call.
 * *line points into the internal buffer and stays valid only until the
 * next read_line() call. Returns 1 with *line set on success, 0 on EOF,
 * -1 on error.
 */
int read_line(linebuf_t *lb, char **line) {
	for (;;) {
		char *start = lb->buf + lb->start;
		char *nl = memchr(start, '\n', lb->len);
		if (nl) {
			int line_len = nl - start;
			if (line_len > 0 && start[line_len - 1] == '\r')
				start[line_len - 1] = 0;
			else
				start[line_len] = 0;

			*line = start;

			int consumed = line_len + 1;
			lb->start += consumed;
			lb->len -= consumed;
			return 1;
		}

		/* No full line buffered yet; make room for more data, compacting
		 * only now that the previously returned line is no longer needed. */
		if (lb->start > 0) {
			memmove(lb->buf, lb->buf + lb->start, lb->len);
			lb->start = 0;
		}

		if (lb->len >= LINEBUF_SIZE - 1) {
			fprintf(stderr, "Server line exceeded %d bytes\n", LINEBUF_SIZE);
			return -1;
		}

		int bytes = read(sock, lb->buf + lb->len, LINEBUF_SIZE - 1 - lb->len);
		if (bytes <= 0) {
			if (bytes < 0)
				perror("read");
			return bytes;
		}

		lb->len += bytes;
	}
}

int process_response(linebuf_t *lb, const char *ok, const char *ng) {
	char *line;
	int rc;

	while ((rc = read_line(lb, &line)) == 1) {
		printf("%s\n", line);

		if (strstr(line, ok))
			return 1;
		if (ng && strstr(line, ng))
			return 0;
	}

	return 0;
}

int process_idle(linebuf_t *lb) {
	char *line;
	int rc, cnt = 0;

	time_t last = time(NULL);
	if (last == (time_t)(-1)) {
		perror("Failed to get current time");
		return IDLE_ERROR;
	}

	while ((rc = read_line(lb, &line)) == 1) {
		printf("%s\n", line);

		if (strstr(line, "* OK Still here") == line) {
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
		} else if (strstr(line, "* ") == line) {
			return IDLE_NEWMAIL;
		}
	}

	return IDLE_ERROR;
}

int main(void) {
	if (!(host = getenv("HOST"))) {
		fprintf(stderr, "$HOST is not set\n");
		return 2;
	}
	if (!(port = getenv("PORT"))) {
		fprintf(stderr, "$PORT is not set\n");
		return 2;
	}
	if (!(user = getenv("IMAP_USER"))) {
		fprintf(stderr, "$IMAP_USER is not set\n");
		return 2;
	}
	if (!(pass = getenv("IMAP_PASS"))) {
		fprintf(stderr, "$IMAP_PASS is not set\n");
		return 2;
	}

	initialize_sock();

	printf("Connected to %s.\n", host);

	linebuf_t lb = {0};

	int ret = 0;

	if (!process_response(&lb, "* OK", NULL))
		goto cleanup;

	int seq = 0, len;
	char buffer[100], ok[20], ng[20];

	seq++;
	len = snprintf(buffer, sizeof(buffer), TAG " LOGIN %s %s\r\n", seq, user, pass);
	snprintf(ok, sizeof(ok), TAG " OK", seq);
	snprintf(ng, sizeof(ng), TAG " NO", seq);

	write(sock, buffer, len);
	snprintf(buffer, sizeof(buffer), TAG " LOGIN %s ******\r\n", seq, user);
	printf(COMMAND_LOG, buffer);

	if (!process_response(&lb, ok, ng))
		goto cleanup;

	seq++;
	len = snprintf(buffer, sizeof(buffer), TAG " SELECT INBOX\r\n", seq);
	snprintf(ok, sizeof(ok), TAG " OK", seq);
	snprintf(ng, sizeof(ng), TAG " NO", seq);

	write(sock, buffer, len);
	printf(COMMAND_LOG, buffer);

	if (!process_response(&lb, ok, ng))
		goto cleanup;

	while (1) {
		seq++;
		len = snprintf(buffer, sizeof(buffer), TAG " IDLE\r\n", seq);

		write(sock, buffer, len);
		printf(COMMAND_LOG, buffer);

		int status = process_idle(&lb);

		len = snprintf(buffer, sizeof(buffer), "DONE\r\n");
		snprintf(ok, sizeof(ok), TAG " OK", seq);
		snprintf(ng, sizeof(ng), TAG " NO", seq);

		write(sock, buffer, len);
		printf(COMMAND_LOG, buffer);

		if (!process_response(&lb, ok, ng))
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

	write(sock, buffer, len);
	printf(COMMAND_LOG, buffer);

	if (!process_response(&lb, ok, ng))
		goto cleanup;

	if (0) {
cleanup:
		ret = 1;
	}

	close(sock);

	return ret;
}
