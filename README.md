# imap-idle-until-new

A tiny plaintext IMAP client that logs in, selects INBOX, and issues IMAP
`IDLE`, blocking until new mail arrives (re-issuing `IDLE` periodically to
avoid server-side idle timeouts). Pair it with the included shell wrapper
and [`fdm`](https://github.com/nicm/fdm) to fetch mail into a local Maildir
the moment it arrives, instead of polling.

It speaks plain (unencrypted) IMAP over a raw TCP socket. It's meant to be
pointed at `HOST=localhost` through an SSH tunnel (e.g.
`ssh -L 143:localhost:143 relay`), where the SSH tunnel itself provides
the encryption and access control — no TLS certs and no server-side
firewall/source-IP allowlisting to maintain. Do not point this at a
server directly over an untrusted network.

## Build

Requires a C compiler. No external libraries are needed.

```sh
make
```

This produces the `imap-idle-until-new` binary. `make clean` removes build
artifacts. Compiler flags can be overridden without editing the Makefile by
creating a local `config.mk` (git-ignored):

```make
CC_ARGS = -Wall -O2
```

## Usage

### `imap-idle-until-new`

Runs a single login → `IDLE` → logout cycle and exits. Configuration is
read entirely from environment variables:

| Variable    | Description                          |
|-------------|---------------------------------------|
| `HOST`      | IMAP server hostname (e.g. `localhost` when tunneled) |
| `PORT`      | IMAP server port (e.g. `143`, or whatever local port an SSH tunnel forwards) |
| `IMAP_USER` | IMAP login username                  |
| `IMAP_PASS` | IMAP login password                  |

```sh
HOST=localhost PORT=143 IMAP_USER=me@example.com IMAP_PASS=hunter2 \
./imap-idle-until-new
```

Exit codes:

| Code | Meaning                                                     |
|------|--------------------------------------------------------------|
| `0`  | New mail arrived; `LOGOUT` completed cleanly                 |
| `1`  | A protocol or network error occurred                         |
| `2`  | A required environment variable was not set                  |

### `imap-idle-until-new.sh`

Wraps the binary in a fetch/idle loop: on each iteration it runs `fdm
fetch` to pull any waiting mail into `%h/Maildir`, then blocks on
`imap-idle-until-new` until new mail is signaled, then repeats. On exit
code `1` it sleeps 60 seconds before retrying; on `2` it exits the loop
entirely; any other/unexpected exit code also triggers a 60-second sleep
before retrying, to avoid hot-looping on an unforeseen failure (e.g. a
crash).

Requires `fdm` and `imap-idle-until-new` on `$PATH`, plus the variables
above and:

| Variable | Description                                    |
|----------|-------------------------------------------------|
| `ACCT`   | Account name used in the generated `fdm` config |

```sh
ACCT=me HOST=localhost PORT=143 \
IMAP_USER=me@example.com IMAP_PASS=hunter2 \
./imap-idle-until-new.sh
```

## License

MIT, see [LICENSE](LICENSE).
