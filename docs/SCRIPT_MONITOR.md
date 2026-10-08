# Script Monitor

Debug > Script Monitor: the first piece of the reworked Debug / Script
Tools area (2026-10-08). An ImGui window that replaces Rampage's Script
Monitor, Script Patcher, Script Loader and Script Terminator, which expect
the user to already know script hashes, function offsets and argument
counts. Built, not live-tested. In English only, on purpose.

## What it shows

- **Threads** (left): every script thread, by name, with thread id, state
  and average VM time per frame. Sortable, filterable by name or hash.
  Killed threads stay listed in red until their slot is reused; hashes
  without a name are amber.
- **Thread** tab: HorseMenu's details (state, program counter, frame and
  stack pointers, stack size) plus stack use, VM time (last frame, average,
  peak, calls per frame), how long it has run (since the monitor first saw
  it), the program's code size, native count, statics, globals block,
  arguments, string heap and reference count, and the exit reason of a
  killed thread. Kill (`TERMINATE_THREAD`), Pause/Resume (sets the
  thread's state; the previous state comes back on Resume, on going online
  and on eject), copy name/hash.
- **Functions** tab: every function in the script as the decompiled
  scripts number them, with Position, ENTER offset, argument and return
  counts and size. Click one, or type `func_688` / `0x3A7`, to hook it.
- **Natives** tab: a picker over alloc8or's natives (namespace combo, then
  search by name or hash), and the native hooks with call counts, last
  caller and last arguments (hover the name).
- **All Hooks** tab, and **Start Script** (HorseMenu's "New": name picker,
  stack size, free stack count).

## How

- **Names.** The game only keeps a script's name hash. `src/data/ScriptNames.inc`
  (`tools/extract_script_names.py`) holds `joaat(name)` for the 1,638
  1491.50 singleplayer scripts; `ScriptData::FindScriptName` is a hash map
  built on first use.
- **func_N.** The decompiler numbers functions by the order of their ENTER
  instructions (`__EntryFunction__` is 0). `ScriptBytecode::ListFunctions`
  walks the loaded program's code pages the same way, with the same operand
  lengths. `tools/check_script_functions.py` checks that walk against every
  decompiled 1491.50 script: 1,638 scripts, every func_N and Position
  matches. A Position is the ENTER offset or the end of the previous
  function (they differ where padding sits between functions); either is
  accepted, as is any offset inside the function.
- **Opcodes.** The decompiler's RDR1355 table, except that it has
  `PUSH_CONST_U32` (55) and `PUSH_CONST_F` (134) swapped:
  aberdeenpigfarm's func_140 (`return fParam0 * 1.8f + 32f`) is
  `22 01 03 00 00 66 00 86 66 66 E6 3F 24 86 00 00 00 42 71 50 01 01`.
  Hook payloads only use pushes checked the same way against tiny
  `return <const>;` functions: -1, 0, 1, 2 (8, 47, 9, 17), U8 (109),
  U32 (55), 0f (115) and F (134).
- **Function hooks** (Rampage's Script Patcher). Right after the ENTER, the
  hook writes a push of the value and `LEAVE argCount returnCount`, so the
  function returns at once. The patch goes into a private copy of the
  script's code pages that the VM runs instead (HorseMenu's ScriptPatches:
  `ScriptVM.h` swaps `m_CodeBlocks` for each VM call), so the game's pages
  are never written and removing the hook, or ejecting, restores the
  script exactly. Return choices are limited to what fits the function's
  return count: void for 0, true/false/INT/FLOAT/char* for 1, VECTOR3 for 3;
  larger returns (structs) can't be hooked. A payload must fit inside the
  function and not cross a 0x4000 code page; a too-short function says so.
  A char* return pushes a pointer to a copy of the text placed below 2 GB
  (the widest constant push is 32 bits); never freed.
- **Native hooks.** Through `NativeHooks.h` (per-program native table
  entries, so our own calls are unaffected), for the selected script or all
  of them. A pool of 64 handler slots (`ScriptHooks.cpp`), since a handler
  gets no user pointer. Modes: skip the native and return a value, call it
  and override the return, or only log. Return choices follow the return
  type in natives.h (`src/data/NativeList.inc`,
  `tools/extract_native_list.py`).
- **VM time.** The same `ScriptVM` detour times each VM call per thread id;
  `ScriptVM::EndFrame` (script loop) closes each frame.
- **Threads.** The window draws on the render thread from a snapshot the
  script thread takes each frame while it's open; buttons post work to the
  script thread with `MainThread::Post` (`src/MainThread.h`, run first in
  the main loop). Nothing on the render thread calls natives.

## The overlay

`src/overlay` is the user's HerbSpawner overlay (Vulkan first, DX12 as the
fallback, WndProc and cursor hooks), adapted: tools register with
`Overlay::Register`; no hook is installed until a tool first opens; while
one is open, the overlay takes keyboard and mouse, the native menu isn't
updated and `DISABLE_ALL_CONTROL_ACTIONS` keeps gamepad input from the
game. The menu key closes every tool (its key-up is swallowed too, so the
native menu doesn't see it). `Overlay::Shutdown` (DllMain) removes the hooks,
waits up to a second for the render and window threads to leave them
(`InHook` counter), then releases the renderer objects and ImGui; it leaves
ImGui allocated if a thread is still inside. ImGui (v1.92.9b) and
Vulkan-Headers are submodules.

## Live test checklist

1. `Rampagio.log`: the four GamePointers signatures, then "ScriptVM: hooked"
   and "Overlay: rendering with Vulkan/DX12" when the monitor first opens,
   on both renderers.
2. The window opens from Debug > Script Monitor, takes the mouse and
   keyboard (typing doesn't reach the game or the native menu's hotkeys),
   F5 and the X close it, and F5 doesn't then toggle the native menu.
3. Thread list: names resolve, states and VM times look sane, killed
   threads turn red. Pause/Resume a harmless ambient script.
4. Functions: compare a few func_N rows (Position, args, returns) with the
   decompiled `.c` of the same script.
5. Function hook: hook a known `BOOL func_N()` to false/true and see the
   effect; remove it and see the original behaviour back. Try VECTOR3 and
   void hooks, and a char* return on a function returning a label.
6. Native hook: Log only on a frequent native (call count, caller, args),
   then a Return hook, then Remove.
7. Start Script with a harmless script and stack size; Kill it.
8. Eject with hooks active and paused threads: scripts resume, game keeps
   running, re-inject works and the monitor opens again.
9. Going online closes the window and removes every hook.

## Not yet

- Restart and Force Cleanup (Rampage's SubScriptEditor), Force Cleanup All
  Scripts.
- Hooks aren't saved; a user-supplied function name table (labels for
  func_N per script) could be loaded from a file later.
- Locals/statics and globals editors (the Global Editor stays tabled).
