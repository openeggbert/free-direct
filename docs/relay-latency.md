# Lag through a Free Direct server: what it is, and the transports that would fix it

Status: **analysis and proposal; the datagram transports below are not implemented.** Recorded on
2026-11-14 after players' reports of lag and of other players' Blupis "teleporting" in games through
`fds.openeggbert.com`. Labels: **[EVIDENCE]** source or measurement, **[DESIGN]** proposal,
**[OPEN]** a decision for the owner.

## 1. What the players see

[EVIDENCE] Speedy Blupi's and Speedy Blupi 2's network model (unchanged historical source, in the
SDL3 port and in the Free API builds alike): each player simulates only its own Blupi and sends its
position every tick (50 ms), mostly as a *non-guaranteed* DirectPlay message
(`blupi_recon/src/decmove.cpp:59-109`, `CDecor::TreatNetData`). A receiver shows the newest
position; when a tick brings none, it extrapolates the last velocity for at most 5 ticks (250 ms,
`CDecor::DoNetSmooth`, `decmove.cpp:236-256`) and then stops the Blupi. When the late positions
arrive, the Blupi jumps to the newest one: the "teleport".

## 2. Why a server makes it worse

1. **[EVIDENCE] One TCP connection carries everything.** The server's transport is WebSocket, so every
   message, guaranteed or not, is delivered reliably and in order. One lost packet holds back all
   messages behind it until TCP resends it (Linux: at least 200 ms; more with exponential backoff), and
   they then arrive together. Over UDP (DirectPlay's own transport, ENet in the port) a lost position
   is simply replaced by the next one 50 ms later. A held-back burst longer than the 250 ms of
   prediction is a visible stop and jump.
2. **[EVIDENCE] Route.** In the SDL3 port (1.5.0 to 1.6.0) a member's messages went to the host, which
   sent them on: member to member was four crossings of the Internet plus the host's loop. Free
   Direct's DirectPlay already used the server's route 2 ("to all others", Decision 30) for
   broadcasts. The port now does too, once the game has started (blupi_recon `port/common/net/session.cpp`,
   session wire version 2).
3. **Distance.** Each crossing costs the path to the server. A server near the players helps every
   transport; it is an operations choice, not a code one.

## 3. Measurements

[EVIDENCE] filled in below from `blupi_recon/port/tests/tcp_stall.py` runs: two SDL3 port programs in
real time (Speedy Blupi, both players walking), a local `free-direct-server`, and the client's
connection through the stalling proxy. The port's link diagnostics count the messages that arrived
over 150 and over 250 ms later than their sender's tick implies.

| Run (78 s, both players walking) | Messages | Over 150 ms late | Over 250 ms late | Latest |
|---|---|---|---|---|
| No stalls (host's view of the client) | 1031 | 0 | 0 | 17 ms |
| A 300 ms stall every 3 s on average, server to client (client's view of the host) | 1025 | 47 | 16 | 304 ms |

Every message over 250 ms late is a stop and a jump of the other player's Blupi on that screen. A
datagram transport would have dropped the lost position instead, and the next one would have come
50 ms later.

## 4. Diagnostics now in the clients

- [EVIDENCE] SDL3 port (`session.cpp`): per sender, in play, how late each message arrives against
  the tick its sender stamped on it; "network: link from player N ..." on stderr every 30 s while new
  late messages occur, and when the session ends.
- [EVIDENCE] Free Direct (`RelayDirectPlayTransport`): per sender connection, stalls (a pause of over
  150 ms followed by at least three messages at once); "free-direct relay: link from connection ..."
  on stderr, likewise. No change to the games' code.

## 5. Transports that would remove the stalls

Both keep the WebSocket for everything guaranteed (lobby, start, deaths, objects) and send the
non-guaranteed messages (positions) as datagrams, which a loss only drops.

### A. A UDP side channel for native programs [DESIGN]

- The server opens a UDP socket on its port number. Its Welcome offers a datagram token (random,
  per connection). The client sends `Bind(token)` datagrams until the server confirms over the
  WebSocket; then it sends non-guaranteed Data as datagrams (token, route, target, payload), and the
  server forwards non-guaranteed deliveries to a bound peer by UDP, to anyone else over WebSocket.
- NAT: the client opens the mapping; a keep-alive every 15 s holds it. No answer within a few seconds
  (UDP blocked): WebSocket only, as today.
- Clients: the SDL3 port (its own relay client; ENet's socket API already gives UDP) and Free
  Direct's `RelayClient` for native Free API builds. The games and the DirectPlay API are unchanged.
- Operations: the server's UDP port must be open in the VPS firewall; nginx (the `wss://` front) is
  not involved.
- Cost: a protocol version 2 (Welcome offer, Bind), about 200 lines in the server and 150 in each
  client, hub unit tests and a stall test. Helps desktop and Android players, not browsers.

### B. WebRTC data channels for browsers [DESIGN]

- The server becomes each browser's WebRTC peer (libdatachannel, MPL-2.0, with libjuice and usrsctp;
  TLS from OpenSSL or Mbed TLS). Offer, answer and ICE candidates travel over the existing WebSocket.
  An unordered data channel with no retransmissions carries the non-guaranteed messages.
- The server has a public address, so ICE needs no STUN or TURN server; it needs a UDP port (or
  range) open.
- Clients: the browser side of both (the port's Emscripten WebSocket layer, Free Direct's
  `RelaySocketWeb.cpp`) in JavaScript; native clients could use the same channel through
  libdatachannel instead of A, at the cost of that dependency in every native build.
- Cost: the largest: a server dependency and its build on the VPS, the signalling, browser glue,
  headless-browser tests.

### C. WebTransport datagrams [DESIGN, not recommended now]

HTTP/3 datagrams need a QUIC server stack (heavier than libdatachannel) and are not yet in every
browser the games run in.

## 6. Proposal [OPEN]

1. First A: small, in both the port and Free Direct, measurable with the stall proxy; it removes the
   stalls for desktop and Android players.
2. Then B for browsers, sharing A's server-side forwarding of non-guaranteed messages.
3. Meanwhile, the diagnostics above tell how often real players' links stall, which decides how
   urgent B is.
