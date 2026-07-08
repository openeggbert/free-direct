# DirectPlay networking backends

This document restates `CLAUDE.md`'s "Networking Backend Decision" policy in doc form, with the
concrete detail of what is actually implemented today. It does not introduce any new decision -
see `docs/directplay-design.md` for the numbered Decisions that shaped the abstraction described
here, and `docs/directplay-limitations.md` for what each backend still cannot do.

## The abstraction: `IDirectPlayTransport`

`src/directplay/DirectPlayTransport.hpp` (a **private** header - never installed, never reachable
from `include/dplay.h`) declares `IDirectPlayTransport`, representing "however bytes actually move
between two peers," independent of which real backend is in use:

- `Listen(port)` / `Connect(address, port)` - start hosting or join a host.
- `Send(targetId, data, size, reliable)` / `Receive(buffer, bufferSize, outSize)` - move bytes,
  addressed by `DPID` (an opaque lookup key to the transport - it never allocates or validates one
  itself, per `docs/directplay-design.md` Decision 7's "what stays out of the transport").
- `Service()` - pump whatever event processing the backend needs (a no-op for a backend with no
  such concept). Called from `DirectPlay2AImpl::Receive()`, piggybacking on
  `free-eggbert`'s own existing poll-`Receive()`-repeatedly pattern rather than introducing a
  background thread (Decision 6).
- `HasPendingConnection()` / `AssignPendingConnection(id)` / `RejectPendingConnection()` - the
  hosting role's incoming-connection queue.
- `HasDisconnectedPeer()` / `TakeDisconnectedPeer(outId)` - the hosting role's departure queue.
- `IsConnectedToHost()` - the joining role's own connection-health check.
- `Shutdown()` - tear down cleanly.

`DirectPlay.cpp`'s `DirectPlay2AImpl` (the public `IDirectPlay2A` implementation) is the only code
that knows about `DPID` allocation, session state, and DirectPlay-level policy (caps, validation,
error codes). The transport's job stays "move bytes and report connection-shaped facts"; every
`IDirectPlayTransport` implementation is deliberately kept ignorant of `DPSESSIONDESC2`-shaped
DirectPlay concepts (`CLAUDE.md`'s Internal Backend Policy).

`Open()` picks which concrete backend to construct at **build time**, via the
`FREE_DIRECT_ENABLE_ENET` CMake option/compile definition - never at runtime, and never via any
new public API surface (`docs/directplay-design.md` Decision 4).

## Backend 1: `LoopbackDirectPlayTransport` - always available, the default

An in-process, no-real-sockets transport used for deterministic unit tests and for a same-process
"host + client" smoke test. Connection lifecycle is implemented via direct C++ object-pointer
tracking across instances in the same process (a port-keyed static registry resolves `Connect()`
to a live `Listen()`ing instance), not real I/O - see Decision 10. This is the **only** backend
compiled in by default (`FREE_DIRECT_ENABLE_ENET=OFF`), and the one every committed
`tests/directplay_tests.cpp` test runs against.

## Backend 2: `EnetDirectPlayTransport` - preferred first real backend, opt-in

Built only when `FREE_DIRECT_ENABLE_ENET=ON`. Uses [ENet](http://enet.bespin.org/), chosen (per
`CLAUDE.md`) because it already provides reliable UDP, ordered delivery, packet fragmentation, peer
connection management, and a connect/disconnect/receive event model that closely matches what a
DirectPlay-shaped API needs - without FreeDirect having to hand-roll reliability, ordering,
fragmentation, or retransmission itself.

**What works today:** hosting (`Listen()` on a fixed port, `docs/directplay-design.md` Decision 5),
accepting and tracking multiple connecting clients (Decision 7), rejecting excess connections over
`dwMaxPlayers` (Decision 9), the hosting role's addressed `Send()` to one of its own connected
peers, and real receive-side buffering for both roles (Decision 19 - this was a gap as recently as
Decision 14, since closed).

**What does not work today**, all tracked as open items in `docs/directplay-limitations.md`: the
joining role's `Open()` branch never calls `Connect()` at all (no mechanism exists yet for it to
learn a host's address - `DPSESSIONDESC2` has no address-like field); the join handshake
(Decision 16) is loopback-only; `EnumSessions()` (Decision 18) is loopback-only and cannot see an
ENet-hosted session.

Uses a single ENet channel (channel `0`) for all traffic (Decision 2) - `IDirectPlay2A`'s
`Send`/`Receive` have no channel-selection concept for any real call site to need more than one.

## Backend 3: `SdlNetDirectPlayTransport` - optional, future, not implemented

Documented here, per policy, but **not implemented** - and per `CLAUDE.md`, should not be started
until the ENet backend is stable (it is not yet: joining and discovery remain open per the section
above). Two possible SDL3_net-based paths exist, each with a real tradeoff:

- **SDL3_net UDP datagrams**: FreeDirect would have to implement reliability, ordering,
  fragmentation, acknowledgement, and retransmission itself - essentially re-deriving what ENet
  already provides for free. Only worth pursuing if ENet ever becomes unavailable on a target
  platform this project needs to support.
- **SDL3_net TCP stream sockets**: simpler to stand up for a basic client/server mode, but a poor
  semantic match for DirectPlay-style discrete, sometimes-unreliable game packets - stream framing
  would have to be added manually on top, and TCP has no unreliable/unordered send option at all
  (unlike ENet, which offers both reliable and unreliable delivery per `Send()` call via its
  `reliable` parameter).

**This document does not commit to implementing either path.** Per `CLAUDE.md`'s rule of thumb:
ENet first and by default; SDL3_net stays a documented option, not a commitment; raw sockets and
Windows-only networking APIs are never used, in either backend.
