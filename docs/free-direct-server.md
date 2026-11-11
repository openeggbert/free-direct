# Free Direct server: Internet multiplayer

`free-direct-server` lets games built with Free Direct play together over the Internet: natively,
in the browser, and the two together. It is an alternative to the local network. Every player,
including the one who hosts the game, connects out to the server, so no player needs an open
port or port forwarding.

```text
game (native) ── Free Direct ── ws(s):// ─┐
game (native) ── Free Direct ── ws(s):// ─┼── free-direct-server :51325 ── rooms (secret IDs)
game (browser) ─ Free Direct ── wss:// ───┘     └─ in a room: the game's sessions, as on a LAN
```

## Playing

1. In the game's multiplayer screen, choose the provider **FreeDirect server**.
2. The connection dialog asks for:
   - **Server**: the server's IP address or host name, or a `ws://` or `wss://` URL;
   - **Port**: 51325 unless the server's operator says otherwise (a URL's own port wins);
   - **Password**: only if the server has one;
   - **Room**: *Create a new room*, or *Join a room with its ID*.
3. Creating a room shows its secret ID (and copies it to the clipboard; in the browser also as a
   link). Give it to the other players.
4. The room works like a private LAN: the game's own session list shows the games in it. Create
   a game or join one, then continue in the game's lobby as usual.

The dialog appears each time you choose the provider, prefilled with the previous connection;
the game's Refresh button and its return after a failed join keep the connection. Cancel goes
back to the provider list.

### Prefilling and automation

| Variable (native) | URL parameter (browser) | Meaning |
|---|---|---|
| `FREE_DIRECT_SERVER` | `server` | address, `host:port` or URL |
| `FREE_DIRECT_SERVER_PORT` | `port` | port when the address has none |
| `FREE_DIRECT_SERVER_PASSWORD` | - | server password (never put in a URL) |
| `FREE_DIRECT_ROOM` | `room` | a room's ID; `new` (native) creates one |
| `FREE_DIRECT_ROOM_FILE` | - | after creating a room, write its ID to this file |

Natively, with both `FREE_DIRECT_SERVER` and `FREE_DIRECT_ROOM` set there is no dialog. A web page
can answer the dialog itself by defining `Module.freeApiServerPrompt(initial)` (returning a
promise of `{server, port, password, roomId, createRoom}` or `null`) and
`Module.freeApiShowRoomCreated(roomId, serverUrl)`.

Other client settings: `FREE_DIRECT_RELAY_TIMEOUT_MS` (server requests, default 8000),
`FREE_DIRECT_JOIN_TIMEOUT_MS` (joining a game, default 10000 with the server),
`FREE_DIRECT_RELAY_TLS_INSECURE=1` (accept any TLS certificate; only for a test server with a
self-signed certificate), `FREE_DIRECT_DEBUG_DPLAY=1` (log).

## Browsers need `wss://`

A page served over `https://`, such as GitHub Pages, may only open **`wss://`** connections: the
browser blocks `ws://`, except to `localhost`. For browser players the server therefore needs a
host name with a valid TLS certificate, either built in (`--tls-cert`/`--tls-key`) or through a
reverse proxy. A bare IP address with `ws://` works for native games and for pages served over
plain `http://`. When a player enters a plain address on an `https://` page, the game assumes
`wss://`.

## Building the server

The server needs a C++20 compiler, CMake, OpenSSL (for `wss://`) and
[IXWebSocket](https://github.com/machinezone/IXWebSocket) (v11.4.6 is tested). It needs neither
Free API nor SDL, nor any game data.

```bash
git clone --branch v11.4.6 --depth 1 https://github.com/machinezone/IXWebSocket ~/deps/IXWebSocket-v11.4.6
cmake -S server -B build/server -DFREE_DIRECT_IXWEBSOCKET_SOURCE_DIR=$HOME/deps/IXWebSocket-v11.4.6
cmake --build build/server
./build/server/free-direct-server --help
```

Inside a full Free Direct build, `-DFREE_DIRECT_BUILD_SERVER=ON` builds it as well. Without
OpenSSL, configure with `-DFREE_DIRECT_RELAY_TLS=OFF` (plain `ws://` only).

**Windows.** The server builds unchanged with MinGW-w64. blupi_recon's
`freeapi/tools/build_windows.sh` cross-compiles it from Linux as a self-contained
`free-direct-server.exe` (`-static`, without TLS) and packs it with `start-server.bat`
(`../../blupi_recon/freeapi/README.md`, "Windows").

## Running

```bash
free-direct-server                                   # ws://0.0.0.0:51325, no password
free-direct-server --port 51325 --password-file /etc/free-direct/password
free-direct-server --tls-cert fullchain.pem --tls-key privkey.pem    # wss://
```

| Option | Default | Meaning |
|---|---|---|
| `--host` | `0.0.0.0` | listen address |
| `--port` | `51325` | listen port (TCP) |
| `--password-file` | none | require the password on the file's first line; or set `FREE_DIRECT_SERVER_PASSWORD` |
| `--tls-cert`, `--tls-key` | none | serve `wss://` with this PEM certificate chain and key |
| `--allow-origin` | any | accept browser pages only from this origin (repeatable), e.g. `https://games.example.org` |
| `--max-connections` | 512 | |
| `--max-rooms` | 256 | |
| `--max-room-members` | 16 | |
| `--max-room-games` | 8 | sessions per room |
| `--room-idle-timeout` | 600 | seconds a room without members is kept |
| `--ping-interval` | 20 | seconds; 0 disables |

The log goes to standard error: connections with their address, hellos, rooms (only the first
four characters of an ID), refusals. It never contains passwords or game data. `SIGINT`/`SIGTERM`
end every game and close every connection; rooms do not survive a restart.

Open the TCP port in the firewall. Behind NAT, forward it to the server; players need nothing.

### TLS through a reverse proxy

Instead of `--tls-cert`, a proxy can terminate TLS and forward the WebSocket upgrade. Caddy, which
also obtains the certificate:

```caddyfile
play.example.org {
    reverse_proxy 127.0.0.1:51325
}
```

nginx:

```nginx
location / {
    proxy_pass http://127.0.0.1:51325;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_read_timeout 3600s;
}
```

Players then enter `wss://play.example.org` (port 443). The server sees the proxy's address, so
its per-address limits apply to the proxy; keep `--host 127.0.0.1` in that setup.

### systemd

```ini
[Unit]
Description=Free Direct server
After=network-online.target

[Service]
ExecStart=/usr/local/bin/free-direct-server --host 0.0.0.0 --port 51325 --password-file /etc/free-direct/password
DynamicUser=yes
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

## Security

- The room ID is the key to a room: 100 random bits, never listed, rate-limited guessing. Share
  it only with the players you want.
- The password (optional) keeps strangers off the server. Over `ws://` it travels in clear text;
  use `wss://` on the Internet.
- The server keeps every message inside its room and session. It does not check the DirectPlay
  identities inside game data (`docs/directplay-design.md` Decision 30).

## Limits

- Everything is delivered reliably and in order, also game messages sent without
  `DPSEND_GUARANTEED`; on a lossy connection, later messages wait for an earlier one.
- The player who creates a game is its host. When the host leaves, the game ends for everyone.
- A game's players are compatible builds of one game and edition; the room refuses others.

Protocol: `docs/directplay-relay-protocol.md`. Tests: `tests/relay_hub_tests.cpp`,
`tests/relay_directplay_tests.cpp` (`ctest -L relay` in a build with `FREE_DIRECT_ENABLE_RELAY`
and `FREE_DIRECT_BUILD_SERVER`), and blupi_recon's `freeapi/tools/net_web.mjs`.
