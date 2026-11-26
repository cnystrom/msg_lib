/* test_msg_lib.c - exercises msg_lib (2026). Run with `make test`.
 *
 * Part 1 tests buffer, base64, queue and bitqueue directly.
 * Part 2 runs a real message exchange: fork() gives a server process and
 * a client process joined by two pipes, using msg_lib's SDL threads. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "log_lib.h"
#include "buffer.h"
#include "base64.h"
#include "queue.h"
#include "bitqueue.h"
#include "msg_lib.h"
#include "msg_lib_version.h"

static int n_pass, n_fail;
#define CHECK(c) do { if (c) { n_pass++; printf("  ok   %s\n", #c); } \
	else { n_fail++; printf("  FAIL %s  (%s:%d)\n", #c, __FILE__, __LINE__); } } while (0)
#define DONE(name) do { printf("%s: %d passed, %d failed\n", name, n_pass, n_fail); \
	return n_fail ? 1 : 0; } while (0)

/* ---- callbacks that msg_lib expects the program to supply ---- */

static volatile int is_server, stopping, got_request, got_reply;
static char reply_str[64];
static volatile int32 reply_val;
static byte reply_bytes[16];
static volatile uint32 reply_len;

void do_data_in(void)
{
	char s[64];
	msg_num_t n;
	int32 v;
	byte b[16];
	uint32 blen = sizeof(b);

	n = msg_get_num();	/* always: it notices MSG_STOP */
	if (stopping || n == MSG_STOP)
		return;

	msg_unpack_string(s, sizeof(s) - 1);
	v = msg_unpack_int32();
	msg_unpack_bytes(b, &blen);

	if (is_server) {
		/* echo back: num+1, the string, value*2, the bytes */
		msg_set_num(n + 1);
		msg_pack_string(s);
		msg_pack_int32(v * 2);
		msg_pack_bytes(b, blen);
		msg_send();
		got_request = 1;
	} else {
		if (n == 101) {
			strcpy(reply_str, s);
			reply_val = v;
			memcpy(reply_bytes, b, blen);
			reply_len = blen;
			got_reply = 1;
		}
	}
}

int read_data(int fd, char *p, int len, int block)
{
	return (int) read(fd, p, len);
}

int write_data(int fd, char *p, int len)
{
	return (int) write(fd, p, len);
}

static int wait_flag(volatile int *flag)
{
	int i;

	for (i = 0; i < 500 && !*flag; i++)
		usleep(10000);
	return *flag;
}

static void test_parts(void)
{
	static buffer_t b, b2;
	static que q;
	static INTERTHREADQUEUE itq;
	char s[64];
	byte raw[] = { 0, 1, 2, 250, 255, 'x', 'y' };
	byte enc[32], dec[32];
	uint32 elen = sizeof(enc), dlen = sizeof(dec), blen;
	int i;

	/* buffer */
	reset_buf(&b);
	CHECK(b.len == 0);
	CHECK(pack_int8(&b, -5) == 0);
	CHECK(pack_int16(&b, 30000) == 0);
	CHECK(pack_int32(&b, -2000000000) == 0);
	CHECK(pack_string(&b, "Soli Deo Gloria") == 0);
	CHECK(pack_bytes(&b, raw, sizeof(raw)) == 0);
	CHECK(pack_int(&b, 77) == 0);
	CHECK(unpack_int8(&b) == -5);
	CHECK(unpack_int16(&b) == 30000);
	CHECK(unpack_int32(&b) == -2000000000);
	CHECK(unpack_string(&b, s, sizeof(s) - 1) == 0
	      && strcmp(s, "Soli Deo Gloria") == 0);
	blen = sizeof(dec);
	CHECK(unpack_bytes(&b, dec, &blen) == 0 && blen == sizeof(raw)
	      && memcmp(dec, raw, sizeof(raw)) == 0);
	CHECK(unpack_int(&b) == 77);

	/* base64 */
	CHECK(base64_encode((byte *) "Man", 3, enc, &elen) == 0
	      && elen == 4 && memcmp(enc, "TWFu", 4) == 0);
	elen = sizeof(enc);
	CHECK(base64_encode(raw, sizeof(raw), enc, &elen) == 0);
	CHECK(base64_decode(enc, elen, dec, &dlen) == 0
	      && dlen == sizeof(raw) && memcmp(dec, raw, dlen) == 0);

	/* queue */
	init_que(&q);
	CHECK(que_empty(&q) && !que_full(&q));
	for (i = 0; i < QUEUE_SIZE; i++) {
		reset_buf(&b);
		pack_int(&b, i);
		enque(&q, &b);
	}
	CHECK(que_full(&q) && enque(&q, &b) == -1);
	CHECK(peek_que(&q, &b2) == 0 && unpack_int(&b2) == 0);
	CHECK(deque(&q, &b2) == 0 && unpack_int(&b2) == 0);
	CHECK(deque(&q, &b2) == 0 && unpack_int(&b2) == 1);

	/* bitqueue (SDL mutex + semaphores) */
	CHECK(BoundedInterthreadQueueCreate(&itq) == 0);
	CHECK(BoundedInterthreadQueueEmpty(&itq));
	reset_buf(&b);
	pack_string(&b, "through the queue");
	BoundedInterthreadQueuePut(&itq, &b);
	CHECK(!BoundedInterthreadQueueEmpty(&itq));
	BoundedInterthreadQueueGet(&itq, &b2);
	CHECK(unpack_string(&b2, s, sizeof(s) - 1) == 0
	      && strcmp(s, "through the queue") == 0);
	CHECK(BoundedInterthreadQueueEmpty(&itq));
	BoundedInterthreadQueueKill(&itq);
}

int main(void)
{
	int c2s[2], s2c[2];
	int status = -1;
	pid_t pid;
	byte payload[] = { 9, 8, 7, 0, 6 };

	printf("msg_lib %s (%s)\n", MSG_LIB_VERSION, MSG_LIB_ORIGIN);
	setvbuf(stdout, NULL, _IOLBF, 0);
	log_lib_use_stdout(FALSE);	/* keep library logging quiet */
	alarm(30);

	test_parts();

	/* message exchange between two processes */
	if (pipe(c2s) != 0 || pipe(s2c) != 0)
		return 1;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		close(c2s[1]);
		close(s2c[0]);
		is_server = 1;
		alarm(30);
		if (msg_init_server(c2s[0], s2c[1]) != 0)
			_exit(2);
		if (!wait_flag(&got_request))
			_exit(3);
		stopping = 1;
		msg_stop_server();
		_exit(0);
	}
	close(c2s[0]);
	close(s2c[1]);

	CHECK(msg_init_client(s2c[0], c2s[1]) == 0);
	msg_set_num(100);
	msg_pack_string("hello server");
	msg_pack_int32(21);
	msg_pack_bytes(payload, sizeof(payload));
	msg_send();
	CHECK(wait_flag(&got_reply));
	CHECK(strcmp(reply_str, "hello server") == 0);
	CHECK(reply_val == 42);
	CHECK(reply_len == sizeof(payload)
	      && memcmp(reply_bytes, payload, sizeof(payload)) == 0);
	stopping = 1;
	msg_stop_client();
	waitpid(pid, &status, 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	DONE("msg_lib");
}
