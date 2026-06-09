# Networking

Plug-and-play multiplayer. "Mark `[[replicated]]`, click play, friend joins via code."

## Services & modules

- `zues_net_udp.dll` — transport (reliability, channels, connection management). Service: `INetTransport_v1`.
- `zues_net_replication.dll` — component replication. Uses reflection over `[[Engine::replicated]]` fields.
- `zues_net_prediction.dll` — client prediction + server reconciliation over `[[Engine::predicted_input]]`.

## Transport: UDP + reliability

Custom, ~1500 LoC target. Channels:

- **unreliable-unordered** — low-latency state (position snapshots)
- **reliable-ordered** — RPCs, chat, game events
- **reliable-unordered** — less common; messages that must arrive but order doesn't matter

Features: sequence numbering, ack bitfields, packet fragmentation (for MTU > 1200), congestion-aware send pacing, duplicate detection.

## Connection flow

1. **Host**: opens UDP listen on ephemeral port. Queries a STUN server → gets public `ip:port`. Encodes to short join code (e.g., base32).
2. **Client**: pastes join code → decodes to `ip:port`. Both sides send UDP packets simultaneously: NAT hole-punch.
3. **Established**: encryption handshake (XSalsa20 / ChaCha20 + Poly1305), connection ID assigned, tick sync.

Fallback: if STUN punch fails (CGNAT / symmetric NAT), client gets clear error. User arranges port-forwarding or their own relay. **Engine does not host a TURN relay.**

## Replication

- Fields marked `[[Engine::replicated]]` are collected at component registration.
- Server snapshot: delta against last-ack'd state, quantized where annotated, packed, sent.
- Client: applies deltas, tracks which fields came from server vs local prediction.

```cpp
struct Transform {
    [[Engine::replicated]] Engine::vec2 position;
    [[Engine::replicated]] float rotation;
    float render_scale;  // local only
};

struct Health {
    [[Engine::replicated, Engine::quantize(0, 100, 7)]] int hp;  // 7-bit quantization
};
```

## Prediction + reconciliation

- Input components marked `[[Engine::predicted_input]]` are applied locally on the owning client immediately.
- Simultaneously sent to server.
- Server simulates, sends authoritative snapshot back.
- On mismatch: client rewinds predicted state to last server-confirmed snapshot, replays buffered inputs from then to now.

```cpp
struct PlayerInput {
    [[Engine::predicted_input]] bool fire;
    [[Engine::predicted_input]] Engine::vec2 move_axis;
};
```

Smoothing: position corrections are interpolated over a few frames, not snapped (unless delta is large → snap to avoid rubber-banding).

## Interpolation for remote entities

Non-owned entities rendered at `server_time - interp_delay` (default ~100ms). Interpolates between last two received snapshots. Jitter-tolerant. Extrapolation fallback for missing frames (limited, <50ms).

## Authority

- Default: server-authoritative for everything.
- Per-entity opt-in for client-authoritative (UI, voice chat, cosmetic effects):
  ```cpp
  world.set_authority(entity, Authority::ClientCosmetic);
  ```
- Server validates client-authoritative changes against anti-cheat rules (simple range checks; not the engine's main concern).

## Server build

Same project.dll, different editor mode: `Engine build --server`. Produces a headless binary that runs all systems except render / UI / input. Deployable to any box.

## Out of scope (v1)

- Matchmaking / lobby UI (game-specific)
- Anti-cheat beyond basic validation
- Rollback netcode (optional future module)
- WebRTC transport (future, for browser clients)
