# Plan: online detection that natives can't spoof

Status: built 2026-10-09 (`src/OnlineGuard.{h,cpp}`), compiles clean,
not live-tested. Offsets and patterns are HorseMenu's and RDR-Classes',
and need checking on build 1491.50 before anything relies on them.

## Implementation notes

- Signals 1-5, 7, 8 and 9 are built. Signal 6 sits out: its mask
  (`kOnlineBlockMask`) is empty until a live dump finds the online-only
  blocks. The log prints the allocated blocks at startup and at the
  switch to online; compare the two and fill in the mask.
- The new patterns are optional fields in `GamePointers` (like
  `GetNativeHandler`): a miss only makes that signal sit out (logged
  once). `NetworkPlayerMgr`/`NetworkObjectMgr` keep the address of the
  global, not the instance HorseMenu dereferences at scan time, since
  the instance is null outside a session.
- Signal 4 is online when the manager exists with a local player and a
  count of at least 1. Signal 5's object manager is online when the
  instance exists (no count field is mapped yet).
- Signal 8 follows jumps that stay inside RDR2.exe (game thunks) and
  flags a handler that is missing, lies outside the image, starts with
  `int3`, or jumps out of the image (`jmp rel`, `jmp [rip]`,
  `mov reg, imm64; jmp reg`, `push; ret`). A fault while reading counts
  as hooked (fail closed).
- The native check (9) is one input like the others: it can only be
  spoofed toward offline, so its "online" always counts. That also makes
  it the fallback when every memory signal is unknown.
- The ReceiveNetMessage detour sets the latch itself, and `Tick` runs a
  pass on the next frame instead of waiting for the 500 ms one.
- Logging: every signal's reading on the first pass, then each change
  (`OnlineGuard: <signal> offline -> online`), and on the switch which
  signals fired plus the raw values (net component threads by name,
  manager pointers, player count, global blocks, ReceiveNetMessage call
  count). The guard keeps ticking after the switch, so the log also
  records step 3 of the verification.
- Hardening: the latch is an inline atomic (`OnlineGuard::Latched()`),
  read by the main loop, the menu-open path in `script.cpp`, `Ui::Push`
  and `Menus::TickSettings` (hotkeys). Not done: the optional `.text`
  self-hash, and release signing.
- Until verification step 1 passes, a false positive in story mode
  suspends Rampagio for the session; the log names the signal.

## Goal

Replace `GameUtil::IsOnline` (`src/GameUtil.cpp`) with a check that an
online menu can't defeat by hooking natives. Today it's Rampage's check:

```cpp
NETWORK::NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(rage::Joaat("net_main_online"), -1, FALSE, 0)
```

That is one native. A menu loaded alongside Rampagio can swap its handler
in the native table to return `FALSE`, and the kill switch in
`script.cpp` (`Commands::Suspend`, `Ui::DisableAllToggles`, ...) never
fires. Rampagio then runs in Red Dead Online.

## Threat model

- **In scope:** a menu that hooks natives (handler swap or inline
  detour) so that SP trainers think they're offline.
- **Out of scope:** someone editing Rampagio's source and recompiling.
  It's open source, so that's always possible. Official releases are
  SHA-512'd by the releaser; a build that doesn't match isn't ours
  ("trustable source" builds vs. recompiles).
- **Partly in scope:** a menu patching Rampagio's own code in memory
  after load. We can't stop it, but we can make it cost more (see
  "Hardening").

**Rule for choosing signals:** read engine memory directly, never through
a native. Prefer state the game itself needs in order to stay in a
session, where lying would desync or drop the player.

## Signals

Each one gives online / offline / unknown (the pointer isn't resolved).
Any single "online" counts as online.

### 1. `net_main_online` thread, walked directly

`GamePointers::FindScriptThread(rage::Joaat("net_main_online"))` already
walks `ScriptThreads` (the `atArray<scrThread*>`) without a native. This
is the same check as today with the native removed. Hiding the thread
from the array would stop the script VM from ticking it.

Cost: one loop over the thread array comparing a 32-bit hash. Already
resolved at startup (`GamePointers::Get()` in `script.cpp`).

### 2. Network component on that thread

`scrThread::m_HandlerNetComponent` (0x6E8, `script/scrThread.hpp`) is a
`scriptHandlerNetComponent*`. Networked scripts have one; SP scripts
don't. Check it on the thread from signal 1, and also check whether *any*
running thread has one. That would catch a renamed or replacement net
script too.

To check: whether any SP script has a non-null net component (log every
thread's value in story mode once).

### 3. The local ped's `netObject`

`CDynamicEntity::m_NetObject` (0xE0, `entity/CDynamicEntity.hpp`). In
story mode the player ped has none. Online it's the `CNetObjPlayer` the
game syncs every frame; nulling it would break sync.

Getting the `CPed*` without a native: HorseMenu's `GetLocalPed` pattern
(`HorseMenu/src/game/pointers/Pointers.cpp`, "GetLocalPed"). Calling it
is a direct call into game code, not the native table.

To check: whether the SP ped's `m_NetObject` really is null on 1491.50,
including after a mission or cutscene that swaps the player ped.

### 4. `CNetworkPlayerMgr`

HorseMenu pattern "NetworkPlayerMgr". `netPlayerMgrBase` fields
(`network/netPlayerMgrBase.hpp`): `m_LocalPlayer` (0xE8, non-null in a
session) and `m_PlayerCount` (0x28C, at least 1). All player sync goes
through it.

Don't use "count > 1": solo and invite-only sessions are still online.

To check: whether the manager exists at all in SP (the pointer may be
null) and whether `m_LocalPlayer` stays set after going back to story
mode.

### 5. Session state

- HorseMenu's "IsSessionStarted" pattern resolves the `bool` the session
  natives read. It's a plain global, so a menu could write `false` to it,
  but the game reads it too. Use it as one signal among several, not on
  its own.
- `NetworkObjectMgr` (HorseMenu "NetworkObjectMgr"): a manager with
  registered objects means a session. Find a count or list field in
  `network/CNetObjectMgr.hpp` (only the vtable is mapped today).

### 6. Script global pages (to research)

`GamePointers::ScriptGlobals` is the global pages table. MP scripts
register global blocks that story mode never allocates, so their page
pointers should be null in SP. Find which block index belongs to the
online scripts: compare the page table in SP and online with the Global
Editor or a one-off log dump. Check the pointer, not a value; values are
easy to poke.

### 7. Latch from network traffic

MinHook detour on HorseMenu's "ReceiveNetMessage" (Rampagio already
uses MinHook in `NativeHooks.cpp`). The detour only sets an atomic
`g_SawNetTraffic = true` and calls the original. If session messages
arrive, the player is online, whatever the natives say. It also catches
the switch to online the moment it starts, instead of at the next poll.

Risk: hooking network code from an SP trainer. Keep the detour trivial,
and check story mode doesn't call it at all (log a counter).

## Tamper detection (fail closed)

8. **Native handler integrity.** Rampagio already resolves
   `GetNativeHandler`. For the natives online checks use
   (`NETWORK_IS_SCRIPT_ACTIVE_BY_HASH`, `NETWORK_IS_SESSION_STARTED`,
   `NETWORK_IS_IN_SESSION`, `NETWORK_IS_GAME_IN_PROGRESS`,
   `GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH`), check that
   the handler is inside RDR2.exe's image and doesn't start with a
   `jmp`/`push; ret` trampoline. If any is hooked, treat the session as
   online.
9. **Cross-check.** Keep the old native check as one input. If it says
   offline while a memory signal says online, it's being spoofed: treat
   as online and log it.

## How it fits together

- New `src/OnlineGuard.{h,cpp}`: `Tick()` and `bool IsOnline()`.
  `GameUtil::IsOnline` forwards to it (or `script.cpp` calls it
  directly).
- **Latched:** once online, stay online for the rest of the session.
  `script.cpp` already keeps the suspend for the session, so this only
  makes the detector agree with it.
- **Cadence:** signals 1-6, 8 and 9 every 500 ms (user, 2026-10-09).
  All of them together are a few dozen pointer reads plus one array
  walk, so that's far below anything measurable. Signal 7 latches
  instantly, so the 500 ms gap doesn't leave a window when a session
  starts.
- **Unknown isn't offline:** if a pointer didn't resolve, that signal
  sits out (logged once). If every signal is unknown, fall back to the
  native check, so a pattern break after a game update doesn't silently
  leave the mod unguarded.
- **Logging:** on the switch to online, log which signals fired. That
  gives the live-verification data and shows when a menu is spoofing
  (signal 9).

## Hardening (raises cost, doesn't prevent)

- Don't funnel everything through one `IsOnline()` a menu can patch to
  `ret 0`. Run the guard's result through a couple of places (the main
  loop, the menu open path, `Commands` apply) that each read the latched
  state.
- Optional: hash Rampagio's own `.text` at startup and recheck it on the
  500 ms tick; a mismatch counts as online.
- Releases: the SHA-512 next to each release. Authenticode signing would
  let users check it from the file's properties without comparing hashes.

## Verification (live, per CLAUDE.md)

1. Story mode: log every signal each tick for a few minutes, including
   through a mission, a cutscene and a player-ped swap. All must read
   offline; any false positive here is a blocker.
2. Go online: log which signals fire and in what order, and how long
   after the transition starts. All of them should end up online.
3. Back to story mode: confirm the latch holds (expected) and note which
   signals go back to offline, for the record.
4. Tamper test: hook `NETWORK_IS_SCRIPT_ACTIVE_BY_HASH` to return `FALSE`
   from a throwaway test ASI. Signal 8 should fire, and online the memory
   signals should still fire.

## Open questions

- Does anything in story mode run `net_main_online`, or a thread with a
  net component (signals 1-2)?
- Does story mode create `CNetworkPlayerMgr` / `NetworkObjectMgr` at all?
- Which global block index is online-only (signal 6)?
- Are HorseMenu's patterns valid on 1491.50? It targets the current
  build too, but check each one: `GamePointers` logs scan misses.
