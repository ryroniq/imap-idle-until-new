# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A minimal C client that logs into an IMAP server over a plain (unencrypted) TCP socket, `SELECT`s INBOX, and issues IMAP `IDLE` in a loop, blocking until new mail arrives (or the server's idle timeout forces a periodic re-issue). It has no TLS support by design — it's meant to be pointed at `HOST=localhost` through an SSH tunnel, which provides the encryption and access control instead. `imap-idle-until-new.sh` wraps the binary in a fetch/idle loop with `fdm` (fetchmail-like MDA) to pull mail into a local Maildir each time new mail is signaled.

## Build

```sh
make          # builds ./imap-idle-until-new (no external libraries required)
make clean    # removes the binary and *.o files
```

Override compiler flags via a local `config.mk` (git-ignored, included by the Makefile) rather than editing the Makefile, e.g.:

```make
CC_ARGS = -Wall -O2
```

No test suite or linter is configured in this repo.

## Running

The C binary (`imap-idle-until-new`) reads all configuration from environment variables and exits if any are missing:

- `HOST`, `PORT` — IMAP server (typically `localhost` and a locally forwarded SSH tunnel port)
- `IMAP_USER`, `IMAP_PASS` — IMAP LOGIN credentials (named to avoid colliding with the POSIX `$USER` env var already set in most shells)

Exit codes (consumed by the wrapper script):
- `0` — clean logout after IDLE reported new mail
- `1` — a protocol/network error occurred (`goto cleanup` paths)
- `2` — a required environment variable was not set

The wrapper script (`imap-idle-until-new.sh`) additionally requires `ACCT` (fdm account name) and expects both `fdm` and `imap-idle-until-new` to be on `$PATH`. Its `fdm` config uses the plaintext `imap` method (not `imaps`), matching the binary's lack of TLS. Its loop is: run `fdm fetch` to drain any existing mail into `%h/Maildir`, block on `imap-idle-until-new` until it signals new mail, then repeat. On exit status `1` (or any unexpected status) it sleeps `SLEEP_SEC` (60s) before retrying; on status `2` it exits the whole loop (unrecoverable config error).

## Architecture notes

- `imap-idle-until-new.c` is a single-file, single-threaded client built directly on POSIX sockets (`getaddrinfo`/`socket`/`connect`, then plain `read`/`write` — no libcurl/imap library, no TLS library). All protocol framing is manual: commands are tagged with an incrementing `A%04d` sequence (`TAG`/`seq`), and `process_response(ok, ng)` scans lines for the expected tagged `OK`/`NO` substrings.
- `process_idle()` is the core state machine for the `IDLE` command: it distinguishes server keepalives (`* OK Still here`) from an actual untagged mail-arrival response (any other line starting with `* `), and forces a fresh `IDLE`/`DONE` cycle (`IDLE_REISSUE`) after 15 keepalives to avoid server-side idle timeouts. Return values (`IDLE_NEWMAIL` / `IDLE_REISSUE` / `IDLE_ERROR`) drive the `switch` in `main()` that decides whether to loop, re-issue IDLE, or fall through to `LOGOUT`.
- Both `process_response()` and `process_idle()` read through a shared `read_line()` helper (backed by a single `linebuf_t` that persists for the life of the connection) rather than calling `read()` directly. This reassembles lines that are split across TCP reads, and only compacts the buffer (shifting unread bytes to the front) lazily — right before the next `read()` — so a line pointer returned to a caller stays valid until that caller's next `read_line()` call instead of being clobbered immediately.
- Control flow in `main()` uses `goto cleanup` / `goto logout` for teardown (single `close(sock)` exit path at the bottom), matching the C idiom used throughout rather than early returns.
- The two files are intentionally decoupled: the C binary only knows about a single IDLE session and exits; all "fetch mail, then wait again" looping and mail delivery (via `fdm`) lives in the shell wrapper.
