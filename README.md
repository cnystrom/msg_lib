# msg_lib: threaded message passing over a pair of file descriptors

Version 1.1 (2026). Split from NewIO 0.12, https://github.com/cnystrom/newio

## Versions

- **1.0:** original code as published in NewIO 0.12 (tag `v1.0`).
- **1.1:** this release, with the fixes listed below. See `CHANGES.md`.

msg_lib is NewIO's message layer. A program packs integers, strings and
binary data into a message and sends it. Background threads (SDL threads)
read and write length-prefixed text messages on any pair of file
descriptors (a socket, pipes, stdin/stdout under inetd, or an ssh
subsystem) and hand each incoming message to a callback.

It also contains the pieces it is built from: a text pack/unpack buffer,
base64 for binary data, a fixed-size queue, and a bounded queue shared
between threads.

## API

From `msg_lib.h` (the program must supply the three `extern` functions):

| Function | What it does |
|---|---|
| `extern void do_data_in(void)` | **You write this.** Called on the data thread for each incoming message; call `msg_get_num()` and `msg_unpack_*()` in it. |
| `extern int read_data(int fd, char *p, int len, int block)` | **You write this.** Read up to `len` bytes. |
| `extern int write_data(int fd, char *p, int len)` | **You write this.** Write `len` bytes. |
| `int msg_init_server(int in, int out)` / `int msg_init_client(int in, int out)` | Start the reader, writer and data threads. 0 on success. |
| `void msg_stop_server(void)` / `void msg_stop_client(void)` | Shut down with a `MSG_STOP` handshake (server starts it, client answers). |
| `void msg_set_num(msg_num_t)` / `msg_num_t msg_get_num(void)` | Message number, packed first. |
| `msg_pack_string`, `msg_unpack_string`, `msg_pack_int32/16/8`, `msg_unpack_int32/16/8`, `msg_pack_bytes`, `msg_unpack_bytes` | Add to the outgoing message or read from the current incoming one. |
| `void msg_send(void)` | Queue the outgoing message. |

`msg_stop_ssh_client()` is declared, but its body is commented out in NewIO
0.12.

From `buffer.h`: `buffer_t` (an 80 KB buffer), `reset_buf`, `pack_int8/16/32`,
`pack_int`, `pack_string`, `pack_bytes` and the matching `unpack_*`.
From `base64.h`: `base64_encode`, `base64_decode`. From `queue.h`: `que`,
`init_que`, `que_full`, `que_empty`, `enque`, `deque`, `peek_que`
(25 slots). From `bitqueue.h`: `INTERTHREADQUEUE` and
`BoundedInterthreadQueueCreate/Put/Get/Empty/Destroy/Kill`.
`msg_lib_version.h` defines `MSG_LIB_VERSION` ("1.1") and `MSG_LIB_ORIGIN`.

## Dependencies

- **SDL 1.2** (system library, found with `sdl-config`) for threads,
  mutexes and semaphores. On current Linux, `sdl12-compat` works (tested with
  1.2.68). Not copied into this project.
- **log_lib** (sibling project) for log messages.
- **str_lib** (sibling project): `buffer.c` includes `str_lib.h`, so only its
  header is needed. Nothing has to be linked.

The `Makefile` looks in `../log_lib` and `../str_lib` (override with
`LOG_LIB_DIR=`/`STR_LIB_DIR=`). Link order: `msg_lib.a log_lib.a` and
`$(sdl-config --libs)`.

## Build

Check out the sibling projects next to this one, install the SDL 1.2
development package (`libsdl1.2-compat-dev` or `libsdl1.2-dev` on
Debian/Ubuntu), then:

```sh
make            # builds msg_lib.a
make test       # builds the siblings if needed, then runs test_msg_lib
make CC=clang test
make clean
```

When unpacked from the release tarballs, the directory names include the
version, so give the sibling paths:

```sh
make STR_LIB_DIR=../str_lib-1.1 LOG_LIB_DIR=../log_lib-1.1 test
```

The test checks the buffer, base64, queue and bitqueue code, then forks a
server process and a client process joined by two pipes. They exchange a
message (number, string, int32 and binary bytes) and shut down with the
`MSG_STOP` handshake.

## License

LGPL 2.1 or later, the same as NewIO. See `LICENSE` (GNU LGPL 2.1). `COPYING`
holds the GNU GPL 2, as in NewIO. Copyright Chris Nystrom; see the header of
each file for its years.

## Origin

Split from NewIO 0.12, https://github.com/cnystrom/newio (tag `newio-0.12`,
commit `ce54c5b`), directory `common/msg_lib/`. The library code is Chris
Nystrom's original, copied unchanged except for the lines listed under
"Changes from NewIO" below.

Contact: Chris Nystrom <cnystrom@gmail.com>

## Changes from NewIO

- `buffer.c:40` and `msg_lib.c:45`: `#include "nio_log.h"` became
  `#include "log_lib.h"`. The commented-out include near `msg_lib.c:36` is
  left alone.
- `base64.c:1-7`: added a header comment crediting Tom St Denis
  (LibTomCrypt, public domain) as the original author.
- Removed two old personal contact lines (a web-log URL and a chat handle) from the file headers, at Chris's request (2026). The rest of each header and the authorship are unchanged, so header-area line numbers are 2 lower than in NewIO.
- `Makefile:36-38`: `-I.. -I../nio_log -I../str_lib` became `-I.
  -I$(LOG_LIB_DIR) -I$(STR_LIB_DIR)`.
- Added a copy of NewIO's `common/nio_types.h` (unchanged),
  `msg_lib_version.h`, `test_msg_lib.c`, the sibling and `make test` rules at
  the end of the `Makefile`, this README, `LICENSE` and `COPYING`.
- Not copied: `archive.sh`, `attic/` and `examples/`. The examples need the
  rest of NewIO and stay there.

## Fixed in 1.1

No code fixes were needed here. The base64 credit above is new in 1.1.

## Known issues

Kept as in NewIO 0.12 (design limits, not fixed):

- There is one connection per process: all state is static.
- `bitqueue.c:135`: `BoundedInterthreadQueueDestroy()` puts a copy of the
  queue's first slot back in the queue to wake the waiting thread (Chris's
  comment asks why). At shutdown, `do_data_in()` can then be called once more
  with an old message. Programs should ignore messages after they start
  stopping, as the test does.
- `buffer.c:211` and `:292`: `unpack_string`/`unpack_bytes` need at least 4
  characters left in the message, so a very short final string fails to
  unpack.
- `buffer.c:234`: `unpack_string(b, s, str_size)` writes a NUL at
  `s[len]`, so pass `sizeof(s) - 1` as `str_size`.
- **base64.c** was written by Tom St Denis for LibTomCrypt, which he
  released into the public domain. It is the only code in these six
  libraries not written by Chris, and its file header now credits him.
- Windows: `bitqueue.h` includes `<SDL_thread.h>` and `buffer.c` uses
  winsock. Not built or tested here.
