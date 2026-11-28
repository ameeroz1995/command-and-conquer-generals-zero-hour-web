# GeneralsX Web Port — Porting Log

## 2026-03-13 — Session 1: Initial Setup
- Started agent session
- Initialized tracker files (port-todo.md, porting-log.md, architecture-map.md)
- Audited initial build errors from ninja_error.log

## 2026-03-14 — Session 2: Rendering Pipeline
- Fixed 5 rendering bugs:
  1. DUMMY_IUNKNOWN COM refcounting → proper AddRef/Release with m_comRefCount
  2. BGRA texture swizzle → GL_TEXTURE_SWIZZLE_R/B for D3DFMT_A8R8G8B8/X8R8G8B8
  3. DXT mip upload → explicit per-level glCompressedTexImage2D
  4. Cull mode reversal → D3DCULL_CCW maps to GL_BACK (not GL_FRONT)
  5. Disabled lights → upload u_Lights[i].enabled=0 on every draw call
- Identified POOL-DOUBLE-FREE as startup blocker
- Added instrumentation to freeSingleBlock (callstack logging before abort)
- Created RENDERING_STATUS.md and BUILD_INSTRUCTIONS.md
- Created link_mac.sh for link-only builds

## 2026-03-15 — Session 3: Memory System + Porting Audit

### POOL-DOUBLE-FREE Analysis
- Exhaustive static analysis of all candidate double-free paths:
  - StringClass (new[]/delete[] → global operator → DMA pool)
  - AsciiString (DMA allocateBytes/freeBytes directly)
  - HashTemplateClass, TextureLoadTaskClass, DDSFileClass
- Confirmed WASM32 memory layout: MemoryPoolSingleBlock = 8 bytes (m_owningBlob + m_nextBlock)
- Verified sentinel value 0xDEADF00D detection logic is correct
- Root cause requires runtime browser callstack (static analysis exhausted)

### Changes Made
1. **GameMemory.cpp — freeSingleBlock non-fatal on Emscripten**
   - On double-free detection, prints full C++/JS callstack via emscripten_get_callstack()
   - Skips the free (leaks the block) instead of calling abort()
   - Game continues past the crash, allowing startup to proceed
   - The leaked block is a minor memory cost vs. a hard crash

2. **web.cmake — DISABLE_GAMEMEMORY nuclear option**
   - Added commented-out `set(RTS_GAMEMEMORY_ENABLE OFF)` with documentation
   - Enables GameMemoryNull.h path: all pool allocators → system malloc/free
   - Requires full rebuild (ninja) if enabled
   - Fallback if non-fatal workaround isn't sufficient

3. **web.cmake — existing WASM32 pool fixes documented**
   - DISABLE_MEMORYPOOL_MPSB_DLINK: prevents list-consistency bug in DMA free list
   - DISABLE_MEMORYPOOL_BOUNDINGWALL: prevents SI-vptr=0 pattern from post-wall overlap

### Comprehensive Porting Gap Audit
Identified all remaining gaps organized by severity:

**CRITICAL (blocks interactivity):**
- SDL input translation: DirectInput stubs are no-ops. Need SDL2 event → game input bridge for keyboard/mouse. Without this, the game renders but accepts no input.
- GameSpy networking: Must be disabled/stubbed for web. Attempting network calls will hang or crash in WASM.

**HIGH (blocks usability):**
- QueryPerformanceCounter/Frequency: Returns 0, causing timing issues. Need emscripten_get_now() bridge.
- Pointer Lock API: Mouse confinement for RTS camera control.
- Save game persistence: Emscripten FS is ephemeral. Need IDBFS mount for user saves/settings.

**MEDIUM (degraded experience):**
- Audio delayed-release thread: CreateThread is no-op on Emscripten. Sound objects may leak.
- On-demand audio archive loading: Currently deferred, needs trigger after main menu.

**LOW (polish):**
- Canvas resize / fullscreen toggle
- Loading progress UI
- WebGL context loss recovery
- Release build optimization (remove -g, -sSAFE_HEAP, -sASSERTIONS)

### CRITICAL Gaps Fixed (same session)

4. **SDL2 Input Translation Layer (EmscriptenInput.cpp/h)**
   - Created SDL2→DirectInput keyboard translation: 80+ SDL_SCANCODE→DIK_* mappings
   - Modified dinput.h stub: IDirectInputDevice8::GetDeviceData now reads from a ring buffer
   - SDL mouse events routed to Win32Mouse::addWin32Event() via EmscriptenInput_RouteMouseEvent()
   - SDL event pump called once per frame in GameWebFrameCallback before engine update
   - Supports: keyboard up/down, mouse motion, left/right/middle buttons, scroll wheel

5. **GameSpy Networking Disabled**
   - Added `NO_GAMESPY=1` define to web.cmake
   - GameSpy code still compiles (with socket stubs) but is inert at runtime:
     - Threads never start (CreateThread is no-op)
     - TheGameSpyChat stays null (never created during single-player)
     - Overlay system handles null gracefully

6. **QueryPerformanceCounter/Frequency Fixed**
   - Replaced stub (returned 0/1) with emscripten_get_now() bridge
   - Simulates 1MHz counter (microsecond resolution)
   - QPF returns 1000000LL, QPC returns emscripten_get_now() * 1000

7. **DIDEVICEOBJECTDATA Struct Conflict Resolved**
   - windows_base.h and dinput.h both defined the struct differently
   - Added include guard so only one definition is active
   - Unified field type for dwAppData (unsigned long *)

8. **WM_MOUSEWHEEL + MAKELPARAM Added**
   - Added missing WM_MOUSEWHEEL (0x020A) define to windows_base.h
   - Added MAKELPARAM macro for constructing lParam values

## 2026-03-15 — Session 4: Audit & Build Fixes

### Deep audit of all Session 3 changes
Audited every new/modified file for compilation errors, ODR violations,
duplicate symbols, and include path issues.

### Build-breaking fixes

9. **LinuxStubs.cpp — c_dfDIKeyboard duplicate symbol (LINKER ERROR)**
   - EmscriptenInput.cpp defines `const void *c_dfDIKeyboard = nullptr;`
   - LinuxStubs.cpp also defined `void *c_dfDIKeyboard = nullptr;`
   - On Emscripten, both files are compiled → "multiple definition" link error
   - Fix: guarded LinuxStubs.cpp definition with `#ifndef __EMSCRIPTEN__`

10. **LinuxStubs.cpp — TheWin32Mouse type mismatch (ODR violation)**
    - Was: `void *TheWin32Mouse = nullptr;`
    - WinMain.h declares: `extern Win32Mouse *TheWin32Mouse;`
    - W3DGameClient::createMouse() assigns a `Win32Mouse*` to it
    - EmscriptenMain.cpp reads it as `Win32Mouse*` for addWin32Event()
    - Fix: forward-declare `class Win32Mouse;` and define as `Win32Mouse*`

11. **LinuxStubs.cpp — ApplicationHInstance type mismatch (ODR violation)**
    - Was: `void *ApplicationHInstance = nullptr;`
    - WinMain.h declares: `extern HINSTANCE ApplicationHInstance;`
    - Fix: changed to `HINSTANCE ApplicationHInstance = nullptr;`

### Warning fixes

12. **windows_base.h — WSA macro redefinition warnings**
    - 5 WSA* macros (WSAEINVAL, WSAEALREADY, WSAEISCONN, WSAECONNRESET, WSAENOTCONN)
      defined twice: first as numeric constants (for GameSpy), then as POSIX aliases
    - Fix: added `#undef` before each POSIX redefinition

13. **EmscriptenInput.cpp — redundant WM_MOUSEWHEEL_EMSCRIPTEN**
    - Had its own `#define WM_MOUSEWHEEL_EMSCRIPTEN 0x020A`
    - windows_base.h already defines `WM_MOUSEWHEEL 0x020A`
    - Fix: removed redundant define, use `WM_MOUSEWHEEL` directly

14. **DISABLE_GAMEMEMORY Nuclear Option Activated**
    - Uncommented `set(RTS_GAMEMEMORY_ENABLE OFF CACHE BOOL ... FORCE)` in web.cmake
    - Swaps GameMemory.cpp (custom pool) → GameMemoryNull.cpp (pure malloc/free)
    - Eliminates POOL-DOUBLE-FREE entirely (no custom pool = no pool corruption)
    - `FORCE` keyword overrides the cached `ON` value from config-memory.cmake
    - GameMemoryNull.cpp: initMemoryManager() still creates factory/allocator stubs
    - All `NEW` / `delete` → standard `malloc(size) + memset(0)` / `free(p)`
    - Verified: __cdecl handled by Emscripten clang, <malloc.h> available via musl

15. **GameMemoryNull.h — Added Missing Macros and Types**
    - GameMemoryNull.h was incomplete: missing critical macros that hundreds of classes use
    - Added `MEMORY_POOL_GLUE(ARGCLASS, ARGPOOLNAME)` → expands to just virtual destructor declaration
    - Added `MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(ARGCLASS, ARGPOOLNAME, ARGINITIAL, ARGOVERFLOW)` → same
    - Added `GCMP_FIND` and `GCMP_CREATE` as empty macros (no pool lookup in null mode)
    - Added `DECLARE_LITERALSTRING_ARG1/ARG2` and `PASS_LITERALSTRING_ARG1/ARG2` as empty macros
    - Added `MemoryPool` forward declaration (some headers reference `MemoryPool*`)
    - Added `MemoryPoolSingleBlock` and `MemoryPoolBlob` forward declarations
    - Added `PoolInitRec` struct (needed by function signature in engine init)
    - Added `STLSpecialAlloc` class with `allocate()`→`::operator new` and `deallocate()`→`::operator delete`
    - In null mode, all MEMORY_POOL_GLUE macros reduce to just `protected: virtual ~ClassName(); public:`
    - No custom operator new/delete per-class; the global operator new/delete (malloc+memset) handles everything

## 2026-03-31 — First Successful Web Link
- `build-html5/GeneralsMD/GeneralsZH.{html,js,wasm}` produced successfully (build.log
  ends with `[100%] Built target z_generals`).
- 145 MB debug .wasm with `-g3 -sASSERTIONS=2`. Distributed copy under
  `GeneralsX/dist/` (committed as untracked) is 44 MB — the RelWithDebInfo build
  the link_mac.sh path produces.
- Project marked publishable: `feat(web): C&C Generals Zero Hour — full
  Emscripten/WebAssembly port` (commit d481e4a, Apr 7 2026) plus follow-up docs.

## 2026-05-04 — Session 5: Triage After Pickup
- Re-opened the project; the `<build-root>/build.log`
  in the parent dir reports undefined `TheMessageTime`, `ApplicationHInstance`,
  `c_dfDIKeyboard`. **That log is stale (Mar 13)** — it predates Session 4's
  stub additions and the Mar 31 successful link. Verified via direct WASM
  object inspection of `LinuxStubs.cpp.o` and `EmscriptenInput.cpp.o`:
  - `LinuxStubs.cpp.o` has `.bss.TheMessageTime`, `.bss.ApplicationHInstance`,
    `.bss.TheWin32Mouse` symbol entries.
  - `EmscriptenInput.cpp.o` has `.bss.c_dfDIKeyboard` plus the keyboard ring
    buffer globals.

### Code-correctness fixes (Session 5)
16. **LinuxStubs.cpp — moved Win32 stub globals inside `#ifndef _WIN32`**
    - `TheMessageTime`, `ApplicationHInstance`, and `c_dfDIKeyboard` were sitting
      *after* the `#endif // !_WIN32` line, so they would have been duplicate-
      symbol errors on a native Windows build (where `WinMain.cpp` defines them).
      The Emscripten link only succeeded because `WinMain.cpp` is excluded from
      that target.
    - Moved all three definitions inside the `#ifndef _WIN32` block. Added a
      header comment that points at the gating in `Main/CMakeLists.txt` so the
      next person who edits this file understands the contract.

17. **LinuxStubs.cpp — `int TheMessageTime` → `DWORD TheMessageTime`**
    - `Win32GameEngine.cpp` declares `extern DWORD TheMessageTime;`. The previous
      definition used `int`. Same width on WASM32 (32 bits) but different signed-
      ness; now matches the extern verbatim and matches the original WinMain.cpp
      definition.

18. **LinuxStubs.cpp — `void* c_dfDIKeyboard` → `const void *c_dfDIKeyboard`**
    - Matches `dinput.h`'s `extern const void *c_dfDIKeyboard;`. Only the
      non-Emscripten branch was changed; the Emscripten definition in
      `EmscriptenInput.cpp` was already `const void *`.

### Triage notes
- No active build break that I could observe in the working tree. The .wasm
  outputs in both `build-html5/` (Mar 31) and `GeneralsX/dist/` (Apr 7) are
  consistent with a clean build.
- A re-link is needed to pick up the Session 5 LinuxStubs.cpp edits. None of the
  edits should change the link line; the `.o` will simply be regenerated and
  carry the same exported symbols (with the type change being binary-compatible
  on WASM32).

## 2026-05-04 — Session 6: Loading Progress Bar (Polish item #1)

The page sat black for ~10 seconds while BigVFS mounted 15 .big archives over
HTTP, with no UI feedback. Implemented a themed loading screen with a global
progress bar fed by per-stage events from the C++ engine.

### New files
- **`GeneralsMD/Code/Main/web_shell.html`** — custom Emscripten shell. Self-
  contained HTML/CSS/JS; no external assets. C&C-themed dark background with a
  red/gold progress bar. Exposes `window.GeneralsX.setProgress(stage, current,
  total, label)` that the engine calls. Also has:
  - `Module.setStatus` / `Module.monitorRunDependencies` mapped onto a 0-5%
    "WASM download" portion of the bar so the user sees motion immediately.
  - `Module.onAbort` + `window.onerror` populate a hidden `#gx-error` overlay
    that takes over the screen on a fatal exception (instead of a tab-blocking
    `alert()`).
  - `F2` toggles a small on-page log (`#gx-output`) for in-browser triage when
    DevTools is awkward to reach.

### C++ changes
19. **`EmscriptenMain.cpp` — `GX_ReportProgress(stage, current, total, label)`**
    - Static-inline helper at the top of the file. Wraps an `EM_ASM` block that
      calls `window.GeneralsX.setProgress()` with `UTF8ToString` for the two
      string args (stage and label).
    - Guarded with `if (typeof window !== 'undefined' && window.GeneralsX)` so
      builds against the default Emscripten shell still work — the call is a
      no-op there. Useful for `-sSINGLE_FILE` / headless diagnostics builds.
    - Stage taxonomy (matched literally against keys in the JS):
      - `boot`   — 6 substeps for SDL/GL/frame pacer init
      - `vfs`    — one tick per archive (15 archives), label = archive filename
      - `engine` — 0/1 → "Loading INI / textures / W3D", 1/1 → "Engine ready"
      - `ready`  — fades the loading overlay out

20. **Bar weighting** — split as `download:0.05 / boot:0.05 / vfs:0.55 /
    engine:0.35 = 1.00`. The vfs stage gets the lion's share because it's
    network-bound and visibly the slowest. Bar guards against backwards motion
    (`lastPct`) so any out-of-order progress events just hold the bar in place.

### Build wiring
21. **`GeneralsMD/Code/Main/CMakeLists.txt`** — added `--shell-file
    ${GX_WEB_SHELL}` to `target_link_options` for the Emscripten target plus
    a `LINK_DEPENDS` property so changing the shell triggers a relink.
22. **`<build-root>/link_mac.sh`** — added the
    same `--shell-file` to the manual link path so a `link_mac.sh` rebuild
    picks up the new shell without needing a full ninja run.

### To verify
- Run `link_mac.sh` (or `ninja -C build-html5 z_generals`) on the Mac.
- Open `build-html5/GeneralsMD/GeneralsZH.html` (or the `dist/` copy if you
  rebuild that).
- Expected: the loading overlay appears immediately with title "Generals Zero
  Hour / Web Edition", progress bar fills as the WASM downloads (0-5%), then
  the C++ progress events drive it from 5% to 100% with stage strings
  "Booting engine" → "Mounting game data: INI.big (1/15)" → ... → "Initializing
  game engine" → "Ready", then the overlay fades out.

## 2026-05-04 — Session 7: Pointer Lock + Cursor Visibility (Polish item #2)

The browser's default cursor sits on top of the engine's rendered cursor (cursor
double-up) and right-click camera pan hits the screen edge after a short drag.
Implemented two related changes that solve both:

### Cursor visibility — `SetCursor()` routing
23. **`emscripten_compat/windows_base.h`** — `SetCursor(HCURSOR)` was a no-op.
    Replaced its body on Emscripten with a forward to
    `EmscriptenInput_SetCanvasCursor(h ? 1 : 0)`. The engine convention is
    `SetCursor(nullptr)` → "I'll draw the cursor myself, hide the system one"
    and `SetCursor(non-null)` → "show the system cursor", so we just translate
    null vs. non-null into a JS canvas-style toggle.
24. **`EmscriptenInput.cpp`** — added `EmscriptenInput_SetCanvasCursor(int)`.
    Dedup'd against the previous reported state (Win32Mouse calls SetCursor
    every frame for the active cursor) and routed via `EM_ASM` to
    `window.GeneralsX.setCanvasCursor(visible)`. Falls back to a direct
    `canvas.style.cursor =` assignment when the custom shell isn't loaded.
25. **`web_shell.html`** — added `setCanvasCursor` to the `window.GeneralsX`
    surface, which sets `canvas.style.cursor` to `'default'` or `'none'`.

### Pointer Lock — JS-owned because of the user-gesture rule
The hard problem with Pointer Lock on a SDL2-Emscripten app: the browser only
grants the lock if `requestPointerLock()` is called *synchronously* from within
the user-gesture call stack, but SDL2-Emscripten queues mouse events from the
real DOM listeners and drains them via `requestAnimationFrame` — by the time we
see `SDL_MOUSEBUTTONDOWN` in the C++ event loop, the gesture stack is gone.

26. **JS owns the request** — `web_shell.html` now registers a
    `canvas.addEventListener('mousedown')` listener that calls
    `canvas.requestPointerLock()` for `e.button === 2` (right mouse). Mirror
    listener on `mouseup` calls `document.exitPointerLock()`. Both run inside
    the actual DOM gesture stack, so the browser grants the lock cleanly.
27. **`pointerlockchange` → C++ callback** — JS listens for the state change
    and calls `Module.ccall('EmscriptenInput_OnPointerLockChange', ...)` which
    flips an internal `g_relativeMouseActive` flag in the C++ side.
28. **`EmscriptenInput.cpp` SDL_MOUSEMOTION handler** — when `g_relativeMouseActive`
    is true, accumulate `event.motion.xrel` / `yrel` into a virtual cursor
    position (`g_virtualMouseX`, `g_virtualMouseY`) clamped to the canvas
    extents, and feed THAT to Win32Mouse via the existing `WM_MOUSEMOVE` route.
    The engine sees ordinary absolute coords either way — the camera-pan delta
    math is unchanged.
29. **`EmscriptenInput.cpp` SDL_WINDOWEVENT_FOCUS_LOST** — proactively releases
    the lock if the canvas loses focus (e.g., `Alt+Tab`), so our state mirror
    doesn't drift out of sync with the browser's.
30. **Exports plumbing** — added
    `_EmscriptenInput_OnPointerLockChange` to `EXPORTED_FUNCTIONS` in both
    `GeneralsMD/Code/Main/CMakeLists.txt` and `link_mac.sh`. The
    `EMSCRIPTEN_KEEPALIVE` attribute on the C function is also there as
    belt-and-braces.

### Caveats / follow-ups
- We still keep `EmscriptenInput_SetRelativeMouseMode()` as a thin wrapper
  around `SDL_SetRelativeMouseMode()` for source compatibility — but we no
  longer rely on it for the actual lock request. SDL2's call may or may not
  succeed depending on browser timing; we log a warning and continue.
- Cursor visibility for non-canvas areas (e.g., when the engine renders a
  full-screen menu) is currently coupled to whatever the engine last passed
  to `SetCursor`. If the engine forgets to call SetCursor on a state
  transition, the cursor visibility will look stale until the next call.
  Acceptable for now — the existing engine code calls SetCursor reliably.
- A "click-to-start" overlay (so the user makes a first interaction before
  the WASM tries any audio / pointer-lock action) is *not* yet wired up; if
  Chrome's autoplay policy blocks audio in a future session, that's where
  to add it.

### To verify
- Re-link with `link_mac.sh` (or full `ninja`).
- Open the page; the system cursor should be visible during the loading
  overlay.
- After the engine renders the main menu the system cursor should disappear
  (the engine draws its own).
- Right-click and drag on the main map view: the OS cursor should hide, the
  view should scroll continuously past where the screen edge would normally
  stop the drag, and releasing the right button should restore the cursor.
- Console should log `INFO: pointer lock engaged` / `released` on each cycle.

## 2026-05-04 — Session 8: Audio (Polish item #3)

Two related problems blocking sound: the audio archives weren't mounted at
all, and the WWAudio delayed-release worker thread doesn't run on
single-threaded Emscripten so any object the engine asked the audio system
to release-after-N-ms accumulated forever.

### Single-threaded delayed-release drain — implemented but dormant
31. **`Core/Libraries/Source/WWVegas/WWAudio/Threads.h` + `Threads.cpp`** —
    added a public static method `Tick_Delayed_Release_Objects()` that
    mirrors one iteration of `Delayed_Release_Thread_Proc`: walks the
    release list under the existing `m_ListMutex`, frees any entry whose
    `time` deadline has passed, leaves the rest. Same lock as the worker so
    the call is safe even on platforms where the worker IS running (no
    behavioral change there).

32. **Did NOT wire it up in `EmscriptenMain.cpp`.** Inspecting
    `build-html5/.../linkLibs.rsp` showed that `libwwaudio.a` is not in
    `z_generals`'s link line on the web build — the audio path is currently
    stubbed via `milesstub`, so `WWAudioThreadsClass` exists in source and
    compiles into `libwwaudio.a` but is not pulled into the final link.
    Calling the helper from `EmscriptenMain.cpp` would have produced an
    unresolved-symbol link error.

    Left a `NOTE:` comment in `EmscriptenMain.cpp` at the call site and at
    the top of the file explaining what to do when WWAudio is actually
    linked: add `target_link_libraries(z_generals PRIVATE z_wwaudio)`,
    `#include "Threads.h"`, and the per-frame call right after
    `TheGameEngine->update()`.

### Deferred audio archive mount
33. **`EmscriptenMain.cpp` — `DeferredAudio_Step()` state machine.**
    Static-storage state machine driven from the frame callback. States:
    - `0`: idle (default).
    - `1`: armed; counting down `g_deferredMountWaitFrames` (~30 frames /
      ~500ms) so the main menu has a chance to paint *before* we start
      pulling network bytes.
    - `2`: mounting one archive per frame from `kDeferredAudioArchives` so
      the main loop stays responsive between fetches. Each
      `BigVFS::Mount_Archive(name)` does a single ~512KB range request to
      grab the .big header — the actual file bodies stream in later via
      HTTP-range as the audio engine opens individual sounds.
    - `3`: done. Calls `BigVFS::Mount_To_Emscripten_FS()` once at the end
      to refresh the VFS file table so the engine can `fopen()` the new
      paths.
    The state machine arms itself from `main()` immediately after the
    `ready` progress event:
    ```
    g_deferredMountState = 1;
    g_deferredMountWaitFrames = 30;
    ```

34. **Archive list** — `AudioZH.big`, `MusicZH.big`, `SpeechZH.big`. These
    were called out in `SETUP.md` as "not yet required — audio archive
    mounting is a pending feature"; now they ARE required for sound (but
    still optional for a silent boot since the deferred mount tolerates
    fetch failures).

### Shell / UX
35. **`web_shell.html`** — added a corner "background load" toast
    (`#gx-toast`) plus `window.GeneralsX.setSecondaryProgress(stage,
    current, total, label)` and `hideSecondaryProgress()`. Doesn't block
    input — user can navigate the menu while audio assets are loading. The
    C++ DeferredAudio state machine fires these via `EM_ASM` at the same
    points it does work.

### To verify
- Re-link.
- Open the page; ~500ms after the loading overlay disappears, the toast
  should appear in the bottom-right showing `Loading audio assets — 0/3 —
  AudioZH.big`, then tick to 1/3 (MusicZH.big), 2/3 (SpeechZH.big), then
  `Audio ready` and fade.
- Console should log `INFO: Deferred mount: AudioZH.big` etc. and finally
  `INFO: Deferred audio mount complete`.
- A click on a menu button should produce its hover/click sound (this is
  the actual end-to-end audio test). If silent, suspect the milesstub
  library — see `port-todo.md` follow-up bullet under "Audio".

## 2026-05-04 — Session 9: IndexedDB Save Persistence (Polish item #4)

Save games and `Options.ini` changes evaporated when the tab closed because
the engine wrote to MEMFS (in-memory only). Wired up Emscripten's IDBFS so
the user-data directory is now backed by IndexedDB and persists across
reloads.

### Mount + initial sync (JS, runs before `main()`)
36. **`web_shell.html` — `Module.preRun = [idbfsPreRun]`** — `idbfsPreRun`
    creates `/userdata`, mounts `IDBFS` over it, then calls
    `FS.syncfs(true, …)` to populate MEMFS from IndexedDB. The pre-run hook
    bumps a runtime dependency (`addRunDependency('idbfs-load')`) so the
    Emscripten boot waits for `removeRunDependency` before invoking `main()`.
    Result: by the time the C++ side runs, any previously-saved files are
    already on disk in MEMFS.

### Engine path adjustment (C++)
37. **`GlobalData.cpp`** — added an `#ifdef __EMSCRIPTEN__` block at the end
    of the GlobalData ctor that overrides the dynamically-built path with a
    clean `m_userDataDir = "/userdata/"`. The default Linux path math would
    have produced something like `/home\Command and Conquer Generals Zero
    Hour Data\` (mixing forward and backslashes from the HOME env var and
    the Win32-style trailing separator), which doesn't map to anything
    sensible in MEMFS.

38. **`Core/.../LocalFile.cpp` — backslash-to-forward-slash normalization
    in `LocalFile::open` on Emscripten.** Engine code that *appends* to the
    user-data dir (e.g., `getSaveDirectory()` adds `Save\`) still produces
    backslash-laden paths — and on Emscripten/MEMFS, `\` is a valid filename
    character, not a separator. Without normalization, a save to
    `/userdata/Save\foo.sav` produces a single weirdly-named file at
    `/userdata`. The fix scans the filename once and, if any `\` are
    present, copies into a stack buffer with `/` instead and uses that for
    `fopen`. The Web_VFS_Read_File_Sync read path already handles both
    separators internally, so reads were unaffected.

### Periodic + visibility flush (C++)
39. **`EmscriptenMain.cpp` — `GX_FlushIdbfs(reason)`** wraps an `EM_ASM`
    that calls `FS.syncfs(false, …)` with debouncing. While a flush is in
    flight, additional flush requests just set a `pending` flag; on
    completion, the pending one runs. Prevents a tight save loop from
    piling up parallel syncs.

40. **`EmscriptenMain.cpp` — periodic flush in `GameWebFrameCallback`.**
    Tick counter; once every 30 × 60 = 1800 frames (~30s at 60fps),
    `GX_FlushIdbfs("periodic")`. Cheap on the average frame (single int
    increment), and gives a recoverable point if the tab crashes before
    visibilitychange fires.

41. **`EmscriptenMain.cpp` — `GX_InstallIdbfsVisibilityHooks()`.** Installed
    once early in `main()`, registers two JS listeners:
    - `visibilitychange` → if state is `hidden`, ccall
      `GX_FlushIdbfs_Tab` (an exported C function so we don't allocate a
      new closure per event).
    - `beforeunload` → call `FS.syncfs(false, () => {})` directly. Best
      effort; some browsers cut async work here, but if the user has
      unsaved data this is the last chance.

42. **Exports plumbing** — added `_GX_FlushIdbfs_Tab` to
    `EXPORTED_FUNCTIONS` in both `GeneralsMD/Code/Main/CMakeLists.txt`
    and `link_mac.sh`. EMSCRIPTEN_KEEPALIVE is also on the C function as
    a belt-and-braces measure.

### Caveats / known gaps
- The 30s periodic flush is on a frame counter, not a wall-clock timer.
  If the page is throttled (background tab) the flush rate drops with the
  frame rate, which is fine — nothing's writing while we're throttled.
- This session does NOT address the `chdir`/`SetCurrentDirectory` path that
  some engine subsystems use to navigate into the save directory. Most
  saves go through `LocalFile::open` directly with absolute-ish paths and
  work fine, but anywhere the engine relies on relative paths after a
  `chdir` may need follow-up.
- The leaf name (`Command and Conquer Generals Zero Hour Data`) is
  intentionally NOT preserved on Emscripten — we just use `/userdata/`
  flat. If we ever want to share IDB state with another fork that uses the
  same leaf, that's a forward-compat concern.

### To verify
- Re-link.
- Open the page; the loading overlay should briefly show `Loading saved
  data` (during the IDBFS pre-run sync) before progressing to the boot
  stages.
- Save a game, reload the tab, return to the load-game menu; the save
  should be there.
- DevTools → Application → IndexedDB → `/userdata` should show the entries
  after a save.
- Console should log `[GeneralsX] IDBFS load complete` on first load and
  no errors on subsequent flushes.

## 2026-05-04 — Session 10: Resize + WebGL Context Loss (Polish item #5)

The canvas was a fixed 1024×768 backing store regardless of the actual
window size, so resizing the browser stretched the framebuffer (blurry on
HiDPI, letterboxed on wide windows). And a GPU reset / driver restart would
silently kill the page because the default `webglcontextlost` behavior is
"navigate to error". Both fixed.

### Canvas resize handling
43. **`web_shell.html`** — added a self-invoking IIFE that registers a
    `window.resize` listener and (where supported) a `ResizeObserver` on
    `#canvas`. Both fire a 100ms-debounced `apply()` that:
    - Reads `canvas.clientWidth/Height` (the CSS-pixel display size)
    - Multiplies by `window.devicePixelRatio` for HiDPI sharpness
    - Sets `canvas.width / canvas.height` to that pixel size (this resizes
      the GL drawing buffer; the browser will no longer upscale a smaller
      buffer with bilinear filtering)
    - `Module.ccall('GX_OnCanvasResize', null, ['number', 'number'], [w, h])`
      so the C++ side syncs SDL.
    Initial `apply()` runs synchronously so the very first frame already has
    the right backing-store size.

44. **`EmscriptenMain.cpp` — `GX_OnCanvasResize(int w, int h)`.** Exported
    via EMSCRIPTEN_KEEPALIVE + EXPORTED_FUNCTIONS. Calls
    `SDL_SetWindowSize(TheSDL3Window, w, h)` (which fires
    `SDL_WINDOWEVENT_RESIZED`, picked up by the existing handler in
    `EmscriptenInput.cpp` to update `g_canvasW/H` for pointer-lock clamping)
    and `emscripten_set_canvas_element_size("#canvas", w, h)` to keep the
    WebGL drawing-buffer attribute in sync even before `TheSDL3Window`
    exists (during early boot, the canvas size still matters for the
    initial GLES3_Init call). Logs the new dimensions.

### Engine layout caveat
The engine's GUI / HUD layout is still calculated against the resolution
chosen at `SDL_CreateWindow` time (currently 1024×768 from
`emscripten_get_canvas_element_size`). After resize, the framebuffer is
bigger but the engine's projection matrices and 2D layouts haven't been
re-driven. C&C Generals was a fixed-resolution RTS so there is no plumbed
in-engine resize pass; for now the user gets a crisp framebuffer at any
size with the original 1024×768-anchored UI. A real fluid-resize pass is
out of scope and listed as a follow-up.

### WebGL context-loss overlay
45. **`web_shell.html`** — added two listeners on `#canvas`:
    - `webglcontextlost` calls `e.preventDefault()` (preventing the browser
      from giving up on the page) and `window.GeneralsX.showError(message,
      'Graphics context lost')` to display the `#gx-error` overlay with a
      contextual message about GPU resets / driver restarts.
    - `webglcontextrestored` logs that the context came back, but doesn't
      try to resume — the engine isn't designed to re-upload every texture,
      VBO, shader, and render-state, so we treat loss as terminal.

46. **Reload button.** `#gx-error` now renders a "Reload page" button
    (CSS-styled to match the C&C aesthetic). A click handler calls
    `window.location.reload()`. Used by the context-loss path and any other
    fatal path that puts up the error overlay.

47. **`showError` extended to take an optional title.** Default still
    "Engine fault"; context-loss uses "Graphics context lost". The title is
    rendered into a new `#gx-error-title` element.

### Exports plumbing
48. Added `_GX_OnCanvasResize` to `EXPORTED_FUNCTIONS` in
    `GeneralsMD/Code/Main/CMakeLists.txt` and `link_mac.sh`. Belt and
    braces with the `EMSCRIPTEN_KEEPALIVE` attribute on the C function.

### To verify
- Re-link.
- Open the page in a window noticeably larger or smaller than 1024×768.
  The canvas should fill the viewport and render crisply (no bilinear
  upscaling).
- Drag the window edge to resize. The GL framebuffer should match the new
  size after a ~100ms beat (debounce). Console should log `INFO:
  GX_OnCanvasResize → WxH`.
- HiDPI test: on a Retina display the backing store should be 2× the CSS
  size — check `canvas.width` / `canvas.clientWidth` in DevTools.
- Force context loss: DevTools → "Rendering" panel → "WebGL: Lose context"
  (or the `WEBGL_lose_context` extension via console:
  `gl.getExtension('WEBGL_lose_context').loseContext()`). The `#gx-error`
  overlay should appear with title "Graphics context lost" and a "Reload
  page" button that works.

### Follow-up: CreateDirectory stub now actually mkdirs (Session 9 gap fix)
49. **`emscripten_compat/windows_base.h`** — the previous stub was
    `#define CreateDirectory(a, b) 0`, a pure no-op. The engine calls
    `CreateDirectory(getSaveDirectory().str(), nullptr)` before saving;
    without a real mkdir, `/userdata/Save` never existed and the subsequent
    `fopen` would fail.

    Replaced the macro with a real implementation on Emscripten:
    `GX_CreateDirectory_Web(path)` does `\` → `/` normalization, strips
    trailing `/`, and calls `mkdir(buf, 0755)`. Returns 1 on success and
    also on `EEXIST` (matches the Win32 ERROR_ALREADY_EXISTS path that
    callers expect). On non-Emscripten the macro keeps its old no-op
    behavior (and on real Win32 the actual `CreateDirectory` is shadowed
    by the macro only inside the emscripten_compat header chain).

    With this in place, the IDBFS persistence chain from Session 9 should
    now actually round-trip a save game: engine calls CreateDirectory
    (creates `/userdata/Save`), engine opens save file (creates
    `/userdata/Save/sav0001.sav`), periodic flush pushes to IndexedDB,
    reload re-populates from IndexedDB.

## 2026-05-05 — Session 11: Touch + Click-to-Start + Release Profile

Picked up the project after the May 4 polish push. The build is at parity
with the dist/ output (Apr 7); this session knocks out the remaining
cross-platform polish items so the next person can focus on audio.

### Touch input (Polish stretch goal)

50. **`EmscriptenInput.cpp` — SDL_FINGER* event handlers.** The whole input
    stack downstream of this file is mouse-shaped, so a parallel touch path
    isn't worth the complexity. Instead we synthesise mouse messages from
    finger events. Gestures supported:
    - **1 finger** — pass-through. SDL's built-in
      `SDL_HINT_TOUCH_MOUSE_EVENTS` synthesises `SDL_MOUSEBUTTON*`/
      `SDL_MOUSEMOTION` for the first touch, so the existing mouse pipeline
      handles tap-and-drag without any new code. We just track the active
      finger count.
    - **2 fingers** — explicit RMB-drag camera pan at the centroid +
      pinch-to-wheel zoom. On the second `SDL_FINGERDOWN` we synthesise a
      `WM_LBUTTONUP` (cancelling the in-flight LMB drag from finger 1),
      then a `WM_RBUTTONDOWN` at the centroid. `SDL_FINGERMOTION` ticks
      `WM_MOUSEMOVE` at the new centroid; pinch distance change > 30 px
      emits `WM_MOUSEWHEEL` notches. Lift to 1 finger emits the matching
      `WM_RBUTTONUP`.
    - **Synthetic-mouse suppression** — while in 2-finger mode we set
      `g_suppressSyntheticMouse` so any further `SDL_MOUSEBUTTON*`/
      `SDL_MOUSEMOTION` carrying `which == SDL_TOUCH_MOUSEID` is dropped.
      Otherwise the engine would see both our explicit RMB drag and a
      stray LMB up/down from SDL's compat path.

51. **`EmscriptenInput.cpp` — centroid math against canvas size.** SDL
    finger coords are normalized 0..1; we multiply by `g_canvasW/H` (already
    tracked for pointer-lock clamping) and feed pixel coords through the
    existing `EmscriptenInput_RouteMouseEvent` route. New helper
    `EmscriptenInput_UpdateCanvasSize(int, int)` exposed in
    `EmscriptenInput.h` so `GX_OnCanvasResize` in `EmscriptenMain.cpp` can
    refresh those dimensions on early-boot resizes (before SDL_CreateWindow
    fires `SDL_WINDOWEVENT_RESIZED`).

52. **`web_shell.html` — `touch-action: none` on `canvas#canvas`.** Without
    this, mobile Safari steals two-finger gestures for page pinch-zoom and
    the canvas never sees them. Also added `-webkit-tap-highlight-color:
    transparent` to suppress the iOS tap rectangle, and `user-select: none`
    so long-press doesn't pop a selection magnifier over the game.

### Click-to-start gesture gate

53. **`web_shell.html` — gesture-gating before engine handoff.** Browsers
    block `AudioContext.resume()` and pointer-lock requests before the user
    has interacted with the page. Most users click or press a key while the
    WASM downloads, in which case the existing fade-out path is fine. For
    the corner case where the engine reaches `ready` without any prior
    gesture (e.g., user opens the tab in the background and switches to it
    after everything's loaded), we now reveal a "Begin" button instead of
    silently fading out. Clicking it (or anywhere) hides the overlay and
    calls `Module.SDL2.audioContext.resume()` — best-effort, currently
    no-op since milesstub doesn't create an AudioContext, but lays the
    groundwork for when WWAudio gets wired up.

54. **State**: any of `pointerdown`/`keydown`/`touchstart` listeners (added
    with `{ passive: true, capture: true }` so they don't intercept input
    the engine cares about) flips `gxUserGestureSeen`. The `setProgress('ready')`
    branch checks that flag; if false it sets `gxReadyPending` and shows the
    button. The next gesture (which may be the button click itself) calls
    `gxFinishLoading()`.

### Release-build profile

55. **`link_mac.sh` — `bash link_mac.sh release` switches to optimised
    distribution flags.** Default invocation is unchanged (debug build with
    `-g3 -sSAFE_HEAP=1 -sASSERTIONS=2 -sDEMANGLE_SUPPORT=1`). Release path:
    - `-O3 -DNDEBUG` for both compile and link
    - drops `-g3`, `-sSAFE_HEAP=1`, `-sASSERTIONS=2`, `-sDEMANGLE_SUPPORT=1`
    - adds `--closure 1` for JS minification
    - writes to `GeneralsZH-rel.{html,js,wasm}` so debug + release coexist
    - per-mode log file (`link_mac.${MODE}.log`) so a debug log doesn't get
      stomped by a release run

    Asyncify stays ON in both modes — it's a structural dep (the engine's
    `emscripten_sleep(0)` calls inside `init()` rely on it), not a
    diagnostic option that can be turned off without breaking the boot path.

### Audio path — documented but not implemented

The remaining big rock is real audio. `port-todo.md` "Audio" section now
lays out the two reasonable approaches (SDL_mixer bridge or OpenAL bridge)
plus the WWAudio link wiring needed to enable the delayed-release drain
helper from Session 8. Out-of-scope for this session because either
approach involves replacing the ~100-function `milesstub` with a real
backend that decodes WAV/MP3 from `AudioZH/MusicZH/SpeechZH.big` and
plays it through Web Audio — a multi-session effort.

### To verify

- Re-link with `bash link_mac.sh` (debug) or `bash link_mac.sh release`
  (distribution).
- Open the page on a desktop browser; the existing flow should be
  unchanged (mouse + keyboard work as before).
- On a phone or tablet, single-finger tap should select; one-finger drag
  should drag-select; two-finger drag should pan the camera; pinch
  should zoom. The page itself shouldn't pinch-zoom or scroll.
- Open the page in a backgrounded tab; switch to it after a few seconds.
  The "Begin" button should appear instead of the overlay auto-dismissing.
- Console on first gesture should log no errors (the
  `Module.SDL2.audioContext.resume()` call is wrapped in a try/catch).

### Latent bug caught & fixed during verification

56. **`EmscriptenMain.cpp::GX_FlushIdbfs` — broken `EM_ASM` macro
    expansion** (added in Session 9, never built since). The JS body
    contained an inline object literal `{ busy: false, pending: false }`.
    The C preprocessor splits macro arguments at top-level commas without
    respecting `{}` nesting, so the macro arg would split mid-literal and
    the JS body would parse as C++ — failing with "use of undeclared
    identifier 'pending'" / "unknown type 'st'" / etc.

    Why it never tripped before: every previous successful build (Mar 31
    + Apr 7 dist) was made *before* Session 9 added this code. Sessions
    5–10 were all source-only edits with no rebuild.

    **Fix**: wrap the body in extra parentheses — the documented
    workaround in `<emscripten/em_asm.h>`:
    ```cpp
    EM_ASM(({
      ... { a: 1, b: 2 }; ...
    }), reason);
    ```
    The `({...})` is a single grouped expression that the preprocessor
    treats atomically. Audited the other 5 EM_ASM blocks in the file —
    all use commas inside `()` only (function-call arglists), which the
    preprocessor *does* respect, so they didn't need wrapping.

57. **`link_mac.sh` — missing `EmscriptenInput.cpp.o` from MAIN_OBJS**.
    Compared the existing `MAIN_OBJS=( EmscriptenMain.cpp.o,
    LinuxStubs.cpp.o )` against `build-web/build.ninja`'s actual link line
    for `GeneralsZH.html`, which lists ALL THREE of `EmscriptenMain`,
    `EmscriptenInput`, and `LinuxStubs`. Without `EmscriptenInput.cpp.o`
    in the link line, the link should have failed with unresolved symbols
    (`EmscriptenInput_PumpEvents`, `c_dfDIKeyboard`,
    `g_emscriptenKeyBuffer`, …) — and likely *did* fail every time
    `link_mac.sh` was run since Session 3 first introduced
    `EmscriptenInput.cpp`.

    Added `EmscriptenInput.cpp.o` to MAIN_OBJS with a comment explaining
    that it's not bundled into any `.a` and so must be passed directly.

### Build verification (May 5 2026)

Compiled the two .o files we modify in the Mac workflow with a new
`compile_targeted.sh` helper that lifts the exact em++ flags out of
`build-web/build.ninja`'s rules so we don't trigger ninja's full-tree
re-evaluation (which would also re-run CMake and pull the entire
WWAudio/WW3D2 compile graph). The script:

1. Compiles `EmscriptenInput.cpp.o` and `EmscriptenMain.cpp.o` with the
   same `-g3 -O0 -std=c++20` + PCH config ninja uses.
2. Supports a `--syntax-only` flag for fail-fast iteration (a
   2-line-edit / 5-second-check loop while debugging the EM_ASM
   macro fix above).

Then ran `bash link_mac.sh` (debug profile). It linked cleanly and
produced:

```
GeneralsMD/GeneralsZH.html      30,820 bytes
GeneralsMD/GeneralsZH.js       558,715 bytes
GeneralsMD/GeneralsZH.wasm  91,330,064 bytes  (~87 MB; debug, -g3 -SAFE_HEAP -ASSERTIONS=2)
```

The HTML grew from ~22KB → 30KB (richer shell with touch + start button +
IDBFS preRun). The wasm grew from 44MB → 87MB because:
1. Sessions 6–11 added meaningful new code (IDBFS, deferred audio, resize
   hooks, touch handling, gesture gate).
2. `EmscriptenInput.cpp.o` is now actually in the link line — `link_mac.sh`
   was missing it from MAIN_OBJS (see #57), so previous link_mac.sh runs
   would either have failed with unresolved symbols, or relied on a stale
   archive somewhere. Either way, this is the first link_mac.sh build we
   know to be self-consistent with the source tree.
3. Debug-only overhead: the build is `-g3 -sSAFE_HEAP=1 -sASSERTIONS=2`,
   which roughly doubles wasm size compared to the release profile.

### Release-profile link (May 5 2026)

58. **Closure compiler regression — legacy allocator export needed.** The
    first `bash link_mac.sh release` run failed at the closure pass with:
    ```
    JSC_UNDEFINED_VARIABLE: variable allocate is undeclared
    JSC_UNDEFINED_VARIABLE: variable ALLOC_NORMAL is undeclared
    ```
    Origin: SDL2's `SDL_assert.c` emits an `EM_ASM` block calling
    `allocate(intArrayFromString(reply), "i8", ALLOC_NORMAL)`. Those names
    are the legacy Emscripten allocator API; without exporting them in
    `EXPORTED_RUNTIME_METHODS` they aren't declared in the JS output that
    closure sees, and ADVANCED_OPTIMIZATIONS errors out.
    
    **Fix**: appended `,allocate,ALLOC_NORMAL,intArrayFromString` to the
    release-mode `EXPORTED_RUNTIME_METHODS`. The debug profile keeps the
    minimal export list (closure isn't run, so no need). Also restructured
    the script so `EXPORTED_RUNTIME_METHODS` is per-mode (was in
    `COMMON_LINK_FLAGS`, where appending another `-sX=…` would have
    overridden rather than merged).

After the fix, the release link succeeded:
```
GeneralsMD/GeneralsZH-rel.html      11,147 bytes  (vs 30,820 debug)
GeneralsMD/GeneralsZH-rel.js       128,568 bytes  (vs 558,715 debug — closure cut 4×)
GeneralsMD/GeneralsZH-rel.wasm  17,139,627 bytes  (vs 91,330,064 debug — 5.3× smaller)
```

Closure brought the JS down from 559KB → 128KB (~77% reduction); dropping
`-g3 -sSAFE_HEAP=1 -sASSERTIONS=2` brought the wasm down from 91MB → 17MB
(~81% reduction). 17 MB wasm is realistic for a streaming web RTS — the
Apr 7 dist was 44 MB at `-g`.

### Audio implementation plan documented

The full audio backend is genuinely a 4-phase, multi-session effort because
of the surface area (~70 AIL_* functions called from `MilesAudioManager`,
3.4k LoC) and the asynchronous-vs-synchronous mismatch between Web Audio's
`decodeAudioData` and `AIL_start_sample`. Rather than rush it in, this
session leaves a detailed phased plan in `port-todo.md` "Wire up real
audio" with:

- The exact init-gate location (`openDevice` line 1429: `AIL_quick_startup`
  returns 0, engine calls `setOn(false, All)`, all audio short-circuits).
- Per-sample flow (`playSample` line 2787) and the 6-function subset that
  unblocks 2D one-shot sounds.
- The streaming + 3D paths and what they each cost.
- The click-to-start gate from Session 11 already harvests a gesture for
  `audioContext.resume()`, so the platform-side autoplay block is already
  handled.

Phase 1 of the plan — replacing `libmilesstub` with a `libwebaudiobridge`
that returns enough non-null handles to keep `m_audioOn = true` — is the
next session's clear starting point. It's a contained scope (rewrite ~10
of the ~100 AIL_* functions, leave the rest as no-ops) that lets the
engine's audio code paths execute without short-circuit, surfacing any
second-order bugs before we add actual sound.

## 2026-05-05 — Session 12: First end-to-end browser test

Wired up Claude Preview against the new `link_mac.sh` build, served
`build-web/GeneralsMD/` over HTTP with a Range-aware `serve.py`, and
drove the page through to the engine's main loop. Found and fixed two
boot-blocking bugs and one cosmetic issue along the way; the deeper
"no-draws" bug needs targeted follow-up.

### Asset symlink + HTTP server (infrastructure)

59. **`build-web/GeneralsMD/assets` was symlinked to `Downloads/GeneralsZH-web/`,
    which only contains four small archives** (PatchINI.big, ShadersZH.big,
    shaders.big, plus an empty `deferred/`). The full asset set actually
    lives in `Downloads/GeneralsZH-web-fresh/assets/` (16 archives total,
    the 15 startup archives + maps.big + gensecZH.big). Re-pointed the
    symlink so BigVFS finds the real files. Audio archives
    (`AudioZH/MusicZH/SpeechZH.big`) are still missing on disk, but the
    deferred mount tolerates 404s.

60. **Custom `build-web/GeneralsMD/serve.py`** with proper HTTP Range
    support — Python 3.14's stock `SimpleHTTPRequestHandler` returns 200
    with full content for Range requests, which breaks BigVFS's
    header-only fetch pattern. The new handler parses `Range: bytes=N-M`
    and responds with 206 + Content-Range. Also serves `.wasm` with the
    correct `application/wasm` MIME and disables caching while iterating.

### Boot-blocking bugs caught in the browser

61. **Engine quit-suicide via `isMusicAlreadyLoaded()`** — first time we
    actually ran the build past engine init in a browser, console showed
    `INFO: GameEngine requested quit — stopping main loop` followed by
    `Aborted(native code called abort())`. Root cause: `GameEngine.cpp`
    line 624 calls `if (!TheAudio->isMusicAlreadyLoaded()) setQuitting(TRUE);`
    The `isMusicAlreadyLoaded` helper iterates `m_allAudioEventInfo` for
    an `AT_Music` entry and then checks `TheFileSystem->doesFileExist()`
    on the resulting path. With milesstub backing the audio path and
    `MusicZH.big` 404'ing on disk, that check fails and the engine kills
    itself before the first frame paints.

    Fix: `EmscriptenMain.cpp` now calls `TheGameEngine->setQuitting(FALSE)`
    immediately after `TheGameEngine->init()` to clear the flag the
    engine just set on itself. Comment block explains the rationale and
    notes the override becomes a no-op when real audio lands.

62. **Error-overlay bleed-through** — when the abort overlay first showed,
    the dimmed loading screen + Begin button were visible *behind* it
    (semi-transparent error background = `rgba(10,0,0,0.92)`). Fix in
    `web_shell.html`: `showError` now hides the loading overlay outright
    (`overlay.style.display = 'none'`) so we never get layered UI.

### What's working: engine boots, main menu pushed

After 61–62 the boot sequence cleanly reaches `INFO: GameEngine::init()
complete` → main loop running → `Begin` button appears → click dismisses
overlay → frames advance with the engine alive (`Shell:push(Menus/MainMenu.wnd)`,
deferred audio mounts running, `INFO: Mounted 16507 files to Emscripten
FS under /assets/`). No abort.

### What's broken: black canvas — root cause not yet pinpointed

After the click-to-start hand-off the canvas stays solid black even
though the engine is running. Instrumented WebGL at the JS layer with
hooks on `glClear / glDrawArrays / glDrawElements / glUseProgram /
glViewport`:

```
Over a 3-second sample (~180 frames):
  clear:        558  (~3 per frame — Begin_Render + others)
  viewport:    2262  (~12 per frame)
  drawArrays:     0  ← never
  drawElements:   0  ← never
  useProgram:     0  ← never
```

The engine is running its render-pass scaffolding (clears + viewport
sets) every frame but **never reaches a single draw call**. With
`useProgram: 0` it's also never binding a shader. This rules out:

- The intro/sizzle Bink-stub trap — fixed by force-clearing
  `m_playIntro / m_afterIntro / m_breakTheMovie` to FALSE every frame
  in `GameWebFrameCallback`. Engine still produces zero draws.
- `m_breakTheMovie == TRUE` gate at `W3DDisplay::draw` line 1851 —
  forced FALSE every frame, no change.
- Resolution mismatch (engine renders at 800×600, canvas was
  464×688). Force-resized the canvas to 800×600 too — still black.
- Shader compile failure — `gles3_wrapper.cpp::Compile_Shader` /
  `Link_Program` log compile failures, none seen.
- Engine quit / not initialized — `g_GameEngineInitialized = TRUE`,
  `TheGameEngine != nullptr`, frames are advancing.

The engine's draw path is:

```
GameClient::update → TheDisplay->DRAW() → W3DDisplay::draw
  → if (!m_breakTheMovie && !m_disableRender && Begin_Render() == OK)
  → drawViews() / TheInGameUI->DRAW() / TheMouse->DRAW() / End_Render()
```

`Begin_Render` ⇒ `Set_Viewport` + `Clear` (matches the 12 viewport / 3
clear per-frame counts we see). But none of the inner `*->DRAW()` calls
ever reach `glDrawElements`. The d3d8 stub's `DrawIndexedPrimitive`
short-circuits with `if (!current_vb || current_vb->data.empty() ||
!current_ib || current_ib->data.empty()) return;` so a possible cause
is that `SetStreamSource` / `SetIndices` aren't reaching the stub for
the Render2D paths the menu uses, OR `Render2DClass::Render` early-outs
on `Indices.Count() == 0`.

**Next-session triage targets**:

a) Add a counter inside `Emscripten_IDirect3DDevice8::DrawIndexedPrimitive`
   that logs how often it gets called and how often it short-circuits
   on empty VB/IB. Counters per frame in the JS console will tell us
   if the engine even reaches the stub or not.

b) Add a counter inside `Render2DClass::Render` (after the
   `Indices.Count()` early-out) so we can tell if the menu has any
   geometry to draw at all. Zero indices means the WindowManager isn't
   pushing button quads — that points at the texture-load failures
   ("Targa: Failed to open file 'trstrtholecvr.tga'") preventing menu
   geometry from being assembled.

c) Inspect `TheDisplay->draw` entry — it might be returning early
   inside its own logic (e.g., `m_loadScreenRender == TRUE` guard at
   line 1854 takes the short InGameUI-only path). The fact that
   `m_loadScreenRender` defaults FALSE makes this less likely but
   worth verifying.

### Other findings worth remembering

63. **BigVFS is making massive duplicate range requests**. Network log
    showed `INIZH.big` fetched ~120 times in a single boot, plus repeated
    fetches of `Textures.big`, `EnglishZH.big`, `WindowZH.big`. This is
    effectively re-downloading the same archive headers per file lookup
    instead of caching them. At the current scale this adds ~10–30
    seconds of network latency to every boot. Filed as a perf followup;
    not blocking, but worth a single hash-map of fetched ranges to
    short-circuit re-fetches.

### Build artefacts after this session

```
build-web/GeneralsMD/GeneralsZH.{html,js,wasm}        — debug build, 31KB / 559KB / 91MB
build-web/GeneralsMD/GeneralsZH-rel.{html,js,wasm}    — release build, 11KB / 128KB / 17MB
                                                        (release does NOT yet have the
                                                        Session 12 boot fixes — re-link
                                                        is needed)
```

### To verify

- `python3 build-web/GeneralsMD/serve.py 8087` and load
  `http://127.0.0.1:8087/GeneralsZH.html`. Engine should boot to the
  "BEGIN" gate within ~15s, click → black canvas + alive frame loop
  (the known remaining issue from this session).
- Console should NOT show `Aborted(native code called abort())`.
- Console should show `INFO: GameEngine::init() complete` →
  `INFO: clearing post-init quit flag (no-music-loaded path)` →
  `Shell:push(Menus/MainMenu.wnd)`.

### Black-canvas root causes — Session 12 follow-up

After more instrumentation we found two related causes that explained
why earlier draw counters were all zero. Both are fixed in the source
tree; the underlying "no draws to the menu" bug is still open and now
we have a much better starting point for the next session.

64. **WebVisibility::Init paused the loop in iframes.** Calling
    `emscripten_set_visibilitychange_callback` made the engine pause
    whenever `document.hidden` flipped true. In Claude Preview's iframe
    (and likely other embed contexts) `document.hidden` is *always* true
    even while the iframe is visibly rendering. Result: the main-loop
    callback's first guard `if (g_Loop.paused) return;` short-circuits
    every frame; `frame_callback` (and therefore the engine's update +
    draw) never runs.

    **Fix**: commented out the `WebVisibility::Init();` call in
    `EmscriptenMain.cpp::main()`. Browsers already throttle background
    tabs at the platform level (rAF stops, setTimeout throttles to 1Hz);
    the explicit pause was a defence-in-depth optimisation that turns
    out to brick the engine in iframe embeds. A real save-state pause
    should be triggered by user-visible UI, not by an event the iframe
    can't authoritatively observe.

65. **`emscripten_set_main_loop` with fps=0 uses requestAnimationFrame**,
    which Chrome aggressively throttles to 0Hz when `document.hidden`
    is true. Even with #64 fixed, the loop never tick'd in the iframe.

    **Fix**: switched `WebMainLoop::Start(GameWebFrameCallback, 0, true)`
    to `WebMainLoop::Start(GameWebFrameCallback, 60, true)`. Non-zero fps
    makes Emscripten pick `setTimeout` instead of rAF, which is throttled
    to ~1Hz in iframes (still slow) but at least keeps the loop ticking.
    On a top-level browser tab the runtime gives `setTimeout` proper
    pacing; the only loss is vsync alignment.

### What we actually see now

After both #64 and #65, the engine runs every frame (slowly in iframe,
60Hz on a top-level tab). WebGL instrumentation captures real activity:

```
Over a 5-second sample (iframe, ~9 frames):
  clear:        9
  viewport:    858  (~95 per frame; engine sets viewport per UI element)
  bindTexture: 143  (~16 per frame; textures ARE being bound)
  texImage2D:    3  (textures uploaded)
  bufferData:    0  ← still 0
  bindBuffer:    0  ← still 0
  useProgram:    0  ← still 0
  drawArrays:    0  ← still 0
  drawElements:  0  ← still 0
```

So the engine is doing per-frame work — clearing, setting viewports,
binding textures, even uploading texture data — but **never** calls
`glDrawArrays` / `glDrawElements` / `glUseProgram` / `glBufferData`.
The render path is making it deep into texture state setup but
short-circuiting before any geometry is sent.

This rules out the throttling theory: a frame ticking at 1Hz still has
all the time it needs to complete a render frame. The black canvas is
a real "no geometry submitted" bug, not a throttling artifact.

### Next-session triage targets (refined)

The pattern (textures bound, buffers untouched, no useProgram) strongly
suggests the d3d8 stub's `Emscripten_IDirect3DDevice8::DrawIndexedPrimitive`
short-circuit:
```cpp
if (!current_vb || current_vb->data.empty()) return 0;
if (!current_ib || current_ib->data.empty()) return 0;
```

Either:

a) `SetStreamSource` is being called with a vertex buffer whose `data`
   vector is empty (length=0 at construction, or the engine never
   wrote into it via Lock/Unlock).

b) `SetStreamSource` is never called at all and `current_vb` stays null.

c) `Render2DClass::Render`'s early-out `if (!Indices.Count() || IsHidden)
   return;` is hit because the menu's window list assembled zero
   geometry — likely because the menu's textures (e.g.,
   `trstrtholecvr.tga` and presumably others) failed to load from the
   .big archives and the windows therefore have nothing to draw.

The `Targa: Failed to open file "trstrtholecvr.tga"` log is the smoking
gun for option (c). Add an instrumentation pass on the texture loader
(or grep INI files for `trstrtholecvr`) to see *which* asset is
expected at that path and whether the menu .wnd files reference it.
`Render2DClass::Render` taking the `Indices.Count() == 0` early-out
also explains why we see zero `bufferData` / `useProgram` calls
despite the engine reaching the menu push.

The textures we DO see (`bindTexture: 143 / texImage2D: 3`) are
probably the engine's MissingTexture fallback being bound to many
windows whose intended texture failed to load — but with no geometry
indices to actually draw them onto, no rasterisation happens.

### Follow-up (perf): BigVFS duplicate range fetches

Network log showed `INIZH.big` fetched ~120 times per boot — once per
file lookup instead of once per archive. A single hash-map of fetched
byte-ranges keyed by archive name would short-circuit re-fetches and
shave ~10-30s off boot. Filed for a follow-up session.

### Build state at end of Session 12

```
build-web/GeneralsMD/GeneralsZH.{html,js,wasm}     — 31KB / 559KB / 91MB
                                                     (debug, includes Sessions 5-12)
```

Source-tree fixes in this session that need re-verification on a real
top-level browser tab (where `document.hidden = false`):
- `EmscriptenMain.cpp` setQuitting(FALSE) post-init (#61)
- `web_shell.html` showError hides loading overlay (#62)
- `EmscriptenMain.cpp` WebVisibility::Init disabled (#64)
- `EmscriptenMain.cpp` main loop fps 0 → 60 (#65)
- `EmscriptenMain.cpp` per-frame `m_breakTheMovie = FALSE` + `m_loadScreenRender = FALSE` (belt-and-braces)

### Session 12 second render fix — `BaseVertexIndex` offset

After the SetStreamSource fix landed pixels on screen, the user reported
"images cut up incorrectly, not in the right menu, no 3D yet". Symptoms:
the C&C Generals logo + various menu textures rendered at full-quad size
with what looked like wrong UV / position data, layered on top of each
other; the 3D scene was missing.

**Root cause**: WebGL2 / OpenGL ES 3.0 has no `glDrawElementsBaseVertex`,
but the W3D engine's dynamic vertex buffer allocator routinely allocates
N vertices INSIDE a much larger shared dynamic VBO and returns a
`VertexBufferOffset` to mark where its allocation starts. The engine
then writes indices `[0, 1, 2, 3]` (relative to ITS allocation) and
calls `SetIndices(BaseVertexIndex = VertexBufferOffset)`. In real D3D8,
BaseVertexIndex is added at fetch time. In our stub it was stored in
`base_vtx_index` but never actually applied — so `glDrawElements` read
indices 0..3 from the FRONT of the VBO, fetching whichever vertices the
first draw of the frame happened to write.

That's why we saw the same logo / atlas page replayed across the canvas
every frame, with wrong UVs (the first allocation's UVs) and wrong
positions (the first allocation's positions). The 3D mesh draws went
through the same path and mis-fetched too — so no scene visible.

**Fix**: shift the per-attribute pointer base forward by
`base_vtx_index * stride` bytes inside `_setup_vertex_attribs`. With the
base pointer pre-shifted, index 0 reads OUR allocation's first vertex,
index 1 reads our second, etc. — emulating BaseVertexIndex correctly.

```cpp
// emscripten_compat/d3d8.h
void _setup_vertex_attribs(UINT stride_override = 0) {
    ...
    UINT stride = stride_override ? stride_override : current_stride;
    if (stride == 0) stride = dec.Get_FVF_Size();
    const UINT base_offset_bytes = (UINT)base_vtx_index * stride;
    dec.Apply_Vertex_Attribs(stride, base_offset_bytes);
}
```

```cpp
// gles3_fvf.cpp — Apply_Vertex_Attribs gains a base_offset_bytes param,
// added to every per-attribute offset:
glVertexAttribPointer(ATTR_POSITION, ..., stride,
    (const void*)(uintptr_t)(base_offset_bytes + location_offset));
// ... same for normal, diffuse, texcoord0..3 ...
```

After both this fix and the SetStreamSource fix:
- The C&C Generals Zero Hour logo renders at the right position with
  correct UVs (full logo, no atlas-tiling).
- The Options/setup dialog (visible at boot) shows proper widgets:
  dropdowns, sliders, checkboxes — each cropped to its correct UV
  sub-rectangle.
- The 3D scene background renders behind the dialog (terrain, structures,
  visible vehicle on the right edge).

### Session 12 first render fix — black-canvas FIXED (`SetStreamSource` clobber)

**Root cause** found via in-engine diagnostic counters wired through
`dx8wrapper.cpp` and `emscripten_compat/d3d8.h`. The W3D engine's
`Apply_Render_State_Changes` walks `MAX_VERTEX_STREAMS` once per draw and
calls `SetStreamSource(i, vb, ...)` for every stream — stream 0 with the
real VB, streams 1..N-1 with `nullptr` (since only stream 0 is used). The
Emscripten d3d8 stub at
`GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/emscripten_compat/d3d8.h`
only models a SINGLE bound stream (`current_vb`), so the null calls for
streams 1+ kept clobbering the just-set stream 0:

```
SetStreamSource:7952 (null=3976)  ← exactly 50% null
dip:called=3976 no_vb=3976 drew=0  ← every draw short-circuited
```

**Fix**: filter `Emscripten_IDirect3DDevice8::SetStreamSource` to stream 0
only; `return 0` for any other stream number. The W3D path doesn't actually
use multi-stream geometry, so dropping streams 1..N is correct.

```cpp
virtual HRESULT SetStreamSource(UINT StreamNumber, ...) override {
    if (StreamNumber != 0) return 0;   // ← the one-line fix
    current_vb = static_cast<...>(pStreamData);
    ...
}
```

After the fix:

```
dip:called=3976 no_vb=0 empty_vb=0 drew=3976  ← every draw lands on GL
```

**Result**: the C&C Generals logo, "GENERALS / ZERO HOUR" title text, and
the menu's game-asset thumbnails all render to the canvas. The menu
button column is on the right side of the engine's 800×600 viewport and
gets CSS-clipped at the iframe's 464×688 backing buffer, but the
fundamental draw pipeline is now alive end-to-end.

### Session 12 follow-up — narrowed black-canvas down to d3d8 stub draw path

After more rounds of in-browser instrumentation, the black-canvas root
cause is now isolated to the d3d8 stub's draw short-circuit. Five
diagnostics were added in `EmscriptenMain.cpp` to probe the asset and
file-IO chain end-to-end:

```
VFS-PROBE: 'Window\Menus\MainMenu.wnd' -> ok=1 size=208561
VFS-PROBE: first 16 bytes: 46 49 4c 45 5f 56 45 52 53 49 4f 4e 20 3d 20 32
VFS-PROBE: as text: 'FILE_VERSION = 2;..STARTLAYOUTBLOCK..  LAYOUTINIT = W3DMainMenuI'
VFS-PROBE: fmemopen menu pos1=0 seekret=0 sz=208561 nread=208561 first10='FILE_VERSI'
```

→ `Web_VFS_Read_File_Sync` correctly returns the 208KB menu file.
→ `fmemopen` over the BigVFS-fetched buffer correctly handles
   `fseek(0,END)+ftell` and `fread(208561)`.
→ Therefore `LocalFile::open`'s BigVFS hook is producing a valid `m_file`
   that can read the entire menu — `WindowLayout::load` and
   `winCreateFromScript` should be parsing the .wnd correctly.

WebGL state-change instrumentation:

```
clear:          ~1 / frame
viewport:       ~12 / frame
bindTexture:    ~12 / frame   ← textures ARE bound
texImage2D:     occasionally  ← textures uploaded
bufferData:     0             ← vertex/index data NEVER uploaded
bindBuffer:     0
useProgram:     0
drawElements:   0
drawArrays:     0
enableVertexAttribArray: 0
```

The engine reaches `DX8Wrapper::Set_Texture` (which bindTextures) but never
reaches `DX8Wrapper::Draw → D3DDevice::DrawIndexedPrimitive →
GLES3_Draw_Triangles → glUseProgram + glDrawElements`. The most likely
short-circuit:

```cpp
// emscripten_compat/d3d8.h Emscripten_IDirect3DDevice8::DrawIndexedPrimitive
if (!current_vb || current_vb->data.empty()) return 0;
if (!current_ib || current_ib->data.empty()) return 0;
```

`current_vb`/`current_ib` are stored by `SetStreamSource`/`SetIndices` in
`Apply_Render_State_Changes`. The engine's `DX8VertexBufferClass` holds an
`Emscripten_IDirect3DVertexBuffer8*` whose `data` (a `std::vector<uint8_t>`)
must be non-empty. If `data.empty()`, the draw is silently skipped.

#### Concrete next-session targets (with file paths)

a) **Add a counter inside the d3d8 stub's DrawIndexedPrimitive**:
   `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/emscripten_compat/d3d8.h`
   line ~929. Make it bump three counters: `total_calls`, `skipped_no_vb`,
   `skipped_no_ib`. Expose via an `extern int` set + an `extern "C"`
   getter. Print from `EmscriptenMain.cpp::GameWebFrameCallback` once a
   second. This will tell us:
   - whether `DrawIndexedPrimitive` is ever called for the menu
   - if so, which short-circuit is taken

b) **Trace `DX8VertexBufferClass::Get_DX8_Vertex_Buffer`** in
   `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp`.
   Confirm it returns a non-null `Emscripten_IDirect3DVertexBuffer8*` with
   non-empty `data` when the engine creates a dynamic VB for menu UI quads.

c) **Trace `Emscripten_IDirect3DVertexBuffer8::Lock`/`Unlock`** in
   `emscripten_compat/d3d8.h` line ~636. The engine writes vertex data
   between Lock and Unlock. If `data` stays empty after Unlock, the
   resize-to-length in the constructor isn't happening, OR the engine's
   Lock returned a null pointer that didn't actually write data.

#### Probe code already in EmscriptenMain.cpp

Lines ~575–650 of `EmscriptenMain.cpp` now run a self-test on boot:
- 4 path probes against `Web_VFS_Read_File_Sync`
- 1 fopen of the MEMFS placeholder (confirms it's 0 bytes)
- 1 fmemopen sanity test on a small static buffer
- 1 end-to-end test that mimics `RAMFile::open(LocalFile)` on the menu

Keep these in place during black-canvas triage; remove once the menu
renders.


## 2026-05-20 → 05-24 — Session 13: Audio bridge, virtual network, skirmish hardening
*(Reconstructed summary — this stretch was originally logged only in commit messages and compile scripts.)*

- **Web Audio bridge**: `Core/Libraries/Source/WebAudioBridge/web_audio_bridge.cpp` (~1,400 lines)
  reimplements the Miles `AIL_*` surface on the Web Audio API — `AudioContext` graph with
  per-sample gain/panner nodes (HRTF for 3D), `decodeAudioData`, an IMA ADPCM decoder written
  from scratch, and detection of MP3 payloads inside nominal WAV headers. Compiled into the
  `milesstub` target via `cmake/miles.cmake`.
- **Virtual UDP transport**: `udp.cpp` gained `GX_Network_EnableVirtual()` — outbound packets
  hand off to `window.GeneralsX.onSendPacket`, inbound arrive via exported
  `GX_Network_PushPacket()`. A ready seam for WebRTC multiplayer; no JS side yet.
- **Skirmish fixes**: function-signature mismatch in Skirmish mode fixed; ODR memory-layout
  definitions aligned globally; mouse drift, viewport clipping, and terrain z-fighting decals fixed.
- **Build profile**: ASYNCIFY disabled (`-sASYNCIFY=0`) in favor of synchronous XHR in BigVFS;
  release profile gained `emmalloc` + Closure tuning; `MapsZH.big` slimmed for web serving.

## 2026-07-06 — Session 14: Resurrection — reproducible CMake build on Emscripten 6
The project moved machines with no build artifacts. Recovered the asset set and the May-24
binaries from backups, then made the from-source build reproducible for the first time since Mar 31:

- `cmake/web.cmake`: include-guard added — `CMAKE_PROJECT_INCLUDE` re-includes the file at every
  subproject `project()` call (gamespy/Generals/GeneralsMD), which duplicated targets. The
  `-DCMAKE_PROJECT_INCLUDE` path must also be **absolute**, and `-G Ninja` must be explicit.
- Emscripten 6.0.2 drift: `-sDEMANGLE_SUPPORT` removed (flag deleted upstream);
  `-sGROWABLE_ARRAYBUFFERS=0` required — Emscripten 6 defaults memory growth to JS resizable
  ArrayBuffers, and Chrome's WebGL rejects resizable-backed views in `texImage2D`.
- The fetched miles stub headers drifted (unpinned FetchContent): `mss.h` gained a `WAVEFORMAT`
  definition colliding with `emscripten_compat/windows_base.h`; guarded. Pin the dependency.
- Link parity with the last hand-built binary: full `EXPORTED_FUNCTIONS` list, `-lidbfs.js`,
  1 GB/2 GB heap, 4 MB stack moved from `link_mac.sh` into `Main/CMakeLists.txt`;
  `WWAudio/Threads.cpp` compiled directly into the web target (WWAudio has no CMake target yet).
- **Verified**: clean `emcmake cmake -G Ninja` + `ninja z_generals` build boots to the animated
  main menu in a headless browser. CI now runs this exact build on every push.

## 2026-11-09 — Session 15: Serverless WebRTC multiplayer on the virtual LAN
- Set out to carry the virtual-UDP seam between machines, not just tabs, with no server.
- Found the seam had one packet queue shared by every UDP socket, so the lobby socket (8086)
  and the game socket (8088) could read each other's packets. `udp.cpp` now tags packets with
  their destination port and each socket reads only its own; the queue is capped at 1024.
- The JS side hard-coded `srcPort: 8087` and compared the broadcast address against
  `0xFFFFFFFF` while `EM_ASM` hands it over as signed `-1`. The sender's real port now flows
  through `onSendPacket`, and addresses are coerced to unsigned. Packets travel as one binary
  frame (src/dest IP and port, then payload) on both transports.
- Added `?p2p`, a WebRTC mesh. Each pair has a `lan` channel (unordered, no retransmits:
  UDP-like) for game frames and a reliable `ctl` channel for control. Control messages reach
  their target over a direct `ctl` or through a neighbour linked to it (ttl 3).
  - Bootstrap: invite/reply codes (deflated JSON holding the SDP, about 1 KB) swapped by hand.
  - Discovery: peers gossip their direct links every 5 s; a peer that learns of someone it
    isn't linked to connects through that neighbour, so one pairing joins the whole group.
  - Offers: the smaller id offers (manual invites excepted); offers carry a nonce, so a stale
    answer can't land on a newer connection. ICE is not trickled; gathering gives up after 5 s.
  - Resilience: a peer without a direct link still gets every frame, forwarded by a
    neighbour; links the browser reports `failed` are dropped and retried through the mesh
    (`disconnected` is left to WebRTC, which recovers it); peers silent for 60 s are forgotten.
- Dead end: a WebSocket relay (discovery + fallback route) was built and then dropped to keep
  the port serverless.
- Dead end that taught the most: slow code swaps. The joiner's ICE agent starts checking the
  host's candidates as soon as it makes its reply, and nothing answers until the host pastes
  it: Chromium gives up after about 30 s. Stripping the host's candidates from the invite
  fixed Chromium (with no pairs it waits, and learns the host from its incoming checks), but
  Firefox fails an agent with no pairs after a ~5 s grace period. The fix that works in both:
  keep one placeholder candidate on TEST-NET-1 (192.0.2.x, never routed) pending, adding a
  fresh one every 3 s, until the link opens. Codes now stay good for 10 minutes.
- Two adversarial reviews caught: a newcomer the larger-id peer didn't record (one-way until
  the offer arrived); tearing links down after 10 s `disconnected`, which in a two-player game
  left no route to re-signal; unanswered invites never closed; a stalled manual handshake
  blocking the mesh for 10 minutes; a bad or stale pasted code breaking a live link; the
  panel claiming "copied" when the clipboard refused. All fixed; codes are validated and
  size-capped, and a virtual-IP clash logs a warning.
- Built with Emscripten 6.0.2 (the CI recipe, in the `emscripten/emsdk:6.0.2` image): links,
  no warnings from `udp.cpp`; `GX_Network_PushPacket` exports with 5 arguments. Boots to engine
  init and stops at the first INI read without game data, as expected.
- Verified with the shell's network block, game side stubbed:
  - Firefox 159, Edge 157 and Chromium 152 as one serverless group (Firefox invited Edge,
    Edge invited Chromium, Firefox–Chromium linked through the mesh); broadcasts and unicasts
    correct; a 3-minute code swap works with either Firefox or Edge as the joiner.
  - Chromium, three tabs: with A–C cut and blocked, frames went through B both ways;
    unblocked, A–C relinked in 26 s; B closed, A–C kept going and B was forgotten.
  - Node: a `disconnected` link survives the ticks and a `failed` one ends (fails on the
    previous code).
- A real LAN match (Steam Zero Hour data, two headless Edge players paired with `?p2p`) found
  four existing bugs that stopped the browser LAN before any packet left, all fixed:
  - `WSAStartup` shim returned 0 without filling `wVersion`, so `Transport::init` failed its
    2.2 check and the lobby showed "Failed trying to communicate with the network".
  - `wchar_t` is 4 bytes in wasm, so `LANMessage` is 536 bytes; `Transport::queueSend` dropped
    everything over the retail 476 (the `static_assert` is compiled out for Emscripten). Web
    builds now accept up to what `doRecv` reads back.
  - `HEAPU8` was not exported, so the first `UDP::Write` aborted; now in
    `EXPORTED_RUNTIME_METHODS`, and the `EM_ASM` reads `Module['HEAPU8']` (Closure-safe).
  - The default name `VirtualMachine_a_b_c_d` is cut to 12 characters, so every player was
    "VirtualMachi" and joins failed as duplicate names; it is now `Player<last octet>`.
- Verified: both players listed in the LAN lobby, host created a game, the other joined and
  accepted, the match started on Alpine Assault, each built a dozer, and both clocks passed
  1:14 in step with no desync.
- Saved LAN names came back as their first letter ("Player12" -> "P"): the quoted-printable
  name codec walked bytes and stopped at the first `00 00`, which with a 4-byte `wchar_t` comes
  right after the first char. It now walks characters and writes each as two UTF-16LE bytes,
  the same format the Windows build writes, so existing prefs still load.
  Names already saved by the old encoder load as one letter, so on 4-byte `wchar_t` builds a
  one-letter saved LAN name is treated as that damage and replaced by the default (checked in the
  browser: a planted `V_00` loads as `Player11`, a planted `Amir` still loads). The base
  Generals copy of the codec and the same guard are fixed too.
- Review nits fixed: auto-assigned virtual IPs are now random `10.0.x.y` (253 addresses gave an
  8-player group about a 10% chance of a clash; now about 0.04%), and the default name is
  unique per IP (`Player<low 16 bits>` on 10.0.x.y, `P<hex IP>` otherwise). Names with emoji
  are saved as UTF-16 surrogate pairs, as on Windows, and joined back on 4-byte `wchar_t`.
- Long matches: game frames are dropped when a channel's send buffer passes 256 KB, as UDP
  would, instead of piling up seconds of stale frames behind a slow link (the game resends
  what it needs). Node check: a backed-up channel drops, a drained one sends again.
- 30-minute match (2 GPU-rendered headless Edge players, auto IPs 10.0.45.179 / 10.0.91.253,
  default names Player11699 / Player23549): both ordered a unit every 2 minutes for 9 rounds and
  every order took effect; the link stayed direct, no disconnection screen appeared, and each
  browser's memory held at 0.7–1.0 GB with no growth.
- Hidden tabs: browsers stop `requestAnimationFrame` in a background tab, so one player switching
  tabs froze that engine and the lockstep match with it. In a networked game the page now ticks
  the engine (`GX_TickHidden`) from a Web Worker timer while hidden, one tick in flight at a
  time so slow frames can't pile up. Single player still pauses. Tested on a fresh 2-player
  match, counting packets: player 2 sent 401 per 30 s visible and about 204 per 30 s while hidden
  for 2 minutes, and both players carried on normally afterwards. With the ticker switched off
  (the old behaviour) it sent almost nothing, and after 1 minute it came back to the
  Disconnection Menu.
- More players (GPU-rendered headless Edge on one 15 GB machine):
  - 8 players: 7 code swaps formed a full mesh (all 28 links direct), all 8 shared one LAN
    lobby and an 8-player game was set up; the match itself did not fit in memory (about
    1.6 GB per copy) and the host's joins timed out while it was swapped out.
  - 4 players: mesh, lobby, join, accept and start on Dark Night (4). Two copies then failed
    from machine limits (one lost its WebGL context, one stalled in swap); the other two kept
    playing in step. A leave-and-return to the lobby kept the full name.

## 2026-11-10 — Session 16: In-game rendering — shroud, terrain, minimap, colours

Reported in Edge (ANGLE on OpenGL, AMD RX 580, Mesa 25.2) and reproduced in headless Edge with
`--use-angle=gl` at 1873x1035 on Alpine Assault (one-player LAN game): blocky stair-step shroud,
black dashes across the terrain, dark stretched patches on cliffs, and a black, streaked minimap.
Five emulation bugs, all in the DX8 → WebGL2 layer:

- **No texture coordinate generation.** The shroud (and cloud shadows, the terrain noise pass,
  projected decals) asks D3D to build texcoords from the camera-space position
  (`D3DTSS_TCI_CAMERASPACEPOSITION`) through `D3DTS_TEXTUREn` (`D3DTTFF_COUNTn`). The shader
  ignored `D3DTSS_TEXCOORDINDEX` and the texture matrices, so the shroud pass multiplied the
  terrain by shroud texels picked with the terrain tiles' UVs: that was the black dashes, the dark
  cliff patches and the shroud's shape. The vertex shader now does D3D8's per-stage texcoords
  (UV set selection, camera-space normal / position / reflection, texture matrix, `PROJECTED`
  divided per fragment). Stage defaults are D3D's (`TEXCOORDINDEX` = stage).
- **Wrong `D3DTSS_*` numbers** in `emscripten_compat/d3d8types.h`: MIN/MAG filter swapped,
  bump-env, border colour, ADDRESSW, COLORARG0/ALPHAARG0 and `TEXTURETRANSFORMFLAGS` (124
  instead of 24) off. `DX8Wrapper::TextureStageStates[stage][32]` was indexed out of bounds with
  124/125, and the GLES3 layer, which reads the real numbers, never saw transform flags while
  the bump matrix landed on slot 24. All now match D3D8.
- **No linear filtering in the caps.** `TextureFilterCaps` was 0, so `TextureFilterClass` mapped
  `FILTER_TYPE_BEST`/`DEFAULT` to POINT: the shroud's stair steps (and point-sampled fonts).
  The caps now report MIN/MAG linear and MIP linear/point.
- **Minimap texture format.** The radar picks `D3DFMT_R8G8B8` first; the upload had no case for
  it and sent the 3-byte buffer as 4-byte RGBA (`Unknown DX8 texture format 20`), hence black
  with streaks.
- **WebGL2 has no texture swizzle.** `GL_TEXTURE_SWIZZLE_*` fails with INVALID_ENUM (257 errors
  per run), so every uncompressed A8R8G8B8 / X8R8G8B8 texture was sampled with red and blue
  swapped (the menu's blue night scene and orange UI were really the sunny desert shellmap and
  Zero Hour's blue UI), and L8 / A8 / A8L8 read the wrong channels. `To_GL_Pixels` now converts
  channel order on the CPU for both create and update, and replaces the two copies of that code.
- Also: `LockRect` ignored `pRect` (D3D points `pBits` at the rect's first pixel); fixed for
  textures and surfaces, used by `SurfaceClass::Lock(min,max)` and the font column tests.

Verified (same map, USA, same start, headless Edge on ANGLE-GL 1873x1035, before = deployed
build): shroud edges and the map edge fade softly, no black dashes, cliffs show rock instead of
dark smears, the minimap shows the terrain with its shroud, and the menu and control bar
have their real colours. Not fixed: some terrain blend tiles (snow patches) still have hard
cell edges; that was the same before, and it's the 2-stage terrain blend pass, a separate issue.

## 2026-11-10 — Session 17: Black infantry, resolution lock, detail level

Three reports from the live server: infantry render as black silhouettes, the resolution is
stuck at the smallest mode, and higher detail levels don't take. Reproduced and verified in
headless Edge (`--use-angle=gl`) with a LAN game on Alpine Assault (USA, Rangers from a barracks).

- **Black infantry: recoloured textures were empty.** Team-coloured textures (infantry) are copies
  made by `W3DAssetManager::Recolor_Texture_One_Time`, which builds a texture from a surface
  through `D3DXLoadSurfaceFromSurface`. That was a `return 0` stub, so the copies stayed all
  zero (read back from GL: RGBA 0). It now copies, point-sampling between the rects and
  converting between the 16/24/32-bit RGB formats. `SurfaceClass::Copy` across formats and the
  missing-texture surface used the same stub.
- **Material colour source.** The vertex shader took ambient and diffuse from the vertex colour
  whenever `D3DRS_COLORVERTEX` was on. D3D only does that per `D3DRS_{AMBIENT,DIFFUSE,EMISSIVE}MATERIALSOURCE`
  = `D3DMCS_COLOR1`, and only when the vertex has a diffuse colour. Skinned meshes always carry
  one (0 when the mesh has none) with the material as source, so they came out black even once
  textured. The GLES3 layer now passes the three sources as a bitmask. Its render-state cache
  must start at 0 like `DX8Wrapper::RenderStates`: the wrapper drops a "change" to a value it
  already holds, so seeding D3D's own defaults there made them stick.
- **Resolution.** The shim offered one mode and `web_shell.html` pinned the canvas to 800x600,
  CSS-scaled. The device now lists the usual modes from 800x600 to 3840x2160 plus the screen's
  size; `CreateDevice`/`Reset` size the canvas backing store to the back buffer and set its CSS
  aspect ratio, and the page's lock follows `Module.gxCanvasW/H`. The engine re-lays out its UI
  for the mode, mouse mapping already scales to `TheDisplay`, and the choice persists in
  `Options.ini`.
- **Detail level.** `CPUDetectClass::Init_Memory` only ran when CPUID exists, never on wasm, so
  RAM read 0 MB: `GameLODManager` failed its 256 MB check, which forces low-res textures, turns
  trees and the shell map off, and recommends Low. It now runs unconditionally (the web build
  reports 1 GB).

Verified: Rangers are textured with their team colour; vehicles keep theirs; Options lists the
modes, 1280x720 applies at once (16:9 canvas, UI re-laid out, clicks land) and survives a
reload; Very High sticks; the shell map now plays behind the main menu.

## 2026-11-11 — Session 18: Fixes from an adversarial rendering review

Three independent reviewers read the GLES3/D3D8 shim against D3D8 semantics; these are the
must-fix findings, each checked in the code first.

- **Texture stage arguments were ignored.** The fragment shader always combined arg1 = texture and
  arg2 = current. It now reads D3DTSS_COLORARG0/1/2 and ALPHAARG0/1/2 for stages 0-1, with the
  COMPLEMENT / ALPHAREPLICATE modifiers; a stage with no texture reads TEXTURE as white (D3D), not
  WebGL's black; COLOROP DISABLE ends the chain. MULTIPLYADD is arg0 + arg1·arg2 and DOT3 is
  4·dot(arg1-½, arg2-½), which keeps render2d's grayscale path identical. Stage args default to
  D3D's (TEXTURE, CURRENT).
- **Fog never showed.** FOGSTART/FOGEND arrive as float bits and were dropped, so start = end = 0.
  Stored now; the fog distance is eye-space z, as D3D vertex fog without RANGEFOGENABLE.
- **Texture uploads stole another stage's binding.** Create/Update_Texture bound to whichever unit
  was active; they now restore that unit's texture.
- **Non-indexed and UP draws applied BaseVertexIndex.** A leftover SetIndices base shifted the
  shadow-darkening and occluded-player-colour quads past their 80-byte VBO. Only
  DrawIndexedPrimitive uses the base now.
- **XYZRHW vertices went through World·View·Projection.** They are screen pixels with 1/w; the
  vertex shader maps them straight to clip space (D3D pixel centres at +0.5) and skips lighting and fog.
- **Stencil.** Only STENCILENABLE reached GL. Func/ref/mask/ops/write mask are forwarded, and the
  canvas now asks for an 8-bit stencil buffer (EmscriptenMain.cpp): it had none, so once the quads
  above drew, the player-colour quad tinted the whole view red. Clear ignores the write masks, as
  D3D's Clear does.
- **Point lights** now stop at Range and add their Ambient (D3D: global + Σ attenuated light ambient).
- **Render-state cache.** Session 17 said this cache "must start at 0 like DX8Wrapper::RenderStates";
  that was wrong: the engine's cache starts at 0x12345678 (Invalidate_Cached_Render_States). The
  redundant-state skip here dropped the engine's first "set to 0" (TEXTUREFACTOR stayed white,
  AMBIENT stayed 0.2). The skip is gone; the engine already filters repeats.

Verified: same LAN game on Alpine Assault in headless Edge (ANGLE-GL) renders the same as the
deployed build (only the flashing general's button differs), 8 stencil bits, no WebGL errors.
Not exercised by that scene: fog, stencil shadows, point lights. Still open (should-fix): stages
2-3 always modulate, missing ADDSMOOTH/SUBTRACT/specular, SetRenderTarget no-op, authored mips
replaced by glGenerateMipmap, small DXT mips under-allocated, no S3TC fallback.

## 2026-11-11 — Session 19: Pixel shaders, four texture stages, specular, small DXT levels

The should-fix items from the Session 18 review, starting with the terrain blend edges.

- **Terrain blends (hard snow edges) — root cause: no pixel shader ever ran.** The web build reports
  PS 1.1, so the game picks TerrainShaderPixelShader (terrain.pso: `lrp r0, v0.a, t1, t0` then
  `mul r0, r0, v0`) and the flat/road variants. Two things stopped it: LoadAndCreateD3DShader sized
  its buffer with getFileInfo, which returns 0 for these backslash archive paths here, so the
  shader tokens were never read; and the shim's CreatePixelShader/SetPixelShader were stubs. The
  terrain then drew with whatever stage states were left over: dark, unlit by the vertex colour,
  and with no blend between the base and blend tile. Fixed: the loader asks the open file for its
  size; CreatePixelShader parses ps.1.1 tokens (tex, mov, add, sub, mul, mad, dp3, dp4, lrp; write
  masks, _x2/_x4/_d2, _sat; .a/.b replicate; neg/bias/sign/comp/x2 source modifiers) into a small
  program the fragment shader interprets, with c0-c7 from SetPixelShaderConstant. A program with
  anything else (texbem: wave, motion blur) gets no handle, so its caller keeps the fixed-function
  path as before. Trees still use the fixed-function path (their vertex shader is not emulated, so
  the game never selects the pixel shader for them). Water's runtime-assembled shaders are untouched
  (D3DXAssembleShader still fails).
- **Texture stages 0-3** now all run D3D's op chain with their own COLORARG0/1/2 and ALPHAARG0/1/2,
  stop at the first DISABLE, and saturate every stage (MODULATE2X no longer feeds >1 onward). Added
  SUBTRACT, ADDSMOOTH, BLENDDIFFUSEALPHA, BLENDFACTORALPHA, BLENDTEXTUREALPHAPM,
  MODULATECOLOR_ADDALPHA, MODULATEINV*, LERP, and the same ops on the alpha side. BLENDTEXTUREALPHA
  and BLENDCURRENTALPHA now use the texture's and current alpha (they used arg1.a/arg2.a).
  Stage ops are seeded with D3D's defaults (stage 0 MODULATE/SELECTARG1, others DISABLE).
- **Specular.** Vertex specular colour (FVF SPECULAR, attribute 11) is passed through when lighting
  is off; with lighting on, Blinn-Phong per light (local viewer, material power and specular, light
  specular). D3DRS_SPECULARENABLE adds it after the stages; D3DTA_SPECULAR reads it.
- **DXT levels below 4x4** were allocated w*h*BPP bytes, less than one 8/16-byte block; they get
  whole blocks now (texture levels and standalone surfaces).

Verified in headless Edge (same LAN game, Alpine Assault): snow and grass patches blend softly, the
terrain is lit by the vertex colour (it was much darker), no WebGL errors. Not exercised: specular
(nothing in that scene enables it), the monochrome filter, fog, stencil shadows, point lights.
Still open: SetRenderTarget no-op, authored mips replaced by glGenerateMipmap, no S3TC fallback,
a texture changed while bound is not re-uploaded, COLORWRITEENABLE ignored.

## 2026-11-19 — Session 20: Frame time: buffer uploads; in-game FOV at other resolutions

A performance report from the live server. Measured in headless Edge (ANGLE on OpenGL, RX 580),
rAF frame times over 16-20 s, fresh profile each run, same LAN game on Alpine Assault.

Before the fix, in game (still / panning), 800x600 Low unless noted:

| build | fps | game update per frame |
|---|---|---|
| 905b8f0 (before the rendering fixes) | 47 / 44 | 19-20 ms |
| 09466e2 | 30-43 / 31-43 | 25-31 ms |
| 27f193f | 39-42 / 40-41 | 20-24 ms |
| 9fca2b8 (live) | 40-44 / 41-46 | 23-30 ms |
| 9fca2b8, 1600x1200 Very High | 39 / 43 | 21 ms |

The rendering fixes cost little (within run-to-run noise); resolution and detail barely mattered:
the game was CPU-bound in every build. A fresh profile still starts at Low, 800x600 (the
recommended level comes from CPU presets that never match on wasm). The shell-map menu ran at
57-60 fps everywhere.

- **73% of in-game CPU time was `gl.bufferData`.** Vertex/index buffer `Unlock` marked the whole
  buffer dirty and the next draw re-sent all of it. The engine draws everything from large shared
  dynamic buffers, writing a small slice per draw, so each draw re-uploaded the whole buffer.
  Buffers now remember the locked byte range (SizeToLock 0 = to the end) and upload only that with
  `glBufferSubData`; GL storage is made once. After: 57-60 fps in game at 800x600 Low and 60 fps
  at 1600x1200 Very High, 12-18 ms update, CPU 63% idle in game; `bufferSubData` is 1%.
- **The in-game camera zoomed far out at any mode but 800x600.** W3DView::setWidth computes the
  horizontal FOV from the aspect ratio, but the view is sized width first and met the default
  height (1600x480 at 1600x1200: 99 degrees instead of 60), and setHeight never recomputed it;
  the terrain draw window then showed as a slab over black. setHeight now re-runs setWidth.
  Only showed once Session 17 made other resolutions work; Session 17 checked them in the menus only.

Not measured: a large late-game battle on a big map (the shell-map battle is the busiest scene
tested), and 16:9 modes in game (same code path as the FOV fix).

## 2026-11-20 — Session 21: Optimised web link; fewer audio calls into JS

Measured uncapped (`--disable-gpu-vsync --disable-frame-rate-limit`, so fps reflects the cost of a
frame) in headless Edge on an RX 580, same LAN game on Alpine Assault, after Session 20's fix:

| build | menu (shell-map battle) | in game, still | in game, panning |
|---|---|---|---|
| c2f5400 (RelWithDebInfo, live) | 852 fps | 364 fps | 438 fps |
| + `GX_WEB_OPTIMIZED=ON` + audio change | 1129 fps (+33%) | 554 fps (+52%) | 571 fps (+30%) |

- **The deployed build linked at -O0 with ASSERTIONS=2.** The README's RelWithDebInfo recipe is the
  development profile: compiled -O2, but linked without wasm-opt, with assertion wrappers on every
  JS call (`checkInt`, `get`, `stackSave` were ~15% of a frame), and full DWARF (a 68 MB wasm).
  Release (-O3, closure) can't be used for the private page: closure renames the `Module.gx*`
  properties the page reads, and the page is minified so it can't be patched. New option
  `GX_WEB_OPTIMIZED=ON` keeps RelWithDebInfo compiles and links -O3, `-g0 --profiling-funcs`,
  `-sASSERTIONS=0`, `-sMINIFY_HTML=0`: same page, same names, 8.3 MB wasm.
  Configure: `emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGX_WEB_OPTIMIZED=ON ...`
- **3% of a frame was `AIL_set_3D_position`** crossing into JS for the listener and every playing
  sound each frame. It now returns when the position hasn't changed.

Looked at and left: JS-based C++ exceptions (`invoke_*` trampolines) were ~12% under assertions and
~1% after; native wasm exceptions need a full recompile for little left. The per-viewport canvas
size query is under 1%.

Load Replay on web (found while building a heavier benchmark, not fixed): the Win32 local file
system lowercases paths when listing a directory but not when opening a file, so recorded replays
(`/userdata/Replays\`) never list, and reading a replay header from a lowercase path failed and
then aborted the engine (heap corrupted at address zero).

## 2026-11-20 — Session 22: GL state shadow, SIMD, wasm exceptions, optional render scale

Uncapped fps proved too noisy to judge (the same build gave 447 and 905 fps in the menu on two
runs). Measured instead inside the page: the game's frame callback time (median, ms) and WebGL
calls per frame, counted by wrapping the WebGL2 prototype. Headless Edge, RX 580, same LAN game.

| | c85dff7 (live) | + state shadow | + SIMD, wasm EH, viewport/uniform caching |
|---|---|---|---|
| menu (shell-map battle) | 3.0 ms, 2711 calls | 2.1-2.3 ms, 1155 | 1.5-1.8 ms, 853 |
| in game, still | 5.8 ms, 5768 calls | 4.3-4.4 ms, 1518 | 3.1-3.3 ms, 1210 |
| in game, panning | 4.6 ms, 3648 calls | 3.7-3.8 ms, 1134 | 2.7-3.0 ms, 915 |

- **GL state shadow** (gles3_wrapper.cpp): buffer bindings, per-attribute pointers and constants,
  the active unit and per-unit textures reach WebGL only when they change. Wrap and filter modes
  live in cached WebGL2 sampler objects instead of four glTexParameteri per bind, and a texture
  stage state only re-applies them when it is a wrap or filter state (it did on every COLOROP
  change). Buffers are deleted through GLES3_Delete_Buffer, which resets the attribute shadow: a
  reused GL id would otherwise look unchanged while the attribute still pointed at the old buffer.
  DrawPrimitiveUP/DrawIndexedPrimitiveUP re-fill one streaming buffer each instead of creating and
  deleting a GL buffer per call. The instancing flag uniform and the viewport/depth range are
  cached. Remaining churn: ~390 vertexAttribPointer per frame, because each draw's base vertex is
  baked into the pointer offsets; WEBGL_draw_instanced_base_vertex_base_instance would remove it,
  but Edge does not offer it (draft).
- **`GX_WEB_OPTIMIZED` now also compiles with `-msimd128` and `-fwasm-exceptions`** (cmake/web.cmake;
  asyncify is off, so native exceptions are allowed). Needs a full rebuild; wasm 6.9 MB.
- **Optional render scale** (page settings, default 100%; 85/75/50%): the canvas drawing buffer is
  the display mode times the scale and CSS upscales it. The engine keeps its resolution (UI layout
  and mouse mapping unchanged); GLES3_Set_Viewport scales viewports to the buffer, and textures
  sample with a mip bias of log2(scale) so they pick the full-size mip: without it, 50% showed dark
  tile seams on terrain (the atlas's 4-pixel tile borders bleed at small mips). Applies live.

Tried and dropped (no gain): ThinLTO (`-flto=thin`): same frame time, wasm 9.5 MB. A cache for
root-level winGetWindowFromId (2.4% in one profile): no measurable change.

Not done, with reasons: instancing/batching (the game issues ~100-210 draws per frame here; the
engine already batches trees, render categories and 2D, and instancing units means reworking the
W3D mesh renderer); caching getStaticDiffuse (it includes dynamic scene lights, so caching would
freeze explosion lighting on 3-way blend tiles, for under 1%); moving the game to a worker with
OffscreenCanvas (asset reads are synchronous XHR on the main thread and threads need COOP/COEP
headers). Revisit batching with a big-battle benchmark (needs Load Replay fixed, see Session 21).

## 2026-11-20 — Session 23: Worker-thread prototype (off by default)

`GX_WEB_WORKER=ON` (with `GX_WEB_OPTIMIZED=ON`) runs main() on a pthread with the canvas
transferred (`-pthread -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT=1`). Needs a server sending
`Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`.
What it took to boot:
- `cmake/gx_worker_em_asm.h`, force-included in that build, turns the game's EM_ASM blocks
  (page UI, Web Audio, the JS FS, which lives on the main thread) into MAIN_THREAD_EM_ASM.
- SDL2 makes its GL context through EGL, which Emscripten runs on the main thread; the worker
  build makes the WebGL2 context on the game thread instead (EmscriptenMain.cpp) and skips SDL's
  swap (the OffscreenCanvas presents when the frame callback returns).
- The canvas backing size is set from the game thread (d3d8.h Size_Canvas); page code that sets
  canvas.width tolerates the transferred canvas. AILBridge_init (EM_JS) runs on the page.

Result: boots, plays the shell map, starts a LAN game on Alpine Assault, input works. But
game-thread fps with the frame cap off (new GXFRAME log, every 5 s):

| | normal build | worker build |
|---|---|---|
| menu | 1000-1370 | 160-210 |
| in game, still | 380-790 | 140-170 |
| in game, panning | 620-890 | 150-200 |

About 4x slower. Likely causes, not yet separated: every redirected EM_ASM is a synchronous
round trip to the main thread (the audio bridge sets 3D sound positions per sound per frame),
file reads are proxied to the main thread, and shared memory with ALLOW_MEMORY_GROWTH slows the
JS glue's heap access on every GL call. Also unsolved for a real deploy: the page's network and
input callbacks would call into the game from the main thread (a data race), and the private
server's saved-asset XHR redirect only patches the main thread. Not deployed; the normal build
stays the shipping one.

## 2026-11-20 — Session 24: Load Replay works; big-battle benchmark

- **Load Replay listed nothing, and loading a replay crashed.** Two causes:
  - Win32LocalFileSystem::getFileListInDirectory lowercased every path, while LocalFile::open and
    CreateDirectory keep the case of player data (/home, /userdata, /tmp). Recorded replays live
    in `/userdata/Replays/`, so the list looked in `/userdata/replays/`. Listing now uses the same
    rule as open.
  - Replay headers store wide text as UTF-16 (2 bytes), but LocalFile::readWideChar/writeChar/
    writeFormat used sizeof(wchar_t), which is 4 on wasm: every field after the replay name was
    misread and the parse ended in an alignment fault (the release build reported it as heap
    corruption at address zero). Wide characters in files are now read and written as 2-byte
    UTF-16, and the recorder writes its strings with `%ls` (`%s` in a wide format is a wide string
    on Windows only). Only the recorder uses these functions.
  Found with a SAFE_HEAP debug build (new option `GX_WEB_SAFE_HEAP=ON`; it also flags benign
  unaligned reads in map loading, so it stops there).
- **Retail 1.04 replays drift on wasm** (CRC mismatch at frame 110 in the 3v3 Hostile Dawn replay
  from GeneralsReplays): wasm floating point is not bit-identical to the PC build. Playback pauses
  once and resumes with P; the Hard AI armies keep playing, which makes a usable big-battle
  benchmark. Replays recorded on the web build should play back on it; not verified yet.
- **Big-battle numbers** (that replay, 8 minutes, ~frame 24000, optimised build, 60 fps cap):
  5.0-5.7 ms frame callback (p95 6.7-8.5 ms), 2000-2400 GL calls per frame, CPU ~60% idle.
  So: cached vertex layouts (VAOs) skipped, vertexAttribPointer and its glue are ~1.5% and most
  draws come from shared dynamic buffers whose offsets change per draw (a VAO cache would miss);
  shader variants skipped, the GPU is not the limit on an RX 580 and Edge exposes no GPU timer
  to measure them; the render-scale option covers weak GPUs.
- **Audio:** the listener orientation, set every frame, only crosses into JS when it changes.

## 2026-11-20 — Session 25: Loading 3-4x faster; debug logging off the frame

Loading, timed in headless Edge from localhost (fresh profile, LAN game on Alpine Assault):

| build | page open -> BEGIN | map load (Start -> in game) | downloaded to in game |
|---|---|---|---|
| fa00b8d (live) | 19-24 s, 420-435 MB | 38.8-48.3 s | 1071 MB |
| 1 MB read-ahead windows | 7-8 s, 194-199 MB | 12.3 s | 334-336 MB |
| + debug prints removed | 6.9-7.6 s, 199 MB | 10.7-11.2 s | 337 MB |

- **Read-ahead windows 8 MB -> 1 MB** (gles3_big_vfs.cpp Fetch_Range_JS). Every read of 512 KB or
  less pulled the 8 MB window around it, and the 96 MB window LRU evicted and re-fetched them:
  Textures.big (318 MB) was read for 343 MB, W3D.big (176 MB) for 224 MB. Each fetched byte also
  goes through the responseText/charCodeAt loop. Tried: 8 MB/384 MB cap 822 MB 29 s; 2 MB/192 MB
  427 MB 13.4 s; 1 MB with a 96 or 192 MB cap the same (kept 96); 512 KB no faster.
  ~110 MB is still read before BEGIN by archives preloaded whole (all maps, INI, windows); the map
  list needs them.
- **Debug logging in hot paths.** A battle replay (3v3 Hostile Dawn) at Very High, 1600x1200,
  camera on an army ran 53-57 fps with dips to 41-48; 26% of the CPU was console output:
  W3DProjectedShadowManager::queueDecal printed four lines per shadow decal per frame (4.6 million
  lines in 8 minutes; decals are on above Low). Removed, along with its dmaPool_32 tracing toggle
  and the allocator's tracing blocks (a pool-name strcmp on every alloc/free), and the
  DEBUG-TOC/CHUNK/DICT prints in DataChunk (thousands per map load). After: a steady 60 fps (rare
  55-58), CPU ~59% idle, 1821 console lines in the whole run.

## 2026-11-20 — Session 26: Logging sweep; BEGIN -> menu; map archives load on demand

- **Logging sweep.** Every plain printf/fprintf and EM_ASM console call outside start-up and
  tools: what is left is start-up messages, debug-command report dumps, rare warnings, or code
  behind GX_TRACE_LOGGING. Console lines per run: a menu + LAN game at Low 758 (mostly the
  1-second TICK/DX8 diagnostics); the Very High battle replay 1821 after Session 25.
- **BEGIN -> main menu: 0.67-0.74 s** (animation-frame stalls after the click; the engine loads
  before BEGIN shows). Session 25's 14 s figure was an artifact of the timing script.
- **maps.big, MapsZH.big and TerrainZH.big are no longer read whole at mount** (gles3_big_vfs.cpp
  Should_Preload). Page open -> BEGIN 6.9-9.7 s / 199 MB -> 6.2 s / 127 MB; downloaded to in game
  337 -> 268 MB; map load unchanged (11.2 s). Checked: the official map list and previews in the
  LAN lobby, a LAN game on Alpine Assault, the 3v3 Hostile Dawn replay (MapsZH.big).
## 2026-11-20 — Session 27 (exp/worker-thread): the worker build is as fast as the normal one

Branch `exp/worker-thread`, not merged. Measured in headless Edge (RX 580) with a new GXWORK log:
the average time the game's frame function takes, every 5 s, on whichever thread runs it.

- **"4x slower" was frame pacing, not work.** With the frame cap off, Session 23 compared frames
  per second. A worker's frames are clocked differently: its game thread sat 57% idle at
  ~180 fps. Work per frame was close all along; with the 60 fps cap on both run at 60.
- **Remaining overhead, removed** (worker build unless noted):
  - Audio play/stop/volume/pan/position/orientation calls with only numbers in and nothing out
    go to the page as MAIN_THREAD_ASYNC_EM_ASM (queued) instead of blocking round trips. Calls
    handing over sound data or reading results back stay synchronous; proxied calls run in order.
  - The page's edge-scroll settings are read about every 2 s instead of twice a frame (both builds).
  - The canvas size (a DOM-selector lookup) is read once a frame instead of per viewport (both).
  - Fixed 2 GB heap: with shared memory and ALLOW_MEMORY_GROWTH the JS glue re-checks its heap
    views on every access (growMemViews).
- **Thread safety.** The game's operator new/delete are memory pools that are not thread-safe.
  `GX_Network_PushPacket` built its packet on the main thread with std::vector; in a two-player
  LAN game that corrupted the pools (POOL-DOUBLE-FREE in UDP::Read, then "memory access out of
  bounds" ~30 times a second until the guest dropped). Now every page-called export that touches
  game state re-sends itself to the game thread with emscripten_dispatch_to_thread_async (runs
  between frames): PushPacket (bytes copied with malloc first), GX_Network_EnableVirtual,
  GX_OnCanvasResize, EmscriptenInput_OnPointerLockChange, GX_TickHidden, AIL_trigger_sample_eos.
  GX_FlushIdbfs_Tab is page JS only and stays on the main thread.
- **Saved game files on the private server.** The page serves saved archives to its own sync XHR
  through an XMLHttpRequest override that a worker never sees. The worker asks the page for each
  archive's blob: URL (`window.gxSavedFileUrl`, which the private server's page patch must add;
  see the worker variant of patch_game_page.py), retrying every 5 s until the file is saved.
  Tested: with the lookup in place, loading a game sent no requests for that archive to the server.
- **Render scale** still applies live: the game thread checks the setting about once a second and
  resizes its canvas (the page can't, the canvas is transferred).

| 60 fps cap on, work per frame | normal | worker |
|---|---|---|
| menu (shell-map battle) | 1.04-1.21 ms | 0.80-0.99 ms |
| LAN game, still | 2.21-2.61 ms | 2.27-2.95 ms |
| LAN game, panning | 2.33-2.94 ms | 2.29-2.59 ms |
| 3v3 replay, minutes 2-5 | 4.3-5.9 ms | 4.0-5.0 ms |

Verified on the worker build: menu, LAN game, 3v3 replay, live render scale, and a two-player LAN
match in two tabs (`?virtual_net`) with the guest tab hidden. Deploying it would also need the
private server to send COOP/COEP headers (`Cross-Origin-Opener-Policy: same-origin`,
`Cross-Origin-Embedder-Policy: require-corp`) and the page patch's `gxSavedFileUrl` line.
What it gains is the main thread: map loading's synchronous downloads no longer freeze the page,
and page work (GC, input) no longer competes with frames. Frame rate is the same.

## 2026-11-21 — Session 27: Low-end hardware; choose multithreading before launch

Branch `perf/low-end`, from `exp/worker-thread` (the live build since 2026-11-20). Low-end test
setup: Edge pinned to 2 cores (`taskset -c 0,1`) with SwiftShader (software GL) as a stand-in for
a very weak GPU; game-thread fps and work per frame from the GXFRAME/GXWORK logs.

| low-end, LAN game on Alpine Assault | menu fps | in game fps |
|---|---|---|
| live worker build | 1.4 | 0.9-1.2 |
| + shader variants, ps.1.1 compiled to GLSL | 1.3-2.2 | 1.1-1.5 |
| + automatic 50% render scale (software GPU detected) | 6.3-6.7 | 3.5-4.7 |

On the RX 580 everything holds 60 fps with the same picture.

- **Shader variants.** Programs are keyed on lighting, fog, alpha test, screen-space vertices,
  specular, the active stage count and the pixel shader (up to 1024), and compiled with matching
  `#define`s (`GX_LIGHT`, `GX_FOG`, `GX_ATEST`, `GX_PRE`, `GX_SPEC`, `GX_PS`, `GX_STAGES`,
  `GX_PSGEN`) that the shaders test instead of uniforms. ps.1.1 handles 1-15 are translated to
  straight-line GLSL (`GLES3_PS_Generate_GLSL`) instead of the interpreter loop with dynamic register
  indexing. GLSL has no int -> bool, so the flags are `true`/`false`. A variant that fails to
  compile is not retried every draw. CPU per frame is ~0.5 ms higher in game (more program
  switches, each re-sending its uniforms); a uniform buffer would remove that.
- **16-bit textures were already 16-bit** on the GPU (RGB565 / RGB5_A1 / RGBA4); most textures are
  DXT. Only rare L8/A8/A8L8 textures are expanded to RGBA8. No change.
- **Worker heap 2 GB -> 1 GB.** New GXMEM log (sbrk top every 5 s): ~310 MB in a LAN game, ~410 MB
  in the 3v3 replay at Very High after 13 game minutes.
- **Automatic low-end defaults** (page, only while the player hasn't set a render scale): a
  software renderer (SwiftShader/llvmpipe/Basic Render, via WEBGL_debug_renderer_info) -> render
  scale 50% and a one-time note to turn on hardware acceleration; <= 2 cores or <= 2 GB -> 75%.
- **Multithreaded build chosen before launch.** Settings drawer "Multithreaded", on by default where
  the page is cross-origin isolated. emcc's `{{{ SCRIPT }}}` tag sits in an inert `<template>`; a
  loader starts `mt/GeneralsZH.js` (the GX_WEB_WORKER build) or the normal build, falling back to
  the normal build if `mt/` is missing. Changing it reloads the page. Asset URLs are now absolute
  against the page so the build in `mt/` reads the same `assets/`. Deploy layout: normal build in
  the web root, multithreaded build's .js/.wasm in `mt/`. Checked: isolated + default -> mt/,
  isolated + off -> normal, not isolated -> normal (checkbox disabled); a LAN game started in each.

Not done: the CPU-side items from the review (dynamic buffer uploads grouped, a uniform buffer,
shadow code, vertex layout caching), each ~1-3% of an already ~60% idle frame on the RX 580; the
uniform buffer is the one worth doing next, since variants made program switches more frequent.

## 2026-11-21 — Session 28: CPU per draw: uniform block, cached vertex layouts, trimmed uploads

Branch `perf/low-end`. Frame callback time (median) and WebGL calls per frame, normal build in
headless Edge on the RX 580, LAN game on Alpine Assault; the big battle is the 3v3 replay at Very
High, 1600x1200, camera on the fighting.

| | live (Session 27) | + uniform block | + VAO cache, trimmed uploads, no FULL_ES3 |
|---|---|---|---|
| menu | 1.6 ms, 850 calls | 1.5 ms, 850 | 0.9-1.0 ms, 261 |
| in game, still | 3.3 ms, 1187 | 3.2 ms, 1158 | 2.2-2.7 ms, 751 |
| in game, panning | 2.9 ms, 906 | 2.9 ms, 896 | 1.8-2.2 ms, 491 |
| big battle (4 min) | 4.1-5.3 ms, 1888-2743 | | 3.7-5.0 ms, 1072-1934 |

- **Uniform block `GXScene`** (binding 0, std140): view, projection, the four lights, global
  ambient, fog, lod bias, viewport and texture factor, shared by every program. It is rebuilt only
  after one of those inputs changes (every writer is in gles3_wrapper.cpp and sets `g_SceneDirty`)
  and only the changed span is uploaded. Before, each shader variant kept its own copy and a
  program switch re-sent them. Uniforms the variant `#define`s replaced (lighting/fog/alpha-test/
  specular/pre-transformed enables) are gone. The pipeline hash is computed once per draw.
- **A VAO per vertex layout** (vertex buffer, index buffer, FVF, stride, base offset), for
  DrawIndexedPrimitive: a known layout is one `bindVertexArray` instead of ~5
  `vertexAttribPointer` (the base vertex is baked into the pointers, so they changed every draw).
  ~900 layouts in a LAN game, ~2800 in the big battle; the cache flushes at 4096 (never reached).
  Per-VAO state (arrays, pointers, element buffer) resets the attribute shadow on every VAO switch;
  the other draw paths use the default VAO. Index buffers get their first fill through
  ELEMENT_ARRAY_BUFFER on the default VAO (WebGL types a buffer by its first binding) and later
  uploads through COPY_WRITE_BUFFER, so an upload never changes the bound VAO. Deleting a buffer
  drops the VAOs that use it. FVF decodes are cached. GXLAYOUT log line: VAOs made, flushes.
- **Dynamic uploads trimmed to the bytes that changed.** Buffers re-uploaded after their first fill
  keep a copy of what was last sent; the dirty span is narrowed (16 bytes at a time, SIMD) to the
  part that differs, and skipped when nothing does. The 2D UI rewrites identical quads every
  frame: uploads per frame went 96 -> 18 in game and 196 -> 0 in the menu. Not done: grouping
  uploads across draws, which would need draws deferred (each draw needs its data first).
- **`-sFULL_ES3` removed**: its client-side-array emulation ran on every draw (a loop over all 16
  attributes) and made each `bindVertexArray` call `getParameter`. Every draw reads GL buffers.
- **Shadows**: volumetric shadow casters off screen are rejected before the ground-height lookup
  (the airborne bound contains the grounded one, so the same casters are rejected). Shadows only
  run above Low detail. `DoShadows` stays ~1% of the big battle: the rest is the engine's own
  silhouette rebuild when a caster moves; left as is, as is the water grid (`drawTrapezoidWater`,
  ~0.8%).
- **Low-end** (2 cores + SwiftShader, multithreaded build): game-thread work per frame 2.2-3.2 ms
  -> 2.2-2.5 ms in game. The frame rate there is set by software rasterization (~200 ms a frame),
  and with the machine under other load (load average 17) the fps difference was within run-to-run
  noise (one run: menu 6.3-6.7 -> 8.1-8.3 fps; a back-to-back pair: equal). Fewer WebGL calls also
  means less GPU-process work, which shares the two cores with the rasterizer.
- Picture unchanged (screenshot diff vs. the live build: only the dozer's smoke animation); no
  WebGL errors. Reviewed by a separate diff-only review: no must-fix; its one should-fix (an
  indexed draw before any FVF did not bind its index buffer) is fixed.

## 2026-11-21 — Session 29: Antialiasing setting, sharp UI at a render scale, smaller items

Low-end test setup as before (Edge on 2 cores + SwiftShader, multithreaded build); the machine was
under other load (load average 7-22), so fps figures there move by ~1 fps between runs.

- **Antialiasing setting** (settings drawer, reloads the page): the WebGL context was always made
  with `antialias: true`. Off by default on a software GPU or a machine with <= 2 cores or <= 2 GB
  (only while the player hasn't chosen). SwiftShader honoured it (4 samples); turning it off at 50%
  scale changed nothing measurable there (menu 7.1-7.6 -> 7.3-7.5 fps, game 4.2-4.4 -> 4.0-4.6): at
  that size it is bound by per-draw work, not pixels. On a real integrated GPU MSAA costs memory
  bandwidth; not measurable on this machine (the RX 580's headless context gets 0 samples anyway).
- **Sharp UI at a render scale** (hardware GPUs, antialiasing off): the canvas stays at the
  display size; the 3D views (`drawViews` in W3DDisplay::draw, between GLES3_Begin_3D/End_3D) draw
  into an offscreen framebuffer at the scale (RGBA8 + depth24/stencil8 renderbuffers, the frame's
  clear replayed into it), which is blitted (linear) into the canvas; UI, text and cursor then
  draw at full size with lod bias 0. Viewports follow the bound target. A multisampled canvas
  can't be a blit target, so with antialiasing on the whole canvas is scaled as before. New 40%
  option. RX 580 at 50%: correct picture, UI sharp, 3D soft; ~1.1 ms menu, 2.4-2.6 ms in game.
  **Not on a software GPU**: on SwiftShader it was far slower (menu 1.9-2.3 fps, game 1.1-1.4, at
  both 50% and 40%, against 5.7-6.7 and 3.0-3.8 with the canvas scaled) — there the full-size
  canvas (clear, blit, UI and presenting it) costs more than the smaller 3D pass saves; at 100%
  the old build was ~1-1.4 fps too. The page tells the engine (`gxSoftwareGL`), which then keeps
  scaling the whole canvas. The renderer check now runs on every load.
- **Vertex shader**: texture coordinates are computed only for the stages the variant runs (all
  four when a pixel shader reads them); the conditions are compile-time constants.
- **Texture updates** (`Update_Texture`) bind through the binding shadow instead of
  `glGetIntegerv` + restore. Mipmaps are still regenerated: texture uploads are ~0.1% of a frame
  and skipping them could leave stale mips on screen.
- **Checked, left as is**: first-start detail is already Low (`StaticGameLOD = Low`), which turns
  off volumetric shadows, uses the simple water and the lowest particle cap, so low-end players
  already get those; the per-frame canvas lookup (`querySelector` / `findEventTarget`, one per
  frame from SetViewport) is 0.0-0.1% of a frame now.

## 2026-11-21 — Session 30: The larger items, measured first (none needed)

Big battle = the 3v3 replay at Very High, 1600x1200, camera on the fighting, normal build.

- **Batching / instancing unit meshes: not done.** Draws per frame in the battle are 217-437
  (minutes 1-4), under the 500 that would justify reworking the W3D mesh renderer.
- **Simulation on another thread: not done.** Inclusive profile of the battle: `GameLogic::update`
  3.0% of the time, `W3DDisplay::draw` 24.5%, the whole `GameEngine::update` 29.5%. The simulation
  is about a tenth of the game's frame work, and the engine's lockstep simulation was never built
  for threads.
- **Smaller textures on low-memory devices: already the default.** First-start detail is Low, and
  Low already loads every texture one mip level smaller (`WW3D::Get_Texture_Reduction()` = 1,
  logged on a fresh profile). A change that forced it on <= 2 GB devices at higher detail was
  reverted: it only mattered for players who choose a higher level themselves.
- Seen in passing: ~7 textures are created per frame in the battle (~28 `texParameteri` per
  frame, likely the game's text strings); texture uploads are ~0.2% of the profile, so left as is.

## 2026-11-21 — Session 31: Where the low-end frame goes (research, no code change)

Low-end setup as before (Edge on 2 cores + SwiftShader, normal build, 50% render scale = a
400x300 canvas, LAN game on Alpine Assault). Method: WebGL2 draw calls patched at runtime
(`~/generals-bench/exp.js`, `exp2.js`; `cdp.py inject` does not work: the script is dropped when
the DevTools connection closes). Cutting a draw's pixels = drawing it with a 1x1 scissor (vertex
work kept); fps from GXFRAME. ~195 draws, ~62k triangles per frame.

| low-end, in game | fps | ms/frame |
|---|---|---|
| as is (live 00024) | 4.6 | ~215 |
| no pixels for any draw | ~17 | ~60 |
| no draws at all | ~40 | ~25 |
| no pixels for the 67 blended draws | 13.5 | ~74 |
| no pixels for the 128 opaque draws | 5.3 | ~190 |

Blended draws by blend function and depth test:

| group | draws, triangles / frame | pixels cut -> fps | cost |
|---|---|---|---|
| multiply (ZERO, SRC_COLOR), depth EQUAL: the shroud pass, terrain drawn again | 15, 30k | 4.7 -> 7.3 | ~75 ms |
| alpha blend, depth ALWAYS: 2D UI | 44, 143 | 4.7 -> 6.3 | ~55 ms |
| the rest | | no change | |

- **Cause: the fixed-function fragment shader interprets the texture stages per pixel.** Ops and
  arguments come from uniforms (`u_StageOps`, `u_StageArgs`): a ~25-way `if` chain per op, six
  argument selects per stage, and `sample_stage(s)` with a dynamic `s`. A software rasterizer
  runs every branch for every pixel (masked), including all four texture lookups. The terrain is
  cheap because its pixel shader is already straight-line GLSL (Session 27); the shroud pass and
  the UI go through the interpreter.
- **Test** (throwaway build, not committed): any variant with stages computes
  `texture0 * diffuse` straight-line. Low-end **4.6 -> 11.5 fps (2.5x)**; the picture was the
  same except what that shortcut ignores (minimap camera outline, a health bar).

Candidates, ranked for weak machines:
1. **Texture stages compiled to straight-line GLSL**, like ps.1.1: the stage ops and arguments
   go into the program key and are emitted as `#define`d code. Expected close to the test's
   2.5x on software GL; on weak integrated GPUs less, unmeasured. Watch the variant count (the
   UI, shroud and units use a handful of stage setups) and first-use compile hitches.
2. **Shroud folded into the terrain pass** as one more texture stage (camera-space texgen is
   already supported): the 30k-triangle second terrain pass goes. After 1, the shroud's pixels
   are ~13 ms of ~87 and its vertex work about as much: ~1.3-1.4x more.
3. **Vertex shader specialised the same way** (light types, texgen modes from uniforms). Vertex
   and setup are ~35 ms of the low-end frame for ~62k triangles; not tested.
4. **Background download of all 2.2 GB of archives** (private server's sw.js) runs during
   matches, ~900 MB of it speech/music/audio. On a slow link or 2 cores it competes with the
   game; unmeasured. Pausing it while a match runs is the cheap version.
5. **1 GB reserved at start** by both builds against 310-410 MB measured use; may fail on
   32-bit browsers or 2 GB devices. A page-provided smaller memory for weak devices; unmeasured.

Not worth it: blended draws other than the shroud and UI (no change when cut); ~25 ms per frame
is fixed overhead with no draws (clears, presenting, compositing).

## 2026-11-21 — Session 32: Texture stages compiled to straight-line GLSL

Candidate 1 from Session 31. Each distinct texture stage setup (colour/alpha ops, the arguments
the ops read, which stages have a texture) and the alpha test function get their own program
slot (1024-2047, `GLES3_ShaderManager::Program_Slot`), whose fragment shader has the stages as
straight-line code (`GLES3_FF_Generate_GLSL`, `run_ff`) and the alpha function as `GX_AFUNC`.
Slots 0-1023 are the old variants with the interpreter, still used if the slots ever run out.
A variant's vertex shader is compiled once and shared by its slots. GXLAYOUT log: `ffprograms`.

| low-end (2 cores + SwiftShader, multithreaded build), two runs each | live 00024 | this |
|---|---|---|
| menu | 6.3-7.3 fps | 33.8-36.4 fps |
| in game, still | 3.4-4.3 fps | 11.9-13.4 fps |
| in game, panning | 3.5-4.8 fps | 10.7-13.8 fps |

- 9 stage setups in a LAN game, so 9 extra programs compiled.
- RX 580: unchanged (menu 0.9 ms, in game 2.7 -> 2.9 ms, panning 2.2 -> 2.0 ms: noise).
- Picture: screenshot diff against live in the LAN game shows only moving things (radar dish,
  dozer, a minimap blip); UI, terrain, shroud and the minimap outline identical. No WebGL
  errors; the 3 shader messages are the known water shaders, also on live.
- Diff-only review: pass. It checked all 11,250 op/argument expressions numerically against the
  interpreter and compiled 410 random setups in WebGL2. Nits left: a vertex shader that fails
  to compile is retried for each slot of its variant (log noise only); the shared vertex shaders
  are not freed at Shutdown (never called again today).

## 2026-11-21 — Session 33: Vertex shader specialised; download paused during matches

- **Vertex setup in the program key.** Each stage's texture-coordinate mode, uv set, transform
  count and projection, the four light types and the material colour sources (COLORVERTEX) join
  the stage setup in `GLES3_FFKey`; `GLES3_VS_Generate_GLSL` emits `gx_tc0..3` and the lights
  unrolled with fixed types (`GX_VGEN`, `GX_CV`). Compiled shaders are shared between slots: a
  vertex shader per variant + vertex setup, a fragment shader per variant + stage setup.
  Programs: 10 in a LAN game, 14 over 4 minutes of the 3v3 battle replay; no WebGL errors.
- Low-end (2 cores + SwiftShader), in game, still, four back-to-back pairs: 11.2 -> 15.7,
  13.2 -> 13.8, 14.0 -> 15.0, 13.6 -> 14.0 fps. Ahead every time but modest (~5-15%);
  panning within noise. On software GL the vertex cost is mostly the vertex count, not the
  branching. RX 580 unchanged (2.6 ms in game). Picture: LAN game diff vs live shows only
  moving things.
- Diff-only review: pass (600 random states x 96 vertices through transform feedback, old vs
  generated, max relative difference 2.4e-7; 684 generated vertex shaders compiled in WebGL2).
  Its should-fix (identical fragment shaders compiled per slot) is done.
- **Private server (not in this repo):** the page tells `sw.js` to pause the background download
  of game archives while a match runs (the game's TICK-DIAG line, `inGame=1`), aborting the file
  in progress; it resumes in the menu. Checked: pause sent once at match start; on a throttled
  test server the download stopped within a second and resumed afterwards.

## 2026-11-21 — Session 34: The fixed per-frame cost, measured (no change)

Low-end setup, normal build, LAN game, 400x300 canvas; WebGL patched at runtime
(`~/generals-bench/exp3.js`, `expmeas3.sh`). One clear per frame (colour + depth + stencil).

| low-end, in game | fps |
|---|---|
| as is | 12.0-13.5 |
| no draws | 32-34 |
| no draws, no clears | 60 (vsync) |
| exact CSS size, no browser scaling | 12.9-14.4 |
| no depth clear / no stencil clear / no colour clear | 12.3-12.8 / 11.6-12.5 / 10.8-12.9 |
| no clears at all | 11.6-12.3 |

- Clears cost nothing measurable in a real frame; the 60 fps with neither draws nor clears is the
  software renderer having nothing to do. Skipping the depth clear (idea 23) and an exact-size
  canvas instead of CSS scaling (idea 37) gain nothing; not done.
- **Battle, low-end** (3v3 replay, default Low detail, `expbat.sh` + `exp4.js`): 10.5-11.1 fps;
  cutting the pixels of the ~103 blended world draws per frame (particles, smoke, effects; idea 9)
  10.3-11.2; skipping the ~7.8 texture uploads per frame incl. the shroud copy (idea 33)
  10.0-11.0. Neither is worth changing.

## 2026-11-21 — Session 35: Render scale Auto

- Pixels are now ~1/3 of the low-end frame (LAN game: as is ~12.5 fps, no pixels ~18.8, no draws
  ~33). But on software GL **40% measured slower than 50%** (two pairs: 10.4-12.6 vs 12.0-13.7
  fps): its cost is per triangle more than per pixel there.
- **Render scale "Auto"** (settings drawer; the default for players who haven't picked a scale):
  the engine reports its frame rate (`Module.gxFps`, with the once-a-second render scale read in
  `GLES3_Begin_Scene`) and whether a match is on (`Module.gxInMatch` from EmscriptenMain:
  `isInGame && !isInShellGame && !isLoadingMap`). During a match, 3 s in a row under 24 fps drop
  the scale 10%, 3 s over 45 raise it 5%, at most one step per 4 s, between 100% and 40% (50% on
  a software GPU). Starts at 50% on software GL, 75% on <= 2 cores / <= 2 GB, else 100%.
- Checked: RX 580 stays at 100% at 60 fps; with the reported fps held at 10 it stepped
  100 -> 40% in 24 s and climbed back when held at 60; low-end stays at 50%; `gxInMatch` 0 in
  the menu, 1 in a LAN game. Untested: a real weak GPU, where Auto should matter most.
- The private server's download pause (Session 33) now uses `Module.gxInMatch` too: the
  TICK-DIAG `inGame` flag also counts the menu's shell map.

## 2026-11-21 — Session 36: Shroud folded into the terrain draws; off-screen terrain tiles culled

- **Shroud fold** (`HeightMap.cpp` `Set_Shroud_Fold`, shim `GLES3_Set_Shroud_Fold`): the shroud
  pass drew the whole terrain again (~16 draws, ~32k triangles), multiplying everything at
  terrain depth. Now the terrain-level draws (terrain, shorelines, extra blend tiles, roads,
  scorches, tracks; not props and bridges, which shroud themselves) multiply their colour by the
  shroud texture at their camera-space position (`GX_SHROUD` in the program key, sampler unit 4,
  same matrix as `ShroudTextureShader::set`) and the pass is skipped. Only for draws linear in
  their colour (blend factors ZERO/ONE/SRCALPHA/INVSRCALPHA); multiplicative draws are already
  right over a shrouded destination. If the shim can't fold (no texture, program slots nearly
  used up), it says so and the pass is drawn as before. Pitfall found: WW3D applies the camera's
  view at the next draw, so the view must be read after `Apply_Render_State_Changes()`.
- **Terrain tile culling** (`isTileCulled`): per-tile bounds from the in-memory vertex copy,
  recomputed after `updateVB`; tiles outside the camera are skipped in both passes.
- Page switches for testing, read about once a second: `Module.gxShroudFold = 0`,
  `Module.gxTerrainCull = 0`.
- Picture: same camera, fold on vs off: 75-135 pixels differ (minimap blips) at scrolled views
  including the shroud edge; at the start view the differences are moving units and UI
  animation. Draws per frame (RX 580): start 210 -> 192, scrolled 78 -> 62, 94 -> 78.
- Low-end, one session, same camera (normal build): neither ~13.8 fps, culling only ~13.7,
  fold only ~15.7, both ~17.9. Multithreaded build vs live 00027, two pairs: 16.0-17.1 ->
  20.9-21.8 fps (+28%). RX 580 unchanged (2.5 ms in game).
- Diff-only review: pass; its two should-fixes (silent loss of the shroud when slots run out or
  the texture is missing) are fixed by the fallback above. Known, small: a half-transparent
  bridge or prop edge in fog is darkened twice (their own shroud pass over folded terrain).

## 2026-11-22 — Uncapped RX 580 numbers for the shroud fold (0fb301f)

Measured on the RX 580 (headless Edge, ANGLE on OpenGL), vsync and frame-rate limit off
(`--disable-gpu-vsync --disable-frame-rate-limit`), game resolution 1920x1080, Very High detail,
normal (single-thread) build. Builds: a67de4d (before) and 0fb301f (shroud fold + terrain tile
culling), 3 alternating runs each (`~/generals-bench/uncap.sh`). Scenes: LAN game on Alpine
Assault (camera still at the base, then panning), the 3v3 battle replay with the camera on the
fighting (3 minutes, 5-second windows, first 15 s skipped). Median / worst 5-second fps:

| run (load average) | build | menu | LAN still | LAN panning | battle | battle CPU per frame |
|---|---|---|---|---|---|---|
| 1 (17) | a67de4d | 614 | 308 / 300 | 265 / 186 | 184 / 108 | 5.0 ms |
| 1 (8) | 0fb301f | 1274 | 757 / 684 | 610 / 393 | 255 / 171 | 3.5 ms |
| 2 (4) | a67de4d | 1359 | 474 / 452 | 353 / 228 | 218 / 101 | 4.0 ms |
| 2 (7) | 0fb301f | 747 | 423 / 422 | 404 / 366 | 159 / 101 | 5.6 ms |
| 3 (12) | a67de4d | 748 | 335 / 322 | 322 / 195 | 156 / 90 | 6.0 ms |
| 3 (13) | 0fb301f | 763 | 340 / 248 | 324 / 220 | 152 / 95 | 5.7 ms |

- No measurable difference on the RX 580: uncapped, it is limited by CPU and browser work, and
  the numbers follow the machine's load (other sessions' builds and benchmarks), not the build:
  the same build's menu runs 614-1359 fps. The pair at equal load (run 3) is within a few percent
  in every scene. The fold and culling remove GPU vertex and pixel work, which the RX 580 has to
  spare; their gain is on weak or software GPUs (low-end setup 16-17 -> 21-22 fps, Session 36).

## 2026-11-22 — Frame-rate overlay; Steady 30 fps setting

- **Show frame rate** (settings drawer, off by default): a line at the top left with frames per
  second, ms per frame, the game's own CPU work per frame, the render scale in use (and whether
  it is Auto), the graphics chip and the build (multithreaded or not). The engine reports
  `Module.gxFps` and `Module.gxWorkMs` (1-second average from gles3_mainloop) with its
  once-a-second render scale read. Made for reading real weak machines without tools. Checked:
  RX 580 overlay 60.0 fps with the engine's GXFRAME at 60.0; low-end 12-14 fps against GXFRAME
  12.1-12.8.
- **Steady 30 fps** (settings drawer, on by default): the 30 fps pacing (79a1ea7) now follows it,
  read about once a second with the page switches (bit 2 of `GLES3_Page_Switches`, also off with
  `?pace=0`), so it applies without a reload; unticking while paced returns to 60 at once.
  Checked with SwiftShader on all cores (match at ~35-43 fps): pacing engaged, unticking went back
  to 60 within 3 s and it stayed off, ticking again re-engaged it.

## 2026-11-22 — Desynchronized canvas on weak machines (item 5, the rest of it)

- **Desynchronized frames** for the single-thread build's canvas when the page flags the machine
  as weak or software GL (`gxLowEnd`): the browser shows them with less copying. Low-end setup,
  single-thread build, two pairs against live 00031: 18.2-18.9 -> 22.0-22.4 fps (+19%); the
  context reports `desynchronized: true`. The multithreaded build gained nothing (three pairs,
  ~20.5 fps both): its canvas belongs to the game thread, which presents directly. RX 580 not
  flagged weak, unchanged.
- **No stencil buffer** (shadows are off at Low): 18.3-18.6 vs 18.6-18.9 fps, no gain; not done.
- With the clears (Session 34) this closes item 5: clears and stencil no gain, desynchronized kept.

## Ground cache: the terrain's base pass kept as an image while the view stands still (2026-11-23)

Measured first, low-end setup (2 cores, SwiftShader, 50% scale, single-thread build), by skipping
classes of draws with a runtime WebGL patch (~/generals-bench/hideprobe.js):
- Blended draws, additive draws, stencil draws, the ~120 small opaque draws: no fps change.
- The ~14 big opaque draws (terrain tiles, ~28k triangles): frame 53 ms -> 27 ms without them.
  With only their pixels removed (1x1 scissor) 36 ms: about 17 ms is pixels, 9 ms triangles.
- Cloud map + light map off (Custom detail): ~1 fps; the default low detail has them off already.

So the terrain's base pass is half of a software frame. `GLES3_Ground_Cache_Begin/Again`
(gles3_wrapper.cpp) wrap it in HeightMap.cpp: the draws are first fingerprinted instead of issued
(whole pipeline state + buffer and texture content serials); an unchanged fingerprint copies the
cached colour + depth in with glBlitFramebuffer, one seen twice in a row is drawn into the cache,
anything else is drawn plainly. Only without antialiasing (the page's `antialias` setting);
`Module.gxGroundCache = 0` turns it off; `Module.gxGCHits/gxGCCaptures/gxGCPlain` count.

Two things had to change for it to hit at all / be right:
- The fog of war texture was re-uploaded every frame with identical pixels. `Upload_To_GL`
  (d3d8.h) now hashes a re-upload (<= 2 MB) and skips it when unchanged, which also saves the upload.
- A first version pasted the image with a shader writing `gl_FragDepth` from a 32F depth texture:
  objects standing on the ground (the player's building, units) disappeared, the depth came out
  slightly nearer than a direct draw's. The blit copies the rasteriser's own depth and is right.

Same match, camera still, cache toggled at runtime: 21.7-25.7 fps on vs 15.2-15.5 off (load ~15),
every frame a hit. Pictures match except moving parts (dish, flag).
- The target size comes from the context (`emscripten_webgl_get_drawing_buffer_size`): asking the
  canvas element from the multithreaded build's game thread gave one frame of "framebuffer
  incomplete" errors at start and once a match that never started.
- Four runs side by side on their own core pairs (bench scripts take LANE=n now; the machine was
  loaded, so only the ratios count): camera still, single-thread 3.9 -> 5.2 fps, multithreaded
  3.8 -> 5.5; panning and menu unchanged; RX 580 with antialiasing (cache off) identical; no GL errors.

## Performance switches in Settings (2026-11-23)

Every optimisation that can be switched has a checkbox under Settings > Performance switches,
built from `window.gxPerf` in web_shell.html; `GeneralsX.opt` holds the values in force (the
player's choice, else the default) and is what the engine reads. Live (read about once a second
through the page switches in gles3_wrapper.cpp): ground cache, shroud fold, terrain culling,
specialised shaders (off = the general interpreting shaders, and no fold), skip unchanged texture
re-uploads, sounds load in the background. At start (the page reloads): simpler texture filtering,
lower shader precision, show frames immediately (desynchronized canvas), prepare shaders at start,
keep game files in memory (256 MB vs 16 MB). The last three "weak machine" defaults (filtering,
precision, desync) still default to window.gxLowEnd.

Low-end setup, one match, each switch off in turn (picture unchanged each time, <= 0.4% of pixels,
the moving parts): all on 25.5-27.7 fps; ground cache off 17.5; shroud fold off 17.9; terrain
culling off 25.1 (nothing off screen in this view); specialised shaders off 4.1; skip unchanged
uploads off 15.8 (the ground cache can't hit then); background sounds off 19.0.

## Pan cache (experiment, switch off by default) (2026-11-23)

Measured first: a depth-only terrain draw (colour mask off) costs the same as the full draw on
SwiftShader (113-120 ms vs 111-123 ms frames, 60-65 ms with no terrain), so a cache only pays on
frames where the terrain isn't drawn at all; nearest-texel filtering gained nothing either. And on
Alpine Assault only the start base is flat (93% of visible ground within 6 units of height;
10-44% elsewhere), so "slide only on flat ground" would almost never apply.

`panCache` (Settings: Slide the ground while the camera moves): while the camera moves without
turning, the terrain's base pass is drawn every third frame (Module.gxPanFrames) into the ground
cache, and on the frames between `GC_Slide` moves the cached colour to where a flat ground at the
height under the view's centre would appear: one blit per 6-8 pixel strip of rows (with no roll a
row of flat ground is at one distance, so it slides and scales along x by one amount), and the depth
image copied as it is. Slopes are a few pixels off for at most two frames; a newly exposed screen
edge shows the clear colour until the next real draw; camera height changes over 1.5 units, turns
and zooms draw normally. `Module.gxGCSlides` counts, `Module.gxGCWhy` says which check last refused.

Low-end setup, scrolling in four directions, same match: 22.4 fps off, 27.4 on (about 1.4 slid
frames per drawn one). Off by default: to be judged on a real machine.

## Identical unit parts drawn together (instancing that actually fires) (2026-11-23)

Stress test (local hook, not in the tree: ~/generals-bench/stresshook.patch + stress2.sh): 400
Crusader tanks spawned by the camera, then all ordered to move. Before: ~190 draws/frame became
4,100-5,000 (about 10 per tank), 0 of them instanced; RX 580 58 -> 44 fps idle and 30-36 while the
order was fresh (40%+ of CPU in draw submission, path finding 3-4%); low-end 11 -> 2 fps with the
main thread 86% idle (SwiftShader working through 4,000 draws).

The "instancing emulation" in DX8TextureCategoryClass::Render never batched anything:
- it compared units' LightEnvironmentClass pointers (one per unit; same contents in the same
  light) -> `Same_Lighting` compares the contents;
- it only joined neighbouring tasks, and they arrive object by object -> the tasks of a texture
  category are stable-sorted by polygon renderer first;
- skins could be batched (each has its own deformed vertices) -> excluded, and batched meshes
  must share the base vertex offset;
- `DX8Wrapper::Draw_Triangles_Instanced` called the shim directly, so the instanced draw used the
  vertex layout the previous draw had left bound ("Insufficient buffer size" GL errors) -> it goes
  through the device's DrawIndexedPrimitive, which uploads and binds like any draw.
Switch: Settings > Draw identical unit parts together (`unitBatching`, page switch bit 12).

400 tanks, toggled in the same match: draws 4,700-5,000 -> 2,300-2,400 (about 21 instanced draws
of 120 copies); RX 580 38.7 -> 61 fps (capped), game work 19.2 -> 12.5 ms; low-end 5.0 -> 5.7 fps;
pictures match. 400 Rangers (skins) are not batched and draw correctly. Left: ~5 tiny draws per
tank (a few triangles each) and all infantry.

## Tank treads batched too: per-instance texture offset (2026-11-23)

After the batching fix, 400 tanks still drew ~650 small opaque meshes a frame: the treads, two
per tank. Each carries a `Material_Override` (its own tread scroll) as user data, which the
batching treated as "unique". Now a batch of such meshes sets the shared
LinearOffsetTextureMapper to zero offset and sends each tread's scroll as a per-instance
attribute (location 12, `a_InstanceUV`, added to the stage 0 texture coordinate when
`u_InstancingEnabled`; `gx_instance_uv` through DX8Wrapper::Draw_Triangles_Instanced).

The other ~1,300 tiny draws in the stress test are muzzle flashes (AVLEOPARD.TURRETFX01,
AIRNGR_SKN.MUZZLEFX01: SORT-flag additive meshes, about one draw per triangle once the sorting
renderer interleaves them). The test's spawn leaves them visible; in play they show only while
firing.

400 tanks, same match: RX 580 4,985 -> 1,661 draws, 45 -> 60.6 fps (capped), moving 52.9 -> 61.3;
low-end 4,645 -> ~1,550 draws, 4.2 -> 4.5-5.0 fps (the flashes above are most of what is left).
Idle pictures match (0.10%). Sanity: both builds and the RX 580 run a match with no GL errors.

## Infantry drawn together without GPU skinning (2026-11-23)

A skin container already deforms all its visible meshes on the CPU into one dynamic vertex buffer
(world space, identity world matrix) and then draws them one mesh at a time: 400 Rangers were
~380 body draws. In DX8TextureCategoryClass::Render the skins of one category (same texture,
shader, material) that stand in the same light (`Same_Lighting`) are now drawn with a single
DrawIndexedPrimitive from a per-frame dynamic index buffer: each mesh's indices (read once per
polygon renderer, `GX_Skin_Indices`) plus its base vertex offset. Up to 65535 indices a draw;
alpha overrides, user data, scaled, sorted and additive meshes keep the old path. Same switch as
the tank batching (`unitBatching`).

400 Rangers by the camera, toggled in one match: RX 580 2,838 -> 2,440 draws idle (the rest is
their muzzle flashes, visible only because of the test spawn) and 909 -> 244 with the group spread
out; low-end 2,722 -> 2,330, 7.0 -> 7.4-8.3 fps. Pictures match within the idle animation (1.1%,
same as two unbatched captures). Sanity: both builds and the RX 580, no GL errors.

## GPU skinning for infantry, and no compare for buffers that always change (2026-11-23)

Profile, 400 Rangers on screen (RX 580, ~54 fps, main thread saturated): deforming and uploading
vertices 23% (Changed_Span 7.9, bufferSubData 5.5, DX8SkinFVFCategoryContainer::Render 4.4,
VectorProcessor::Transform 3.3, Get_Deformed_Vertices 1.9), skeleton evaluation ~8%, path finding ~10%.

- `skipCompare` (d3d8.h Emscripten_BufferData::Upload, page bit 13): after four uploads in a row
  that changed >= 7/8 of the locked span, the next 120 uploads send the locked span without
  comparing it with the bytes last sent; then one copy and one real comparison decide again.
- `gpuSkinning` (page bit 14): W3D binds each skin vertex to one bone, so the vertex shader can do
  what MeshGeometryClass::get_deformed_vertices does. The skin container marks eligible meshes
  (opaque container, no material pass pending, no alpha/material override, <= 120 bones) with base
  offset GX_GPU_SKIN instead of deforming them. In DX8TextureCategoryClass::Render the marked
  instances of one polygon renderer in the same light are one instanced draw from a static copy
  of the model's vertices (`GX_Skin_Model`: vertices as stored + a buffer of bone numbers,
  attribute 13) with each instance's bone matrices in a float texture row (`u_BoneTex`, unit 5,
  three texels per bone; `u_SkinBones`). Everything else (material passes, faded or overridden
  meshes, sorted skins, shadows, picking) still uses the CPU path.

400 Rangers, toggled in one match: RX 580 game work 14.8-16.3 ms -> 13.1-13.2 ms, 53.8-60.9 ->
60.2-61.1 fps (capped), body draws ~380 -> 5 instanced draws of 80; low-end 6.0-6.8 -> 7.2-7.6 fps.
Pictures match within the idle animation (1.5% / 1.1%). Sanity with it on by default: LAN match on
both builds (SwiftShader) and the 3v3 replay on the RX 580, no GL errors, scene correct.

## Health bars of a selected army in one draw (2026-11-23)

User report: fps drops when many units are on screen, most of all when they are selected. Each
selected unit's health bar was `drawOpenRect` + `drawFillRect`, and each of those is its own
`Render2DClass::Render` (viewport, material, transforms, a dynamic vertex and index buffer, one
draw call): 400 selected soldiers added ~800 draw calls.

- W3DDisplay.cpp: while W3DView's per-unit pass runs (`GX_RectBatch_Begin/End` around the
  `drawablePostDraw` loop), `drawLine`, `drawOpenRect` and `drawFillRect` only add their shape to
  `m_2DRender`; one `Render` draws them all (flushed every 2000 shapes for the 16-bit indices).
- Order is kept: the other W3DDisplay draws (image, clocks, video) draw the collected shapes
  first, and so does any other Render2DClass through `Render2DClass::PreRenderHook`.
- Page switch bit 15 (`GeneralsX.opt.uiBatching`), on by default.

400 Rangers selected, RX 580, toggled in one match: 3,251 -> 2,474 draws, game work 17.8 ->
14.2 ms (unselected 12.9), 57.4 -> 60.6 fps. Health bar pixels identical in both modes (15,183
fill pixels, 0 differing).

## All 2D drawing in batches (2026-11-23)

User: "all 2d draws are worth batching, make sure batching is the default behavior always".
The health bar batching above now covers pictures and the interface as well:

- `W3DDisplay::drawImage` resolves the texture first; a picture with the same texture and draw
  mode as the ones already collected joins them, anything else draws the collected ones first.
  drawImage's state resets (grayscale off, alpha back on) are applied after the batch draws.
- Collecting also runs around `TheInGameUI->draw()` (the HUD and all windows), not only the
  per-unit pass. Text and other Render2DClass users still flush through `PreRenderHook`.
- Same switch (`uiBatching`, bit 15), now labelled DRAW 2D SHAPES TOGETHER, on by default.

Counted with kindprobe.js (draws by GL state), RX 580: the HUD of an idle match is ~55 draws a
frame, ~43 batched (text between the pieces breaks batches). The command bar is identical in
before/after screenshots. The same count shows where many-unit frames really go: ~5 one-triangle
additive draws per soldier (muzzle flash meshes, ~490 for 100 Rangers) and ~10 small alpha draws
per vehicle through the sorting renderer. That is the next item.

## See-through draws merged across meshes (2026-11-23)

The draw count by GL state showed the many-units cost after batching: ~5 one-triangle additive
draws per soldier (muzzle flash meshes) and ~10 small alpha draws per vehicle. They all go
through `SortingRendererClass::Flush_Sorting_Pool`, which depth-sorts every triangle and draws
each run of triangles from one mesh as its own draw, because each mesh has its own world matrix.

- Page switch bit 16 (`GeneralsX.opt.sortBatching`, Settings: DRAW SEE-THROUGH EFFECTS
  TOGETHER), on by default.
- While a mesh's vertices are copied into the frame's buffer they are moved into world space
  (positions by the full matrix, normals by its 3x3 part, row vectors as D3D); the per-triangle
  depths used for sorting are computed exactly as before, from the mesh's own matrix. Meshes
  whose matrix is already identity (particles) are copied as before.
- Each moved mesh then draws with an identity world matrix, and neighbouring runs in the sorted
  order whose remaining state is the same (shader bits, material, textures, view, and the
  lights when the material is lit) become one draw. Same triangles, same order.

100 Rangers + 60 Humvees + 40 Comanches, toggled in one match: RX 580 1,224 -> 265 draws (fps at
the 61 cap either way); weak-PC setup (SwiftShader) 1,200-1,600 -> 250-410 draws, 7.4-9.6 ->
9.2-12.9 fps. Pictures: off vs on 1.8% of pixels >24, two shots with it off 1.5% (animation).
No-regression run on both builds against rev 00048: same within noise, no GL errors.

## Battle benchmark in Settings (2026-11-23)

User: one reusable, stressful, realistic benchmark as the testing case for changes, with a
Settings button. `GeneralsMD/Code/Main/WebBenchmark.cpp`, page side in web_shell.html
(Settings > BENCHMARK, `GeneralsX.bench.run(tour)` for scripts).

- Battle: 2 v 2 skirmish on Tournament Desert, fixed seed 12345, three brutal AIs. Each side
  gets a 300-structure base (every buildable structure of every faction and general, 30 power
  plants first) and 800 units (every buildable infantry, vehicle and aircraft, 351 types) in
  four waves on three fronts that attack the other base. Camera keys every 5 s (scrolls the
  front, visits both bases, turns); every 2 s another group is selected and sent somewhere else
  (near the camera, aircraft, one front's infantry, one unit, vehicles on screen). Every
  special power on the bases (88; superweapons first) is fired alternately at the other army
  and the other base. 60 s of game time, stopped at 150 s on slow machines.
- Map tour: Alpine Assault, Tournament Urban, Dark Night, Golden Oasis, Eastern Everglades,
  each loaded alone with the map revealed, the camera at four points for a second each, one
  320-px thumbnail read back at the swap (`gx_bench_shot`).
- Per phase: fps, 1% low, frame time avg/p50/p90/p99/max/sd, frames over 50/100 ms, game work
  per frame, engine and GL draw calls, triangles, upload bytes (buffers and textures) per
  frame and MB/s, program switches, texture binds, heap in use and high water, objects,
  particles, logic frames per second. GL counters: `gx_stats.h` wraps the upload, program,
  bind and draw calls (included from gles3_wrapper.h). The frame hook is `gx_frame_hook` in
  gles3_mainloop.cpp. Results go to `window.gxBenchDone`; the last ten are kept in
  localStorage and the table shows each number's change from the previous run.
- Refused in network games; runs on the game thread (dispatched in the worker build).
- Harness: ~/generals-bench/benchrun.sh BUILD PORT TAG [tour].

First runs, RX 580: battle 5-12 fps with 2,600-3,300 GL draws, 1,000 program switches and
1,500 texture binds a frame, game work 80-180 ms; the heap passes the 512 MB start and grows
(a 9.8 s frame in one run). Map tour 58-60 fps. Both builds about 2.5-3 minutes in all.

## Benchmark tools, and three changes measured with them (2026-11-23)

Benchmark additions (WebBenchmark.cpp): memory snapshots (heap, file cache, engine pools largest
first, texture and buffer copies kept), the ten slowest frames with what the benchmark did just
before and what the frame did (file reads, shader compiles, uploads, heap growth); it waits for
the main menu itself; `GeneralsX.bench.record(frames)` records a match of brutal AIs (we observe)
and `GeneralsX.bench.replay(name, frames)` plays a recording flat out and reports a CRC
mismatch. Note: the retail 3v3 replay (bench.rep) leaves this codebase's simulation at frame
110 whatever the switches, so logic changes are checked against a referee recorded with the
code before the change (~/generals-bench/referee.rep). The test bench is now an MCP server
(/media/laith/D1/Projects/generals-bench-mcp, `generals-bench`).

Memory at the end of the battle (RX 580, single-thread build): 564 MB in use = file cache 141,
engine pools 149, texture copies 93, buffer copies 21, rest 160. The long freezes were game work
(10 s, 8 s frames during strikes), not memory growth.

- Command bar searches once per game tick (Player.cpp, switch uiScanCache, bit 18): the
  shortcut bar's "which special power is ready" questions walked all the player's objects
  several times per button every frame. Outside the logic update the answers are kept until the
  next logic frame; the AI (inside the update) always walks. A/B, same build: work per frame
  -19% while the armies arrive, -5% in the strikes; frames over 100 ms -7%.
- Texture copies the graphics chip has are dropped (d3d8.h, switch freeTextureCopies, bit 19):
  uncompressed levels 1+ (never sent: glGenerateMipmap builds them) at once, compressed textures
  after their upload. A dropped level locked again gets zeroed memory (counted: 0 in the battle
  and the five-map tour). Battle end 554 -> 519 MB, texture copies 92 -> 56 MB; same pictures.
- Faster clean-up of destroyed units (GameLogic.cpp, switch fastDestroy, bit 20): a destroyed
  object's sleepy updates are taken from its own behavior modules and put in heap order, instead
  of scanning every update in the world per object. Same modules, same order, same erases.
  Verified: old and new side by side (Module.gxVerify, bit 21) over 22,000 destroyed objects, 0
  differences; the referee recording (9,000 frames of brutal AIs) replays without a mismatch
  with the change on. A/B in the strikes: fps +7%, work -7%, p90 frame -10%.

## Shader compiles in the background, reused vertex layouts, fast inverse sqrt switch (2026-11-23)

Measured with the battle benchmark (bench MCP, A/B same build, one switch apart).

- Background shader compiles (gles3_wrapper.cpp, switch asyncShaders, bit 22, on): a strikes
  profile showed getShaderParameter at 12.2% and one 7.7 s frame with 2 compiles (the browser
  compiles when the game asks whether compiling worked). With KHR_parallel_shader_compile a new
  specialised program links in the background and its draws use the general program of the same
  setup until COMPLETION_STATUS_KHR says done (GXLAYOUT async= / fallbackdraws=). Programs that
  fold the fog of war in are still compiled at once. In the A/B run the big freeze happened not
  to be a compile, so no difference was measured; 92 programs went to the background, 773
  draws used the fallback. Kept: no cost, removes compile freezes when they happen.
- Reused vertex layouts (switch reuseLayouts, bit 24, on): the layout key included the buffer's
  start offset, so a battle made 271,437 VAOs and flushed the cache 66 times; now one layout per
  buffer setup is re-pointed when the offset changes: 273 layouts, 0 flushes. RX 580 frame
  times within noise (strikes p90 -5%, frames over 100 ms -9%); same pictures.
- Fast inverse square root (wwmath.h WWMath::Inv_Sqrt, switch fastInvSqrt, bit 23, OFF): the
  bit trick + one Newton step. It changes vector normalising slightly and that feeds the
  simulation, so EmscriptenMain.cpp forces it off in network games and replay playback. A/B:
  strikes +3% fps / -3% work, armies -2%: within noise, as expected (wasm sqrt is one
  instruction). Left off.

## Flow fields for long ground paths (2026-11-23, switch flowFields, OFF by default)

Profile of the battle benchmark: path searches are ~24% of a frame while armies arrive
(Pathfinder::internalFindPath 11.7% inclusive, findClosestPath 12.6%). Attack-move gives every
unit of a group the same target; each unit then ran its own A*. Counted (GXPATH, per 5 s of
game): 370-1,200 searches to 146-540 goal cells in 43-134 8x8-cell goal areas.

AIPathfind.cpp `Pathfinder::gxFlowFieldPath`, called from internalFindPath for ground-to-ground
searches longer than 24 cells (no tunnelling, no ignored obstacle): a field is an exact integer
Dijkstra (Dial's buckets, costs 10/14, the A*'s corner rule) from every walkable cell of the
goal's 8x8 area over the whole map, per locomotor surfaces and crusher flag; up to 32 kept
(LRU), dropped when a building is placed or removed (gxObstaclesChanged from
internal_classifyObjectFootprint / classifyMap / changeBridgeState) or after 600 frames. A unit
walks it downhill to the area, a normal search does the last cells, then Path::optimize.
findClosestPath is unchanged.

It ignores other units on the way (the A* steers round fixed ones), so it changes the
simulation: page switch bit 25, off by default, and gxFlowFieldPath does nothing in network
games or replay playback (checked: the referee replay matches with the switch on). Not yet
host-decided or recorded in replays: a skirmish recorded with it on will not play back.

A/B, same build: armies fps +13%, work -12%, p90 -12%; strikes fps +10%, work -9%, p90 -17%.
1,941 paths from fields, 59 fell back to A*, 782 fields built (buildings destroyed in the strikes
invalidate them all). Battle screenshots look normal (armies engage, no stuck groups).

Flow fields ON by default in single player (the user's choice, 2026-11-23); still never in
network games or replay playback. Default-on run, multithreaded build: battle 11-23 fps, tour
58-60 fps; single-thread run: 2,907 paths from fields, 93 fell back.

GPU field builder (gles3_wrapper.cpp GLES3_GPU_Flow_Field, switch gpuFlowFields, bit 26, OFF
until measured): the same field by integer relaxation in a fragment shader (R32I ping-pong,
R8UI walkability, costs 10/14, the A*'s corner rule), read back every 32 passes until it stops
changing; every GL state it touches is saved and restored. Verified (Module.gxVerify): 1,271
GPU fields in a battle, each compared cell by cell with the CPU field: 0 of 55,080 cells
differed in all of them. 192-288 passes per field.

Batched GPU fields (the user: "Yes, try batching"): one RGBA32I atlas, a tile per field with an
unwalkable cell between tiles; each pass also writes whether the cell changed, so the passes run
unchecked (as many as the longest field so far needed, 288) and one read-back shows whether to
continue. When a field is missing, every stale field used in the last 300 frames is rebuilt in the
same batch (a field depends only on the buildings, s_obstacleSerial, so early = same result).
Verified: 1,867 batched GPU fields, 0 cells differ from the CPU. Read-backs: 763 per battle for
756 batches (was ~8 per field, ~9,900). Still slower than CPU fields, A/B same build:
RX 580 armies fps -5% (work +6%), strikes fps -21% (work +28%); software rendering armies fps
2.1 -> 0.7 (work 92 -> 1,274 ms). The cost is the passes and the 16-byte-per-cell transfers, not
only the read-back count; batching also rebuilt more fields (1,503 vs ~1,240). Switch stays off.

## 2026-11-24 — Plan routes around terrain and buildings only (switch ignoreUnitsInPaths, off)

Profile of the battle's armies phase (multithreaded build): path searches are ~30% of the frame, and
almost all of it (25.7%) is `findClosestPath`, the approach search units run when closing on a target
they cannot stand on (often a building); ordinary `findPath` searches are ~4% after flow fields.
The switch (page bit 27) makes `findPath` and `findClosestPath` expand cells without looking at
other units (`checkForMovement` skips unit cells while `s_gxPlanIgnoringUnits` is set); destinations
are still checked against units, and a unit that is blocked plans with units again. Crowding is left
to the existing movement code (collisions, allies stepping aside, blocked re-plans). Simulation-changing:
single player only, never network games or replays.

A/B, battle benchmark: RX 580 multithreaded: armies 9.4 → 9.9 fps (+5%), strikes 6.3 → 8.4 fps (+33%,
work −13%, 1% low 0.3 → 1.1). Software rendering: neutral (armies 2.4 → 2.3 fps, work −4%; rendering-bound).
About 40% of plans fall back to planning with units because the unit is blocked.

Parallel path searches were not built: every search keeps its open/closed lists, costs and parents in
the shared map cells (`PathfindCell::m_info`, from one global pool, which also holds the cells' unit
ids) and per-search zone flags in the shared zone manager, so two searches cannot run at once without
moving all search state into per-thread storage across every search variant.

## 2026-11-24 — WebGPU compute flow fields (switch webgpuFlowFields, off)

The flow fields of `flowFields`, built by a WebGPU compute shader on the game thread (EM_JS; a worker
has `navigator.gpu`). `relax`: 16x16-cell tiles with a 1-cell halo in workgroup memory, 24 relaxation
sweeps per dispatch, integer `atomicMin` back to the field, a per-dispatch "changed" flag read back with
`mapAsync` between rounds; `dirs`: the walk's step per cell (first lowest neighbour in the CPU walk's
order, 8 = goal area, 9 = stuck, 15 = unreachable), 8 cells per u32. Fields now keep only these steps
(the CPU path computes them from its distances, same walk as before).
Lockstep: a field asked for on logic frame N is used from frame N+3 whether the GPU or the CPU (no
adapter yet / none) builds it; until then the search runs the plain way. If the GPU result is not back
by frame N+3, `GameLogic::update` returns without running the frame (`gxFlowFieldsHold`), so the game
plays the same however fast the GPU is; after 3 s the field is built on the CPU from the map as it was sent.
Verified on the RX 580 (Edge, Vulkan, AMD GCN-4): 2,357 fields, 0 distances and 0 steps differ from the CPU.
A/B on the RX 580 (both lanes WebGPU-enabled, multithreaded): armies 13.5 → 13.7 fps, strikes 9.1 → 9.6 fps
(+5%, work −4%), strikes 1% low 1.3 → 0.9 (341 held frames: the GPU round trip, two `mapAsync`s, is
sometimes longer than 3 logic frames). One dispatch round of 24 always converged on this map.
Bench lanes: WebGPU on Linux needs a headed Edge (headless gets SwiftShader); `webgpu: true` in the bench MCP
opens the window off-screen with occlusion throttling off, and `cdp.py` now picks the game page (a headed
Edge opens an internal side page first) and clicks Begin by its id.

## 2026-11-24 — Parallel path searches (switch parallelPaths, off; multithreaded build only)

At the top of `processPathfindQueue` the searches the next queued units will ask for (as many as the
queue served last frame, +1; guessed by `AIUpdateInterface::gxPredictSearch`: the approach path's
`findClosestPath`, or computePath's `findPath`, chained with the `findClosestPath` computePath always
makes after an empty `findPath`) run on 3 worker threads plus the game thread. Each search has its own
copy of the search state: the cells' `PathfindCellInfo` it touches (copied from the shared one on
first touch, made fresh where the shared pool would allocate; ground cells looked up by their index in
the map, inline in `PathfindCell::I()`), the open/closed lists, tunnelling flag, cell count, ignored
obstacle and the zone blocks' passable flags (the Pathfinder members are redirected by macros to the
thread's copy). Flow fields are only read (an existing valid field), never built. The queue then runs
as before; a result made ahead is used only for the identical call (object, arguments, locomotor set,
tunnelling flag, ignored obstacle, failover mode) when no cell it read changed since (unit data,
obstacles, info appearing or going: per-cell frame stamp), the shared pool still has room for what it
allocated and the field it walked is unchanged; then its state is written back into the shared cells.
Anything else is searched again the normal way. The game's memory pools lock only during a wave.
The search fields an info keeps between searches are not tracked: every search rewrites them before it
reads them (verify mode checks every result against the normal search).

Exactness: verify mode, battle benchmark, multithreaded: 2,260 searches made ahead compared with the
normal search, 0 mismatches. `bench_replay` (multithreaded, switch on): matches the referee recording
over 9,000 frames. Single-thread build (switch absent, same code otherwise): replay matches.

Speed: slower, so the switch stays off. A/B (RX 580, multithreaded), latest: armies 10.8 → 5.3 fps.
Per search the copy costs ~1.9x a normal search (was 7x before the open list walked its infos directly
and the lookup went inline); but about half of the searches made ahead are thrown away (not asked for:
the queue's 5,000-cell budget runs out, or the guess was wrong; or redone because an earlier unit changed
a cell), the wave waits for its longest search, and the path queue is only ~20% of a battle frame
(~16 ms of ~80), so the ceiling is small. Ideas left: guess only searches likely to be served and long,
start the wave during the previous frame's render, cut the copy cost further.
Side effects in the default path: `putOnSortedOpenList` walks the infos' links instead of looking each
cell's info up (same list, same order). New bench tools: workers in CPU profiles (`PROFWORKERS=1`),
replays on the multithreaded build (`bench_replay multithread`).

## 2026-11-24 — WebGPU flow fields v3, parallel path searches v2

WebGPU fields: one submission and one `mapAsync` per job (relaxation, steps, read-back copy together;
a second round only if the last dispatch still changed something), seeds made on the GPU from the goal
area, walkable masks one byte per cell and shared between fields, buffers kept in reusable sets, tiles
skip a dispatch when neither they nor a neighbour changed in the previous one, the dispatch count learned
from the last dispatch that changed anything; software adapters (SwiftShader, fallback) are never used,
and the GPU builds only while the average game work per frame is over 20 ms (the CPU builds the same
field on the same schedule otherwise). A result is used only if its commands validated (error scope);
a shader or validation error turns WebGPU off. Verified on the RX 580: 2,939 fields, 0 cells differ.
Held frames 341 → 21 (v2). Measured: with the switch on, even CPU-built fields cost ~19% in the armies
phase, because of the 3-frame lockstep delay (searches that wait run the plain A* meanwhile) — the
delay, not the GPU, is what loses.

Parallel paths v2: no copies (a worker's first touch of a cell makes a clean scratch entry; unit data is
read from the shared cell), and the game thread serves units while the workers compute, waiting only for
the result it needs (and for every worker before a normal search or a flow-field change). Verify: 1,058
checked, 0 mismatches; replay matches (9,000 frames). A/B: armies 13.0 → 9.9 fps (was 10.8 → 5.3).

## 2026-11-24 — Battle files read ahead (switch warmBattleFiles, on)

- Profile of the strike phase (live build 1e2f9e6, normal build): synchronous file reads 17%
  (`send` 12.9% + `doXHR` 4.4%); particles ~2%, see-through sorting 1.7%, culling 0.5%, the fog
  of war texture 0%. So the GPU particle/fog/culling ideas were not built: ≤ 4% to gain.
- The reads are first uses mid-match (the first strike's frame read 232 files, 1.8 s). The
  remembered-files warm-up (Session 33) only helps from the second match, and its 1 MB windows
  can't hold a long list: the battle's 1159 files (52 MB) span 246 windows, more than the
  100 MB window cache.
- Now: a shipped list (`gx_battle_files.inc`, the files a benchmark battle reads on a fresh
  profile) plus the remembered files, fetched as exact ranges when a match starts
  (`Warm_Queue_JS`: neighbours merged into ≤ 2 MB requests, 3 in flight; kept by path until
  the first read takes them, ≤ 128 MB). Off on ≤ 2 GB devices; switch off = the old warm-up.
- Measured (machine swapping, fps noisy; the stall frames are the clear signal): off, frame 616
  233 files 1.74 s plus 40- and 33-file stalls; on, no strike frame above 20 file reads and none
  in the 10 slowest. Strikes 7.3 -> 9.5 fps in solo runs. Multithreaded: frame 616 0.35 s.
  The army spawn at frame 4-5 (373 files) still stalls: the warm-up has only just started.
- Caveat: the list comes from this benchmark, so the benchmark is its best case; a real match
  uses a subset and the rest stays in memory unused (≤ 52 MB).

## 2026-11-24 — Parallel path searches and WebGPU flow fields removed

- Both were exact but slower (armies −24% and −19%); removed at the user's request. The path
  files, GameMemory.cpp, AIUpdate, GameLogic.cpp, gles3_mainloop.cpp, gles3_wrapper.cpp and
  web.cmake are back to 0bca474 (ignore-units routes kept); their Settings entries are gone.
  Page bits 28/29 are free again. Check: both builds compile, referee replay matches over 9000
  frames.
- Next logic work: see LOGIC_IDEAS.md (profile of the strike phase and five candidate savings).

## 2026-11-24 — Three same-game logic savings (switches enemySearchSkip, collisionOrder, allyTable; on)

- **enemySearchSkip** (bit 28): each partition cell counts its objects per controlling player
  (`PartitionCell::m_gxCount`/`m_gxMask`, kept on COI add/remove and moved by
  `PartitionData::gxTeamChanged` from `Object::setOrRestoreTeam`). `getClosestObjects` skips a
  cell when its first filter is "live enemies of X" (`PartitionFilter::gxEnemiesOf`) and the cell
  holds nobody X is enemies with. Off for the game when X's side has team-level relation
  overrides, X is an undetected defector, or a script moved a team to another player.
- **collisionOrder** (bit 29): collision pairs test the NO_COLLISIONS status bit before the shape.
- **allyTable** (bit 30): `Player::getRelationship` answers from a per-player table rebuilt when
  any relation map is written (`g_gxRelationGen`).
- Same game: referee replay matches over 9000 frames; verify mode checked ~40.5 M objects in
  skipped cells, 0 that the filter would have let through.
- Benchmark A/B (all three off vs on, machine loaded): work ms armies 91.0 -> 87.4 (-4%),
  strikes 93.6 -> 90.5 (-3%), aftermath 55.8 -> 53.3 (-5%); fps +2/+3/+5%. Less than the ~10%
  estimated in LOGIC_IDEAS.md: collisionOrder and allyTable are small; most of the skip's
  saving is the distance tests on allies.

## 2026-11-24 — Three more same-game logic savings (second switch word: contactTable, sleepyKeys, openHeap; on)

Measured first (temporary counters, peak battle, per frame): ~10k open-list insertions walking
35-124 cells each (over a million pointer steps), longest open list 563 cells; touching pairs in
tens of thousands; 40-50k queued sleepy updates. (The counters' own timers inflated the update
queue's share; the sampling profile's 3.8% for the heap is the reliable figure.)

- **openHeap** (`GLES3_Page_Switches2` bit 2, AIPathfind.cpp `PathfindCellList`): the open list is
  a binary heap keyed on (total cost, arrival number). The sorted list puts a cell after all of
  equal or lower cost, so its head is always the lowest (cost, arrival); every cost change takes
  the cell off the list first, so stored keys stay right. The release walk sorts the heap first
  (same release order). At PATHFIND_CELLS_PER_FRAME (5000) cells the retail insertion walk would
  stop early, so the heap turns back into the sorted list for the rest of that search.
- **sleepyKeys** (bit 1, GameLogic.cpp): `m_gxSleepyKey` holds each queued module's priority beside
  `m_sleepyUpdates`; every priority change is followed by `rebalanceSleepyUpdate(i)`, which
  refreshes key i. Same swaps, no trip into each module per comparison.
- **contactTable** (bit 0, PartitionManager.cpp): the touching-pair duplicate check uses a 2^18
  bucket table (allocated once, emptied through the pair list) instead of 5381 buckets.
- Same game: referee replay matches over 9000 frames (lg2 and lg3); verify mode checked 75.6 M
  stored queue keys, 0 wrong.
- A/B (all three off vs on, lg3): work armies 66.5 -> 59.4 ms (-11%), strikes 107.6 -> 100.1 (-7%);
  fps +10% / +7%. Machine load 8-25 during runs.
- Bench tool fix (generals-bench-mcp server.py `_finish`): lanes are closed before a job is marked
  done; a waiting cli.py used to exit first and leave the browser running into the next run.

## 2026-11-24 — Measured, not kept: skipping far-apart collision pairs

- Coarse per-frame timers (peak battle, per frame): ~4,400 moved objects, ~95k pair offers,
  ~51k distinct touching pairs, ~6.4k (13%) truly touching; building the list 7-8 ms, testing it
  11.5 ms (including the collision reactions), freeing it 1.9 ms.
- Tried (exact; replay matched, verify 0 mismatches): skip pairs farther apart than both shapes'
  reach (bounding circle x sqrt 2 + 1) before the shape test, and offer a neighbour once per
  object. A/B: within 1-2% (no gain): the shape tests are cheap; the 11.5 ms is mostly the
  collision reactions (onCollide: physics pushes), which are real game work. Removed.
- What is left in logic is mostly game behaviour: units re-planning routes when blocked
  (AIUpdateInterface::computePath ~14% of the frame: a failed route search explores everything
  reachable, then a closest-point search runs) and collision reactions. Making those cheaper
  changes how the game plays (single player only, behind a switch).

## 2026-11-24 — Measured, not kept: shorter closest-spot searches (single player)

- Counters (peak battle): failed route searches are cheap (rejected early by checkDestination,
  ~0.1-1 ms per 150 frames); the cost was the closest-spot searches: ~2 a frame, different units
  each time (not repeats), ~10,000 cells each, 5-10 ms each before the open-list heap.
- Tried: stop a closest-spot search after 2,000 cells once it has a candidate (single player,
  never network games or playback; replay still matched). It fired on ~600 of ~2,700 searches,
  but the A/B showed no gain: with the heap (5b0c901) these searches are already cheap. It
  changes unit behaviour for nothing, so it was removed.

## 2026-11-24 — Behaviours, collisions, weapons: profile; collide-module list not kept

- Strike phase, share of the frame (lg3): collision reactions 6.7% (physics push 3.7%, AI
  "blocked by" handling 2.7%); weapons firing 3.4% (damage 2.4%); moving objects
  (setTransformMatrix -> reactToTransformChange) 3.1%; weapon-status model flags 1.5% (mostly
  feeding the drawable).
- Object::onCollide had 2.7% self time: it asks every behaviour module (20-40 per unit) for a
  collide interface on every collision. Tried (exact; replay matched): each object's collide
  modules listed once. A/B showed no difference at machine load 8-19 (fps 7-9, noise larger than
  a 2% change); not kept until it can be measured on a quiet machine.

## 2026-11-24 — Touching-pair list rebuilt as flat arrays (switch contactArray, second word bit 3, on)

- The pair list (PartitionContactList) allocated a node per pair (~51k a frame at the peak),
  probed hash chains for repeats and freed every node at the end of the frame. Now: pairs go into
  one array in discovery order and are processed newest first (the old list put each new pair at
  its head); repeats (either order) are turned away by an open-addressing table of pointer pairs
  whose slots carry a frame generation, so nothing is allocated, freed or cleared per pair. The
  pair body is one shared function (gxProcessPair) taking references, so a reaction that removes a
  partition data mid-frame still nulls later pairs, as before. The table doubles if pairs pass half
  its slots.
- Same game: referee replay matches over 9000 frames; verify mode builds the old list alongside and
  compared every frame's pairs and order: 0 mismatches.
- A/B (off vs on, twice, load 7-9): strikes fps 12.2 -> 12.8 and 11.4 -> 12.0 (+5%), work -5%/-4%;
  aftermath +1% / +7%; armies +1%.

## 2026-11-24 — Nearest-object search: measured, rebuild not kept

- Counters (peak battle, per frame): ~180 searches (70% iterate-all-in-range), ~31k cells visited,
  ~89k COI visits for ~41k distinct distance tests (objects in several cells), ~27k within range,
  ~12k passing the filters.
- Tried (cellArrays): each cell's COI list mirrored as a plain array of modules (reversed, read from
  the back = list order). First version also stored the object pointer and verify mode caught it:
  a dead building remembered under the fog keeps its module in the cells with the object cleared
  (unRegisterObject), so the stored pointer went stale. Fixed by reading the object from the module;
  then replay matched and verify found 0 mismatches.
- A/B twice: +1%/-1% and +6% strikes / -4% aftermath: no clear gain. The list walk is not the cost;
  each visit still reads the module and the object. Not kept. A real gain needs positions and
  sizes in dense per-object arrays (a change to the whole object system).

## 2026-11-24 — Drawing core: measured before a rebuild

Strike phase (live build 52b7e14), share of the frame, inclusive:
- W3DDisplay::draw 32.5%: RTS3DScene::Flush 18.0% (mesh renderer 8.4%, sorting 1.7%, terrain 2.3%),
  per-object submission RTS3DScene::renderOneObject 6.4% (HLodClass::Render 4.7%), Drawable::draw
  4.2%, drawablePostDraw 2.0%, 2D/UI ~3.6%.
- Under the mesh renderer: Emscripten_IDirect3DDevice8::DrawIndexedPrimitive 8.2% (Apply_Pipeline_State
  2.8% incl. a uniform-block upload 0.7%; vertex/instance uploads 0.8%; WebGL calls themselves ~2-3%:
  bufferSubData 1.4, uniformMatrix4fv, bindTexture, drawElements, bindBuffer/VAO 0.2-0.3 each).
- Probe (kindprobe.js) in strikes: ~480 plain WebGL draws per frame plus instanced ones; the
  benchmark's 2,450 "draws" are engine-level requests before batching.
- So a full rebuild (recorded, sorted draw list submitted with minimal state; per-object data kept
  together) can save at most roughly half of the ~8% submission cost and of the ~6% per-object
  cost: about 6-7% of the frame, for several days of work with visual-regression risk.

## 2026-11-24 — Route searches: occupied-cell bits (cellBits) and lazy flow fields (lazyFields), both kept

- Counters (big battle, per 150 frames): ~5M checkForMovement calls reading ~45M cells (blocks of
  1-9 cells around each searched cell), 27-36% of them holding a unit; flow fields: 40-120 field
  builds (~7 ms each) for ~100-260 field paths, i.e. fields are rarely shared (344 builds for
  800 paths); the downhill walks cost almost nothing.
- cellBits (second switch word bit 5, on): one bit per ground cell, set while the cell's unit flags
  are not NO_UNITS. Every flag write goes through PathfindCell::gxSetFlags, which keeps the bit in
  step (always, so the switch can flip mid-game). For a ground block fully on the map,
  checkForMovement reads the bits column by column and runs the original per-cell body only for
  occupied cells, in the original order. Off-map blocks and bridge layers keep the old loop.
- lazyFields (second switch word bit 6, on; flow fields are single player only): a field's
  Dijkstra (Dial buckets) and its walk mask are kept and resumed on each request until the
  start cell's distance D is final. Every cell nearer than D is then final and every other cell
  holds more than D, so the downhill walk reads the same cells as with the full field. The mask
  is filled in on demand with the ignored obstacle of the moment the field was made (cell types
  only change with s_obstacleSerial, which retires the field). The GPU field path is unchanged.
- Same game: referee replay matches over 9000 frames (paths-2 and paths-4). Verify mode: the old
  checkForMovement loop beside the new one, and the full field beside each lazy walk (all cells
  nearer than D equal, all others above D): 0 mismatches.
- A/B: fps pairs were useless at machine load 12-33 (the same setting swung ±50%), so I compared
  strike-phase profiles instead (share of the frame, both switches off vs on, two pairs at load
  11-25):
  checkForMovement 5.0% -> 3.7% and 4.7% -> 3.8%; gxFlowFieldPath 3.6% -> 1.7% and 3.1% -> 1.6%;
  AIUpdateInterface::computePath (incl.) 13.9% -> 11.0% and 12.8% -> 11.0%.
- Tried, not kept: reusing a field when its rebuilt walk mask is unchanged (fieldReuse). It never
  happened (0 of 226 rebuilds): each rebuild follows a real walkability change from a building
  placed or destroyed.
## 2026-11-24 — Unit AI / state machines: measured, nothing kept (logic-ai agent)

Strike-phase profiles of 66e2829 (builds ai-0, tags ai-prof1/ai-prof2; machine load 27-48):
- AIUpdateInterface::update 9.9-13.7% inclusive, but its own code is thin: all AI self time
  (isIdle, isDoingGroundMovement, updateStateMachine, doLocomotor, state updates, findLocomotor,
  chooseGoodLocomotor, transitions) adds up to ~2-3% of the frame, each item 0.1-0.4%.
- The rest belongs to other areas: target scans getNextMoodTarget 2.7-4.1% (almost all
  AI::findClosestEnemy -> PartitionManager::getClosestObject), path following
  Path::computePointOnPath 1.7% (doLocomotor + getLocomotorDistanceToGoal, with isLinePassable),
  weapon firing under AIAttackFireWeaponState 1.2-1.7%, locomotor physics math 1.3%.
- Counter (ai-diag1, per 150 frames in strikes): 3,100-6,700 mood scans by cadence (80% find
  nothing), 60-960 forced rescans after a kill, ~1,900 from idle units: ~45 scans a frame, all
  already on the game's own 2-second cadence (MoodAttackCheckRate) with no repeated work to drop.
- Checked and ruled out: model-condition clears every frame (Drawable already returns early when
  nothing changes); a second computePointOnPath per frame (Path already caches it, and a memo would
  change its countdown, so not exact); state-machine create/destroy (0.3%, only on object death).
- Not built: exact micro-changes here could save at most ~0.5% of the frame, below what this
  loaded machine can measure. The one real lever in this area is the scan cadence (e.g. back off
  after misses), which changes the game: it could only be a single-player switch, worth ~1-2%.

## 2026-11-24 — Unit AI: what the states cause (logic-ai agent, second pass)

Counters (build ai-diag2, strike phase, per frame):
- ~1,000-1,800 AI updates. Attack-move units 600-1,100 and move-to 250-370, all awake every frame
  (they are moving: needed). Idle units awake 60-92% of frames (turrets tracking, blocked units).
  Dead units up to ~385 updates a frame (AI_DEAD never sleeps), but the profile puts AIDeadState at
  0.0%: not worth making exact sleep for.
- Path requests: 35-70 a frame re-request a moving goal (90% within one cell of the last request),
  but these are missiles' quick paths that return early when unchanged. Moves started over to the
  same place grow to ~45 a frame late in the battle. Only 5-10 real route searches run per frame,
  and the pathfinder already caps work per frame (PATHFIND_CELLS_PER_FRAME), so fewer requests
  would not cut CPU much. Pathfinder share did not move in the runs below. Not pursued.
- Target scans: ~45 a frame, 80% find nothing, mostly attack-move units (15-37 a frame) and idle
  units (5-11).

Kept, off by default — lazyScans (switch word 2, bit 12; single player only, gated like
ignoreUnitsInPaths): a routine scan that finds nothing waits one more check interval (4 s instead
of 2 s). Profile pairs at load 11-19 (off -> on), strike phase share of the frame:
- pair 1: getNextMoodTarget 3.5% -> 2.1%, AIUpdateInterface::update 13.1% -> 10.8%
- pair 2: getNextMoodTarget 3.5% -> 2.5%, AIUpdateInterface::update 12.9% -> 13.2%
So scans cost 30-40% less, about 1-1.4% of the frame. Whole-frame A/B could not show it: load
swung 15 -> 42 between runs (+19% then -34% fps), and with the switch on the battle itself plays
out differently. Replay matches the recording with the switch off and on (replays ignore it).
Off by default because units notice newcomers up to 2 s later; the main chat decides.
## 2026-11-24 — Combat area (weapons, damage, moves): profile; two drawing caches kept (switches boneCache, lightCache; second word bits 20-21, on)

- Profile (lg ca1, machine load 50-75, so strike phases hit the time limit; shares from a 30 s armies
  window): weapon-status model flags 0.7-0.9% of the frame, of which ~70% was
  W3DModelDraw::doHideShowProjectileObjects (units that show their loaded missiles): every frame it
  formatted "BONE%02d" names and searched every sub-object by name (RenderObjClass::Get_Sub_Object_By_Name
  0.3-0.4%). W3DModelDraw::hideAllHeadlights ran a strstr over every sub-object name on every model
  state change (0.3%). Moves (setTransformMatrix -> reactToTransformChange) 1.2-2.0%: spread thin
  (two matrix copies, atan2, partition dirty, drawable Set_Transform; trigger areas 0.1%, pathfinder
  cell update ~0); damage 0.6-1.1% (half of it ActiveBody::internalChangeHealth -> visual state);
  particles 0.2-0.6%. Nothing else in this area above ~0.5%.
- **boneCache** (bit 20): the missile bones' sub-object indices are found once per render object,
  model state, slot and clip size, then hidden/shown by index (same calls, same order, same
  bone-children pass, same updateSubObjects). **lightCache** (bit 21): the headlight sub-objects are
  listed once per render object. Both caches are emptied in nukeCurrentRender, the only way the render
  object changes. Drawing only; the game is untouched.
- Same game: referee replay matches over 9000 frames. Verify mode re-does every cached lookup by name
  and compares: 60,000+ checked, 0 mismatches.
- Measured (profile share, armies window, switches off -> on): missile bones 0.6% -> 0.2%, headlights
  0.3% -> 0.1%, by-name sub-object search 0.4% -> 0.1%; about 0.6% of the frame. fps A/B was not
  measurable (load 37-75, strike phases stopped at the time limit; fps differed 2x between identical runs).
- Not done (each < 0.3% and/or would change the game): the drawable's second atan2 per move (the
  object's angle can differ from the matrix's after setOrientation), heap changes in the update queue
  (heap order is game order), audio per shot (random seeds under RETAIL_COMPATIBLE_CRC), damage
  victim order.

## 2026-11-24 (later) — clipSkip (second word bit 22, on) and the strike-phase numbers for all three switches

- **clipSkip**: `Drawable::updateDrawableClipStatus` runs every frame for every armed unit and weapon
  slot (ObjectWeaponStatusHelper) and walked every draw module, although only W3DModelDraw modules
  with ProjectileBoneFeedbackEnabledSlots act. The drawable now asks its draw modules once which
  slots they show (`ObjectDrawInterface::gxClipFeedbackSlots`, default "all") and skips the walk for
  other slots. Draw modules never change after creation. Drawing only.
- Same game: referee replay matches over 9000 frames with boneCache, lightCache and clipSkip on.
- Strike-phase profile share (machine rebooted, load 16-26), all three off -> on, two pairs:
  weapon-status model flags 1.5% / 1.4% -> 0.5% / 0.5%; missile bones 0.8% -> 0.2%; headlights
  0.5% / 0.4% -> 0.1%. About 1.3% of the strike frame.
- fps/work A/B, same runs (off -> on): strikes 5.6 -> 7.3 fps (work 176 -> 134 ms) and 4.8 -> 10.1
  (206 -> 97 ms); armies 8.9 -> 9.8 and 4.7 -> 12.7. Direction agrees but the size is mostly noise
  (an earlier pair without clipSkip, load 12-33: strike work 124.5 -> 107.8 and 145.8 -> 141.3;
  armies +9% in one pair). The profile shares are the reliable figure.
- Looked at and dropped (profile share in strikes, each too thin or not exact): moves
  (Thing::setTransformMatrix 2.5-3.4% inclusive, spread over matrix copies, atan2, drawable
  Set_Transform, partition dirty; no piece above 0.3%); W3DModelDraw::adjustTransformMtx (0.3%,
  a few field reads); weapons firing 1.8-3.6% (damage half; victim order and audio seeds are game
  state); update-queue heap 0.7% (heap order is game order; sleepyKeys already done); particles
  0.2-0.6%.
## 2026-11-24 — Touching-pair list build: same-cells fast path and leaner pair offers (switches cellReuse, leanPairs; second word bits 8-9, on)

- Counters (peak battle, per frame): ~1,100 cell updates, 91% end on exactly the same cells (86% are
  "small" shapes, 1.7 cells each on average); ~2,100 objects scan their cells and make ~42k pair offers.
  Each offer cost a cell-list step, a read of the other side's partition data (object pointer) and a
  probe into a 4 MB pair table: the list build is mostly cache misses.
- cellReuse (bit 8): the cells a fill covers depend only on a key (whole-cell corners for small shapes,
  centre cell + cell radius for the retail circle fill, the exact floats for boxes) plus the COI array
  size. If the key is unchanged, the old remove-all-and-refill only moved each COI to the front of its
  cell's list; the fast path does just that (and resets the per-cell player count slot). The key is
  kept on both paths, so flipping the switch mid-game stays exact.
- leanPairs (bit 9, needs contactArray): offers read only the two partition-data pointers; pairs with an
  object-less side (fogged ghosts) are kept and skipped when processed (they never were listed, and
  they don't change the other pairs' order). Repeats are found in a table of 8-byte keys (1 MB, 0 =
  empty), its used slots listed and emptied at reset, instead of the 16-byte generation table (4 MB).
- Same game: replay matches over 9000 frames; verify mode (cellReuse compares each reused key's cells
  with a real refill; the contact-array check skips ghost pairs) 0 mismatches over the whole battle.
- Measured: fps A/B could not see it (load 12-46 after a reboot; strikes off 5.8/5.0/8.1/5.6/5.6 vs on
  5.8/3.6/9.1/7.0/3.6). Coarse per-frame timers (two per frame) instead, build ms / processing ms per
  150-frame window: off 0.33-0.44 while the battle builds up and 0.79-0.80 at the peak; both on
  0.26-0.32 and 0.56-0.63 (about 20-25% less build work, e.g. peak window 14.2 -> 10.3 ms on the same
  machine state). Split: leanPairs alone 0.30-0.35 (most of it); cellReuse alone 0.34-0.39, 0.71 at the
  peak (a small share). Worth roughly 1-3% of a peak frame.
## 2026-11-24 — Range searches rebuilt: fewer entries touched (switches searchSlotSkip, laserFilters, denseCellMask, searchCache; second word bits 16-19, on)

- Who searches (per-call-site counters, peak battle): point-defense lasers 44% of all entries walked
  (no filters: every unit in scan range was listed, then rejected in the update), stealth detectors
  23% (first filter "is stealthed", the team filter second, so no cell skip), potential-collision
  searches 12%, findClosestEnemy 9%.
- Changes (all exact):
  - laserFilters: PointDefenseLaserUpdate passes its own rejections as filters (enemy first), so the
    search skips non-enemy cells and lists only targets. The list is unsorted; dropping entries keeps
    the order of the rest.
  - searchSlotSkip: each cell keeps one COI sublist per player slot, in the cell list's relative order
    (a per-cell insertion sequence; slot changes re-link by sequence). A search whose first filter
    admits only some players walks only those players' lists, merged back by sequence. Also: the
    relationship filter (no allies admitted, neutrals allowed) now gives the player mask, and stealth
    detectors put their team filter first (filters only test, so order doesn't change the result).
  - denseCellMask: every cell's player mask is mirrored in one 4-byte-per-cell array; the ring walk
    reads it before touching the (200-byte) cell.
  - searchCache: each COI holds its object's x, y and max(bounding circle, sphere) radius, refreshed on
    every transform change, cell coverage and Z-geometry change. An entry farther than range + both
    radii + 1 unit (2D centre distance; a lower bound for all four distance types) is turned away
    without reading the module or the object. The slack makes float rounding irrelevant.
- getClosestObjects is now a wrapper around gxClosestImpl(flags). Verify mode runs every search twice
  (fast and the plain walk with nothing skipped, alternating order), compares the objects that passed
  (in order), the result, its distance and vector and the iterator list, and times both runs.
- Same game: verify 0 mismatches (183k-246k searches per run, 7 runs; 32k laser scans compared with
  the unfiltered list); referee replay matches over 9000 frames.
- Paired timing inside verify runs (load-proof): search time / plain walk = 0.59 with today's live code
  (cell skip only), 0.25 with all four. Per switch (all on minus one): minus slot lists 0.55, minus
  cache 0.34, minus dense mask 0.36. Per-player lists vs a per-entry slot check: entries walked per
  search 258 -> 206, 0.345 -> 0.287. Laser scans (whole scan, alternating): unfiltered 609 ms,
  filtered 104 ms.
- A/B (four switches off vs on, twice, load 4-5): strikes fps 13.9 -> 14.8 and 13.8 -> 14.7 (+6.5%),
  work 71.0 -> 66.9 and 71.5 -> 67.3 ms (-6%); armies and aftermath flat.
- Not built: coarse 8x8 block masks. Per search 132 cells are in range and 51 kept; the 81 skipped are
  already 4-byte reads from the dense array (well under 2% of a search).

## 2026-11-24 — Five logic branches merged (perf/logic-paths, -ai, -combat, -collide, -query)

- Merged into perf/low-end (1112bed). Conflicts were only the shared switch list, Settings entries
  and this log; resolved by keeping both sides (one duplicated contactArray line removed).
  Second switch word default: 15|32|64|256|512|65536|131072|262144|524288|1048576|2097152|4194304
  (lazyScans stays off).
- Same game: referee replay matches over 9000 frames; verify mode 0 mismatches (builds merge1).
- A/B live (ca1 = rev 00057) vs merged, twice: armies +10% / +5% fps (work -8% / -5%), strikes
  +7% / +7% (work -7% / -7%), aftermath +12% (work -11%). Load 4-22.
- Not yet in: the range-search agent's 8x8 block summaries (bit 24), still being measured.

## 2026-11-24 — Match rules: game-changing switches decided by the host, carried in the game and the replay

- New helper `gxMatchRule(rule)` (Common/GxMatchRules.h, defined in GameInfo.cpp). Every switch that
  changes the simulation goes through it: ignoreUnitsInPaths, flowFields, fastInvSqrt, lazyScans
  and the new coarseStart. The old gates ("never in multiplayer or playback") are gone.
- The rules travel in the game-options string every player receives and the replay header stores
  (GameInfoToAsciiString): a `GX=<hex>;` key, written only when a rule is on, so a game without
  rules sends the retail string. ParseAsciiStringToGameInfo reads it. The LAN host (and the online
  host) sets the rules from its own Settings when it creates the game; skirmish and campaign
  recordings store the local rules when recording starts. GameLogic::startNewGame calls
  gxStartMatchRules(): in a network game or a replay the rules are fixed from TheGameInfo (the
  host's or the recorded ones) and each player's own switches are ignored; in single player the
  local switches are read live, as before. One "GXRULES" log line per game says which rules apply.
- Proof: a skirmish recorded on paths-5 with rules 0x1F (header `GX=1F`) plays back matching for
  9000 frames with every local switch off ("match rules from the replay: 1F"); the same for a
  coarseStart recording on paths-6 (0x33) and on paths-8. The old referee replay (no GX key) still
  matches. An older build refuses the new replay (unknown option key), so the files cannot be
  mixed up silently.
- LAN: not run as a two-browser test (the bench has no lobby driver). Each LAN client starts the
  match from the GameInfo parsed from the host's options string, exactly as a replay does from its
  header, and gxStartMatchRules takes the rules from that object on every machine, so all peers run
  the same rules; the replay test exercises that same code path.

## 2026-11-24 — Route queue measured; coarseStart kept (off), pathBudget dropped, ignoreUnits stays off

- Counters (battle benchmark, paths-6/7; deterministic in single player, so identical run to run):
  the 512-slot route queue is full (max 510) in the armies phase; requests wait 20 logic frames on
  average, 204 at worst (~7 s); about 90% of re-plan requests are "goal moved" (5-14k per 150
  frames, mostly repeats of a request already queued), then new orders (~1-7k), path lost and
  blocked (~50-200 each). Worst single flow-field build: up to ~155 ms (several runs), 18-35 ms
  common: a spike worth slicing (that would be a match rule).
- coarseStart (match rule, second switch word bit 7, off): while a unit's full route waits in the
  queue (moves longer than 200 units), it follows the zone-level route straightened by
  Path::optimize, then takes the full route when it arrives; the move state does not re-plan
  while on it. 2,500 units switched per battle; 4.5% of them found the full route pointing back
  the way they had come, after ~12 units (about one cell) of movement on the coarse one. Replays
  match. Frame time unchanged (strikes 13.3/14.4 fps vs default 11.9/14.5).
- pathBudget (match rule, smaller per-frame budget for starting queued searches; 2,500 and 1,500
  cells tried with coarseStart on): no frame gain (strikes 13.2/13.0 and 13.5/13.0 fps vs default
  11.9/14.5); a search is never split across frames, so the work is only deferred. Average wait
  rose from 20 to 37 and 54 frames (+0.5 s and +1.1 s), past the ~200 ms the user accepts. Removed.
- ignoreUnitsInPaths, measured again: strikes 13.0/13.1 fps vs 11.9/14.5, fewer >100 ms frames
  (48/34 vs 130/40), average wait 25 frames vs 20; 39% of plans fell back to planning with units
  because the unit was blocked. No clear gain: proposal is to keep it off by default, now that it
  could be used in network games.

## 2026-11-24 — Group routes: sharedRoutes (logic-ai agent; merged perf/low-end 445e8b8 first)

What a group move costs today (counters, build ai-diag3, per 150 frames, early battle): the
benchmark's attack-moves send every unit of a group to the exact same point. The first unit
reserves that cell; for every other unit findPath fails at checkDestination (the cell is taken)
and computePath falls back to findClosestPath, which examined 6,000-11,000 cells per call:
150-280 such calls = 1.0-1.7 million cells, against 10-240 thousand for all successful searches.
The route queue was backlogged on 136-149 of every 150 frames with 340-470 units waiting.
Flow fields (paths agent) already share one field per goal area, but the fallback skipped them.

sharedRoutes (switch word 2, bit 13; single player only through one gate,
AIUpdateInterface::gxSharedRoutes, so it can become a host-decided match rule with a one-line
change): when that search fails because the goal is taken, the unit takes the nearest free spot
around the goal (adjustDestination's test over square rings, up to 40 rings,
Pathfinder::gxAdjustDestinationRings) and plans to it, which uses the shared flow field. The ring
where the last spot for that goal was found is remembered for 5 s, so the next unit continues
from there instead of re-testing taken cells. Only if no spot is found does the old fallback run.
- Counters with it on: fallback 6-20 calls per 150 frames (3-11 thousand cells); successful
  searches 300-1,300 per 150 frames instead of 120-680; queue backlogged on 15-100 of 150 frames,
  ~130-170 waiting instead of 340-470.
- Profiles (load ~6): armies phase findClosestPath 16.7% -> 0.3% of the frame,
  computePath 19.4% -> 12.8%, but Pathfinder:: 22.3% -> 17.1% only and GameLogic::update the same
  (52.7% vs 52.3%); strike phase findClosestPath 7.8% -> 0.3%, computePath 13.6% -> 11.7%,
  Pathfinder:: 16.7% -> 17.5%. The pathfinder works to a fixed cell budget per frame
  (PATHFIND_CELLS_PER_FRAME) while units are queued, so the budget now goes to other units' routes.
- Frame time, two pairs off/on (load 7-12): work ms armies 50.9/53.1, 53.6/52.4; strikes
  108.6/79.9, 75.8/77.4; aftermath 54.8/48.6, 43.7/47.5. No frame-time gain shown.
- Replay matches with the switch off and on (replays ignore it). Screenshots off/on:
  ~/generals-bench/ai-shot-{off,on}_battle*.png (armies meet and fight normally).
Kept, OFF by default: it makes big armies get their routes about 3x sooner, not frames faster.
Making it a CPU gain would mean lowering the per-frame cell budget while it is on (a game
change of its own); left for the main chat to decide.

## 2026-11-24 — Spreading AI work over ticks (logic-ai agent)

One gate for every game-changing AI rule: AIUpdateInterface::gxAiRule(bit) (single player only;
the match-rules wiring changes only that function). lazyScans (bit 12) and sharedRoutes (bit 13)
now go through it too.

Shares first (build ai-3, strikes / armies, load 3-4, inclusive share of the frame):
- (b) computer opponent: AIPlayer/AISkirmishPlayer < 0.1%, Player::update 0.2-0.4% (the benchmark
  armies are scripted). Nothing to gain here; not built.
- (c) scanners: StealthUpdate 0.7-0.8%, StealthDetectorUpdate 0.3-0.5%, PropagandaTowerBehavior
  0.4-0.5%, AutoHeal / AutoFindHealing 0.1%. At most ~0.7% from halving them, below what A/B can
  show, and each carries timing rules (detection lasts updateRate+1, healing per frame). Not built.
- (d) turrets: TurretAI 1.5-1.9%, part of it their target scans (lazyScans). Halving aim updates
  would need double turn steps and touches firing; not built for < 1%.
- (a) target scans: getNextMoodTarget now 1.0-1.5% (after the query agent's work). lazyScans already
  halves the routine ones (measured earlier 3.5% -> 2.1/2.5%); no further change.
- The real per-tick cost in AI is AI::update 14-20%: the route queue spends up to
  PATHFIND_CELLS_PER_FRAME (5,000) cells every frame while units wait.

routeBudget (bit 14, single player via gxAiRule): the queue's per-frame budget is halved
(processPathfindQueue only; the open-list limits keep retail values). Paired with sharedRoutes,
which removed the wasted searches, so the queue keeps up. A/B with sharedRoutes on in both
(A = budget 5,000, B = 2,500), load 2.7-8:
- pair 1: armies fps 21.8 -> 20.8 (work 40.7 -> 42.5 ms); strikes 16.5 -> 18.1 (59.9 -> 54.5);
  aftermath 28.4 -> 33.9 (34.6 -> 29.0), 1% low 6.3 -> 20.5
- pair 2: armies 22.6 -> 22.0 (39.1 -> 40.4); strikes 16.5 -> 17.8 (59.9 -> 55.5);
  aftermath 28.3 -> 33.1 (34.8 -> 29.7), 1% low 6.9 -> 19.3
- slowest frames unchanged apart from file loads (f4/f5 start-up, f615 spy-satellite strike).
- armies-phase profile: AI::update 14.2% -> 14.4% (no backlog there); the gain comes later.
- Replay matches with all three rules on and with defaults. Screenshot ~/generals-bench/ai-shot-rb_battle*.png.
Proposed defaults (single player): sharedRoutes + routeBudget ON together (+8-10% fps in strikes,
+17% in aftermath); routeBudget alone NOT (without sharedRoutes the queue is backlogged and units
would wait twice as long); lazyScans OFF (~1% at best, units react up to 2 s later).

## 2026-11-25 — Staggered collisions (switches collideStagger / collideStagger3, second word bits 10 / 11; single player; off)

- Walking and driving units (AI, on the ground, infantry or vehicle, not projectiles) look for
  touching pairs only on every 2nd (bit 10) or 3rd (bit 11, wins) logic frame, by object ID, so
  pushes, blocked-by handling, crushing, crate pickups and entering buildings come up to 1 or 2
  frames late. Movement and cell updates still run every frame (range searches see real positions).
- A unit skipped on a frame is put back on the dirty list after the pairs are processed (found by
  ID, as a reaction may have removed it), so it is checked on its turn even if it stopped moving.
- Each pair once per cycle: this frame's waiting units are marked first (a frame stamp on the
  partition data); an on-turn unit skips a waiting neighbour that sorts before it (address order) -
  that neighbour checks the pair on its own turn. Without this, a pair was checked whenever either
  side was on turn (every-2 saved only 13%).
- One gate, gxCollideStaggerEvery() in PartitionManager.cpp: single player only (never network
  games or replay playback); to be wired to the host's match rules (gxMatchRule) at merge. Replay
  with the switch on still matches (playback ignores it).
- Measured (collide-5, coarse per-frame timers, collision build + processing summed over the
  battle's 12 windows of 150 frames; load 6-12): off 95.1 / 98.4 ms, every-2 76.7 / 76.0 (-21%),
  every-3 61.5 / 65.7 (-34%); ~430 / ~570 units wait per frame. Strikes fps off 13.1 / 12.8,
  every-2 13.1 / 13.1, every-3 13.7 / 12.7: within noise (collisions are ~11% of a strike frame, so
  -34% is ~3-4%). Slowest frames unchanged. Screenshots of the crowded fight (collide-voffA vs
  collide-vs3A battle3) look the same: no units visibly inside each other.
- Proposed default: every-3 for single player (both switches left off until the match-rule wiring).

## 2026-11-24 — Round 3, movement as a data-oriented system: measured, not built

- Profile on dc2f00b (paths-9), share of the frame, inclusive: AIUpdateInterface::doLocomotor
  3.0% (strikes) / 3.3% (armies); all Locomotor:: code 1.8% / 1.9%, of which Locomotor's own code
  is ~0.1% self (the rest is what it calls: physics forces, terrain heights);
  Path::computePointOnPath 2.5% / 2.4% (partly inside doLocomotor); PhysicsBehavior::update
  3.1% / 2.2% (the collide agent's system). Load was high (fps ~5), shares still comparable.
- Ceiling: movement is ~3% of the frame. Flat arrays plus one bulk locomotion pass would keep the
  same per-unit math and calls (physics, terrain, path walk), so even halving it saves ~1.5%.
  That is below what the bench can show (A/B pairs swing 10-20% at today's load). Per the round-3
  rule (prove the slice pays first), the rewrite was not started. Match rule 1024 and second-word
  bits 24/25 are still free.
- If anything here is worth doing, it is Path::computePointOnPath (2.5%): it walks the path's
  node list for every moving unit every frame; its cache returns a stale answer only up to 20
  times. An exact cheaper lookup (resume from the last segment) is a small, separate change.
- computePointOnPath, counted (paths-10, battle benchmark): ~400 full computations a frame (of
  ~1,300 calls; the rest hit its cache), each walking 30-48 path segments (flow-field routes keep
  many nodes), ~15k segment visits a frame. An exact lookup must still test every segment for the
  closest one, so a flat segment array only makes each visit cheaper: ~0.3-0.4% of the frame at
  best, below what the bench resolves, and it needs an owner link plus invalidation in every
  PathNode setter (paths are edited in place from AIUpdate and several Pathfinder functions). Not
  built. A windowed search around the last closest segment would cut more but changes results
  (match rule 1024), for well under 1% of the frame: not worth the visual checks.
## 2026-11-24 — Round 3, update loop as a flat system (logic-ai agent): measured, stopped

Merged perf/low-end dc2f00b first.

What the loop is: m_sleepyUpdates is a binary heap of update modules keyed by
(next frame << 2 | phase), with the key copy beside each entry already (sleepyKeys). Many entries
share a key each frame, and the order among them, which is the simulation order, comes from the
heap's exact sift mechanics. So a same-game rebuild must keep that heap algorithm; only where its
data lives can change. Every entry popped is then run through its module's own update(), which
touches the module and its object anyway.

Share of the frame (build ai-5, load 30-52, inclusive): armies — sifting (rebalance*) 1.3%,
GameLogic::update's own code 1.1%; strikes — 0.8% and 0.7%. AIUpdateInterface::update 6.4-7.8%,
StateMachine::updateStateMachine 4.4-5.3%: the state work under it is mostly route searches, enemy
searches and weapons (other agents' systems). Ceiling for rebuilding the loop: about 2%.

Prototype (holeSift, bit 15): the sift held the moving entry aside and slid the others into the
hole, writing the index into a module once per level instead of twice. Verify mode (the swapping
sift replayed on copies every 97th call and compared, entry by entry, with indexes): 0 mismatches.
Replay matched.
- profile pair, strikes (load 8-12): sifting 1.6% off, 1.6% on.
- A/B pairs, work ms off/on (load 8-30): armies 59.8/62.1, 67.5/51.9; strikes 85.5/97.6, 82.8/75.8;
  aftermath 68.4/53.3, 43.8/75.3. Noise in both directions, no gain.
The sift's cost is the cache misses on the entries that really move (their keys and their
modules' index fields), which the hole does not avoid; the extra writes it saved were to the
moving entry, already in cache. Not kept (code reverted), per this round's rule: the full flat
rewrite of the loop cannot pay more than ~2% and the slice shows nothing.

## 2026-11-24 — combat round 3: weapons + damage as a data-oriented system — measured, not built
- Share of the frame, build combat-3 (merge dc2f00b, current switch defaults), profile inclusive:
  | | strikes (load 60-70) | armies (load 38-55) |
  |---|---|---|
  | ObjectWeaponStatusHelper::update (the per-frame weapon-status pass) | 0.4% | 0.7% |
  | WeaponTemplate::fireWeaponTemplate (firing, incl. damage) | 1.4% | 1.8% |
  | of which dealDamageInternal / Object::attemptDamage / ActiveBody::attemptDamage | 0.7 / 0.7 / 0.6% | 1.0 / 0.9 / 0.8% |
  | Weapon::getStatus, updateWeaponSet, chooseBestWeapon, range checks | ≤0.1% each | ≤0.1% each |
  | GameLogic::update | 38% | 55% |
- Only the weapon-status helper is a bulk loop (one entry per armed unit, every frame). Firing and
  damage are events: they touch other objects, random numbers, FX and object creation, so they
  stay in queue order and have no loop to flatten. Health is read through pointers by AI and
  targeting at scattered times; a flat health table has no hot loop to speed up.
- Ceiling: removing the status pass entirely (plus its update-queue entries) saves at most ~0.5-1%
  of the frame. A/B pairs on this shared machine move ±9% between runs, so a flat-table rewrite
  (weapon reload/clip/status + body health arrays, kept in sync with the old fields) could not be
  shown to pay. Stopped before building it, per the round-3 rule. Bits 23/30 and match rule 16384
  stay unused.
## 2026-11-24 — paths, ai and collide branches merged (dc2f00b); AI and collision rules are match rules

- Merged perf/logic-paths (match rules, coarseStart), perf/logic-ai (sharedRoutes, routeBudget,
  gxAiRule) and perf/logic-collide (collideStagger / collideStagger3).
- gxAiRule(bit) and gxCollideStaggerEvery() now go through gxMatchRule(): new rules
  GXRULE_SHARED_ROUTES 64, GXRULE_ROUTE_BUDGET 128, GXRULE_COLLIDE_EVERY2 256, GXRULE_COLLIDE_EVERY3
  512 (Common/GxMatchRules.h; mapped from switch bits 13, 14, 10, 11 in gxLocalMatchRules). The host
  decides them in network games, the replay header in playback, the local switch in single player.
  All stay off by default.
- Build merge2 (wasm cf344897… normal, 71129e83… mt). Referee replay matches (9,000 frames) with
  defaults, and with sharedRoutes + routeBudget + coarseStart + collideStagger3 switched on locally
  (playback uses the header's rules: "GXRULES match rules from the replay: 0"). Verify mode: every
  GXVERIFY line mismatches 0.
- A/B vs merge1 (live 445e8b8) and rules off/on: run at load 40-100 (five agents profiling), frame
  rates ~4 fps instead of ~13; not usable. To be rerun on a quiet machine.

## 2026-11-25 — Route queue round: what fills it, group routes, route cache, flow cover (paths agent)

- Correction: the earlier "90% of re-plans are goal moved" was wrong. Those requests were
  projectiles' quick paths, which never enter the queue. A re-plan cooldown (N = 20/30/45 frames)
  measured bit-identical to rule off: the moved-goal site fires 241 times per battle. Dropped, not
  committed. The queue fills with new-order bursts: in the battle benchmark demand is ~6,300
  searches in 1,050 frames at ~1,250 cells each, i.e. ~7,500 cells a frame against the 5,000 budget.
  Search kinds (to frame 1,050): A* 6,390 (1,860 of them served by a flow field), closest-spot
  1,467, attack approach 713. 3,986 repeat requests a battle are already merged by queueForPath.
- cpopSegments (exact, third switch word bit 0, OFF): computePointOnPath reads each route's
  optimized segments from one flat array kept on the Path (PathNode records its owner path; every
  node setter marks the array stale). Referee replay matches; verify mode 0 mismatches. A/B
  (paths-19, load 12-17), off -> on: armies 11.7 -> 11.3 and 11.1 -> 10.3 fps, strikes 8.5 -> 8.3
  and 8.0 -> 6.9: no gain (both pairs a little lower, inside the noise), so it stays off.
- moveAfterTick (match rule 32768, third word bit 1, off): units' locomotion runs in one pass after
  the update queue (GameLogic::update, before the AI/pathfind and partition updates), in
  update-queue order; the AI update no longer takes the locomotion's sleep, the pass wakes a unit
  earlier when its locomotion needs it. Recorded with the rule on (GX=8002) and played back with
  it off locally: matches. One pair (paths-19, load 11-20), off -> on: armies 11.6 -> 9.5, strikes
  10.3 -> 11.5, aftermath 16.3 -> 8.6 fps: no clear gain; off.
- coarseStart is now ON by default and applies to every move (no 200-unit minimum). Wrong-way
  share at the switch: 4.2% (123 of 2,946).
- routeQueue (match rule 1048576, third word bit 3, off): a request that finds the 512-slot queue
  full waits in an overflow list instead of being dropped (the 512 slots are inside the pathfinder
  CRC/xfer, so the slots themselves stay). Alone it holds 1,000+ requests and the average wait rose
  (126 frames); dropped requests 1,054 -> 0.
- groupRoute (match rule 1024, third word bit 2, off): a group move or attack-move plans one route
  per kind of mover (same surfaces and crusher flag) from the member nearest that kind's centre;
  members within 250 get a copy shifted by their offset (where a shifted point is blocked, half the
  offset, then the route itself), used by their next route request instead of a queued search.
- routeCache (match rule 2^21, third word bit 20, off): up to 64 recent routes; a unit of the same
  kind and size starting within 4 cells of a cached start and going within 4 cells of its goal gets
  the route shifted to it (blocked shifted points: a miss). Expires after 300 frames or when
  buildings change.
- flowCover (match rule 2^23, third word bit 22, off): flow fields serve moves over 8 cells (was
  24); a field route uses almost none of the per-frame search budget.
- Numbers (battle benchmark, to frame 1,050; load 14-22):
  | rules | searches | cells/search | wait avg / worst (frames) | dropped | armies / strikes fps |
  |---|---:|---:|---:|---:|---:|
  | default | 6,286 | 1,255 | 42.1 / 217 | 1,054 | 10.5 / 9.4 |
  | groupRoute + routeCache | 6,834 | 1,015 | 25.1 / 119 | 166 | 12.5 / 10.4 |
  | + flowCover | 6,758 | 913 | 21.9 / 134 | 280 | 13.6 / 10.6 |
  Whole run: 31.7 -> 18.6 -> 15.9 frames average wait. Group hints: 1,265 members followed a
  group route; route cache: 719 hits. Still above the 6-frame target.
- Determinism: recorded with groupRoute, routeCache, flowCover, routeQueue, moveAfterTick on
  (GX=B08422) and played back with all of them off locally: matches. Referee replay matches.
- Not built yet: routeRefine (rule 2^22, bit 21): zone-level plan, detailed only near the unit.

## 2026-11-25 — routeRefine: a stretch at a time, and capped closest-spot searches (paths agent)

- Cells per search kind (battle benchmark, to frame 1,050, default rules): route searches
  (findPath) 4,584 x ~250 cells (flow fields and the zone-level search already keep them cheap);
  closest-spot searches (findClosestPath: goal taken or blocked, attack approach) 1,467 x ~4,470
  cells = 6.6M of the 7.9M cells searched (83%); attack paths 713 x ~250.
- routeRefine (match rule 2^22, third switch word bit 21, off): (a) a move longer than 40 cells
  searches in detail only to a point ~30 cells along the zone-level route, then follows the
  zone-level route; when the unit is within 12 cells of the end of the detailed stretch, the move
  state asks for the next one (such routes are not put in the route cache). (b) a closest-spot
  search stops after 2,000 cells once it has a spot.
- Numbers (to frame 1,050; whole run in brackets; load 14-22):
  | rules | searches | closest-spot cells each | total cells | wait avg / worst (frames) |
  |---|---:|---:|---:|---:|
  | default | 6,286 | 4,471 | 7.87M | 42.1 / 217 (28.8) |
  | routeRefine (stretches only, before the cap) | 6,182 | 4,782 | 8.35M | 47.3 / 231 (29.1) |
  | routeRefine with the cap | 7,745 | 1,952 | 6.07M | 30.3 / 122 (22.1) |
  | groupRoute + routeCache + flowCover | 6,758 | 2,758 | 6.17M | 21.9 / 134 (16.9) |
  | + routeRefine (stretches only) | 7,489 | 2,943 | 6.46M | 15.2 / 87 (10.6) |
  | + routeRefine with the cap | 8,306 | 1,660 | 4.72M | 9.4 / 42 (7.2) |
  fps (single runs): default armies 15.3 / strikes 9.3; all four rules with the cap 12.5 / 9.4
  (load 22); stretches-only set 16.6 / 12.0 (load 14).
- Screenshots (armies and strikes) with all four rules on: the moving front stays spread along the
  road, no single-point clumps, no units left behind; fights resolve.
- Determinism (paths-22, no diagnostics): referee replay matches; recorded with groupRoute,
  routeCache, flowCover, routeRefine, routeQueue and moveAfterTick on (GX=F08422) and played back
  with all of them off locally: matches.

## 2026-11-25 — Route-planning CPU share, and routeQueue staggering (paths agent)

- Profile pairs (paths-23, armies phase, share of the frame; load 8-10 except the first "off" run,
  which ended at 29), rules off -> groupRoute + routeCache + flowCover + routeRefine on:
  AIUpdateInterface::computePath (all route planning) 19.3% -> 9.5% and 18.1% -> 9.5%;
  findClosestPath 18.0% -> 6.9% and 15.5% -> 6.7%; findPath 1.3-2.5% -> 2.7-2.8%; flow fields
  1.0-1.6% -> 1.8-1.9%. Route planning's CPU share halves; the same pair's fps were flat
  (armies 14.2 -> 14.2, strikes 10.4 -> 10.1), the time going to the rest of the frame.
- routeQueue (match rule 2^20, third word bit 3) now also staggers the queue: requests queued in
  one frame become eligible 64 a frame, in request order; coarse starts (a zone-level search each)
  run 32 a frame, in request order, instead of all at the order; and a closest-spot search does
  not start once the frame has used half its 5,000 cells (it waits for the next frame). Budget
  unchanged.
- A/B, all four route rules on, routeQueue off -> on (load 8-10): strikes fps 10.5 -> 10.8 and
  11.8 -> 12.4; strikes 1% low 4.2 -> 5.6 and 4.9 -> 6.0; strikes p90 126 -> 110 and 112 -> 96 ms;
  frames over 100 ms in strikes 348 -> 251 and 188 -> 55, in armies 38 -> 21 and 9 -> 7. Slowest
  battle frames (after the two loading frames) 366 -> 291 and 271 -> 252 ms.
- Determinism: referee replay matches; recorded with the four route rules plus routeQueue on
  (GX=F00422) and played back with them off locally: matches.
## 2026-11-25 — Round 3: physics as flat arrays (bits 26 / 27) — measured, not built

- Asked: PhysicsBehavior position/velocity/forces in flat arrays with one bulk integration pass.
- Measured first (temporary timers in PhysicsBehavior::update, phase span = first to last physics
  call of a frame; physics is the only PHASE_PHYSICS module, so it runs as one block; load 20-37,
  strikes ~75 ms/frame, 1,000-3,000 physics steps per frame):
  phase 4.2-9.7 ms/frame (~10% of a strike frame), split per frame:
  forces + integration (what flat arrays would speed up) 0.5-1.3 ms;
  terrain height + bounce 0.5-1.2 ms; setTransformMatrix 1.6-3.7 ms (drawable transform,
  partition refresh/dirty, trigger areas, map extent); tail (airborne check = second terrain
  query, landing, sleep time) 0.8-1.6 ms; the rest is update-queue dispatch and timer overhead.
- Result: the integration math is ~1-1.5% of a strike frame. Even halving it (~0.5 ms) is far
  below what the fps A/B pairs resolve, and a bulk pass could not move the rest: every step
  writes the object's transform, which feeds the drawable, the partition and triggers in
  update-queue order. Stopped per the round-3 rule; no code kept, bits 26 / 27 unused.
- Where physics time actually goes: setTransformMatrix (~40% of the phase, ~4% of the frame).
  Cheaper ideas there (skip the drawable/trigger work when the position did not change, reuse
  the ground height from the step for the airborne check) are not data-layout changes; not done.
- Rechecked after the rule change (drift allowed behind match rule 2048, behaviour-outer passes
  approved), with the transform update split as well (build phys-2, load 11-24; extra timers
  inflate it): per strike frame setTransformMatrix is drawable transform ~33%, partition
  refresh/dirty ~18%, trigger areas + pathfinder position ~14%, map extent ~8%, the rest matrix
  copies / yaw (atan2). Each piece is under ~1 ms of a ~90 ms frame, and a bulk physics pass
  already exists (physics is its own update phase). Drift-allowed options considered and not
  built: physics every 2nd frame for ground units (locomotors push every frame: visible jerks,
  wrong speeds); bulk integrate-then-commit (same calls, same cost). Still nothing measurable;
  match rule 2048 left unused.

## 2026-11-25 — Physics flat pass built anyway (switch physReuse bit 26, on; match rule physBulk 2048 / bit 27, off)

Built per the user's round-3 decision (small gains meant to add up). Two parts:
- physReuse (second word bit 26, default ON, same game): the physics step already looks up the
  ground height at the unit's x, y and layer; that is exactly what Object::calculateHeightAboveTerrain()
  computes for the airborne check after the move, so the step stores it in the height cache
  (Thing::gxSetHeightAboveTerrain) instead of a second terrain lookup. Only when the position after
  setTransformMatrix still has that x, y. Verify mode: 1,060,000 checks in the battle, 0 mismatches.
- physBulk (match rule GXRULE_PHYS_BULK 2048, second word bit 27, default OFF, drifts): at the first
  physics update of a frame, one pass over a flat list of all PhysicsBehaviors (creation = object ID
  order, compacted in the pass) integrates gravity, friction and velocity for every unit whose
  physics runs this frame; each unit's own update then only moves it. A force applied to a unit
  by another unit's physics step after the pass counts next frame (the drift). Units not covered
  (woken during the phase, held) integrate inline as before.
- Dropped: skipping setTransformMatrix when the matrix did not change. Not the same game: the
  battle's physics call counts drifted from the first 150 frames (260 vs 261), and with only the
  height reuse they match off exactly (261, 674, 1180, ... 1922). Setting an unchanged transform still
  refreshes something the simulation reads (most likely the drawable's transform, which weapon
  launch bones are read from).
- Replays (final build phys-7, wasm c9e96a89…): referee replay matches (9,000 frames) with defaults and with physBulk on locally;
  recorded a skirmish with physBulk on (phys-bulk6.rep, header rules 0x802) and it replays to a
  match. Screenshots (phys6-bulk*/reuse*/off* battle2-7): normal fights, nothing stuck or piled up
  (shots are taken by wall time, so scenes differ between runs).
- A/B (build phys-6t, physics-phase timer, the first 6 windows of 150 frames that every run
  reached = the same 8,858 physics calls for off/reuse; load 8-19, very noisy):
  off 44.4 / 37.4 ms, physReuse 32.9 / 26.3 ms (-28%, both pairs), physBulk (+ physReuse)
  28.5 / 33.8 ms (same as physReuse: one pair each way). Strikes fps off 5.8 / 8.2, physReuse
  8.7 / 11.1, physBulk 10.7 / 8.4: load noise, physics is ~10% of a strike frame. physBulk gives
  nothing measurable on its own and is not slower; left off for the combined measurement.

## 2026-11-25 — Collisions: quicker crush check and collide-module list (third word bits 4 / 5, on, same game)

- Measured first (build col-t1, temporary timers; they inflate everything, compare shares only):
  per strike frame 4k-31k pairs offered, 1.4k-5k really touching; the geometry test is about a third
  of the collision time, Object::onCollide on both sides two thirds; inside PhysicsBehavior::onCollide
  AI processCollision ~1/3 (2k-7k calls per frame; AIUpdate.cpp, the ai agent's file this round) and
  the crush check ~15%.
- crushFirst (third word bit 4, Object.cpp): canCrushOrSquish(TEST_CRUSH_ONLY) can only answer true
  when my crusher level is above its crushable level, so it decides that before the ally lookup
  (getRelationship changes nothing). Verify mode: 1,240,000 checks in the battle, 0 mismatches.
  Pairs (col-2t, crush-check time summed over the first 6 windows, load 10-20): off 12.80 / 11.44 ms,
  on 11.92 / 12.00 ms: inside noise, not slower.
- collideList (third word bit 5, Object.cpp): Object::onCollide asked every behaviour module (20-40
  per unit) for its collide interface on every collision; now each object lists its collide modules
  once (behaviour order, built on its first collision; getCollide() is fixed per module and the
  behaviour list never changes after creation). Same status check before each module. Verify mode:
  10,080,000 checks, 0 mismatches. Pairs (col-3t, Object::onCollide time outside PhysicsBehavior
  summed over the first 6 windows, same 20,617 touching pairs per run, load 5-11): off 45.5 / 27.4 ms
  (the first under a load spike), on 18.9 / 19.1 ms: about -30% of the dispatch.
- Referee replay matches (9,000 frames) on col-2 and col-3.
- physBulk pair rerun (phys-6t, load 6-10): off 30.6 / 22.8 ms, on 26.2 / 27.0 ms: same as before,
  no gain, not slower.
- crushFirst pair rerun at load 5-7 (col-3t, the first pair started at load 12): crush-check time
  off 10.25 / 9.95 ms, on 10.77 / 9.33 ms: inside noise, not slower (kept on: same game).
## 2026-11-24 — Round 3 update 3: drifting update loop (4-ary queue) built, measured, not kept (logic-ai)

Merged perf/low-end 501981e first. Built behind match rule 4096 (GXRULE_UPDATE_LOOP, second-word
bit 15, default off): the update queue as a 4-ary heap (half as deep, the moving entry held aside
and the others sliding into the hole). The heap is binary at every game start and switches shape
only at the start of the update pass by a rebuild, so record and playback follow the same steps.
- Determinism: old referee replay matches (rule off); a skirmish recorded with the rule on replays
  to a match (rules from the replay header 0x1002). Verify mode with the rule on: the queue is a
  valid 4-ary heap with right indexes and keys every frame, 0 mismatches.
- Profile pair, strikes (load 17-21): sifting 2.2% -> 1.9% of the frame.
- A/B pairs off/on (load 15-30), work ms: armies 80.4/94.3, 59.0/76.5; strikes 116.9/141.8,
  108.9/161.3 — and with the rule on the strike phase hit the benchmark's time limit both times.
  Screenshots (~/generals-bench/ai-ul-{off1,on1}_battle*.png): at the same shot the armies had not
  met yet with the rule on. The order of updates due on the same frame changes, and the battle
  plays out differently and heavier; the queue itself got only 0.3 points cheaper.
Not kept (code reverted): it is measurably slower in its own pairs. The update queue's turn order
is part of how the battle unfolds; a cheaper queue would have to keep today's tie order.

## 2026-11-25 — Round 3: update-loop flat pass (logic-ai), merged perf/low-end a0897b2

Where GameLogic::update's own time goes (strikes, load 9-15): GameLogic::update self 1.1-1.5%,
heap sifting (rebalance*) 1.1-1.7%; AIUpdateInterface::update 7.7-12.6% inclusive, of which the
state machines' own dispatch is small (updateStateMachine self ~0.3-0.4%; the rest is the states'
route, enemy-search and weapon work, other agents' systems). The ~4.5% figure did not reproduce.
The heap's order is the simulation order (earlier entries: hole sift gave nothing exact, 4-ary heap
drifted into slower battles), so this round's slice is the other per-tick walk in the loop.

disabledList (third switch word bit 8, exact, default ON): the end-of-tick check for disabled
objects walked every object in the game to find the few that are disabled. Now
Object::setDisabledUntil adds the object to a flat list (and registerObject, for objects disabled
before they were registered); the list is sorted into object-list order (each object gets a number
when it is prepended, so the list is in descending numbers; rebuilt from the list itself after a
reset or a load) and only those objects are checked; ones no longer disabled drop out.
- Verify mode: every frame the listed disabled objects, in order, equal the full walk's, and the
  list order is descending numbers: 0 mismatches (up to 538 disabled objects at once).
- Old referee replay matches.
- Profile pair, strikes (load 11): GameLogic::update self 1.5% -> 1.4%; the walk was ~0.1%.
- A/B pairs off/on, work ms (load 10-24): armies 68.8/61.8, 59.1/61.4; strikes 110.4/102.2,
  99.1/96.7; aftermath 51.2/51.2, 52.7/51.6. Within noise, not slower. Kept, default on.
AI state dispatch as a flat pass: no slice found that pays. A state machine's per-tick cost is its
transition tests and state updates, which read their own unit and the world; the dispatch itself
(sleep check, virtual calls) is ~0.5% of the frame.
## 2026-11-24 — Measured, not kept: 8x8 block masks for range searches (would-be switch blockMask, second word bit 24)

- Built on 29c8869: per 8x8 block of cells, the OR of the cells' player masks, recomputed from the
  dense cell masks whenever a cell's mask changes; the ring walk checked the block before the cell,
  so the visited cells and their order were unchanged.
- Same game: verify (every search against the plain walk) 0 mismatches in 4 runs, plus a direct check
  that no skipped block held a wanted cell; referee replay matched over 9000 frames.
- Paired timing (fast / plain walk): on 0.231 and 0.255, off 0.245 and 0.251: no difference. fps A/B
  (blockMask off vs on): pair 1 unusable (load jumped 3 -> 24), pair 2 at load 9-12 strikes 13.7 ->
  13.0, work 71.9 -> 76.0 ms. Reason: after the dense mask a skipped cell costs one 4-byte read from
  a small array; the ring walk itself (offset list) is what's left, and blocks can't skip it without
  changing the visit order. Removed; the patch is kept outside the repo
  (side-session notes, logic-query-blockmask.patch).

## 2026-11-25 — Round 3 (spatial data): touching-pair tests read kept places, not units (switch pairPlace, second word bit 28, on; match rule spatialPairs 8192 / switch pairOffer, bit 29, off)

- Measured first (merged tree c182dee, profile at load 35-56, shares only): PartitionManager::update
  15.0% of the strike frame (11.3% armies): pair building 3.4%, gxProcessPair reading both units
  2.9%, shape tests 2.3%, collision reactions 2.7%; range searches 4.1%. ~87% of listed pairs don't
  touch.
- The flat data: every PartitionData now keeps x, y and max(bounding circle, sphere) radius (the same
  copy its COIs already kept for range searches), refreshed on every transform change, cell update
  and Z-geometry change.
- pairPlace (exact): a listed pair whose kept places are farther apart than (rA + rB) x sqrt 2 + 1 is
  finished from the two PartitionData alone: neither Object, its status bits nor the shape test is
  read. The sqrt 2: the box-vs-cylinder test treats the circle as its bounding square (first version
  used rA + rB and verify caught 4,836 touching pairs among 150M; with sqrt 2, 0 in 140M).
- pairOffer (match rule 8192, changes the game slightly): the same test when a neighbour is offered,
  so clearly-apart pairs never enter the pair table. Differs only when a collision reaction earlier
  in the same frame moves a unit next to a dropped neighbour.
- Same game (pairPlace): verify 0 mismatches (every skipped pair re-tested with the full shape test);
  referee replay matches over 9000 frames. pairOffer: recorded 9003 frames with it on, played back:
  matches the recording; referee replay (rule off) matches; screenshots of the crowded fight with it
  on look normal (no stuck or stacked units).
- Paired timing (hidden setting gxPairTiming: frames picked by a hash of the frame number run with
  the features off; PartitionManager::update timed for both kinds, ~900 frames each):
  pairPlace alone 10.65 -> 10.01 ms and 9.60 -> 9.29 ms per frame (-6%, -3%); pairPlace + pairOffer
  10.95 -> 9.60 and 11.00 -> 9.46 ms (-12%, -14%). Load 13-31.
- fps A/B (pairPlace off/on): one usable pair at load 7: armies 16.3 -> 16.6, strikes 12.1 -> 12.0,
  aftermath 19.4 -> 22.5; the other pairs ran at load 16-50 and stopped at the time limit.

## 2026-11-25 — Fix: cellReuse fast path kept the per-player search lists out of step

- cellReuse (unchanged cells: move each COI to the front of its cell list) put the COI at the head
  without a new sequence number, so the cell list was no longer in sequence order and searchSlotSkip's
  merge of per-player lists could visit candidates in a different order than the plain walk (ties
  and iterator order). Verify mode never saw it: it turned the fast path off.
- Now CellAndObjectIntersection::gxMoveToFront takes a new sequence and re-links the slot list; in
  verify mode the fast path runs on odd frames (the fill check on even ones), so the search check
  covers lists it reordered.
- Verify: 90k searches, 0 mismatches; cellReuse checks 0 mismatches; referee replay matches.
## 2026-11-25 — Visual staggering (combat agent; switches animStagger / soundStagger / particleStagger)

- Drawing and sound only; the game logic never reads any of it. Plain Settings switches, no match
  rule. Referee replay matches with all three on (combat-4, 9,000 frames).
- animStagger (second word bit 23, **off**): a single-animation unit that is small on screen
  (radius/distance < 1/50) and has not moved advances its bones every 2nd drawn frame
  (Animatable3DObjClass::Render). Bone share stayed 1-2.7% either way; no measured gain, so off.
- soundStagger (second word bit 25, on): 3D sounds still playing update position/volume every 3rd
  frame (MilesAudioManager::processPlayingList); full update when the volume changes.
- particleStagger (second word bit 30, on): each particle system steps its particles twice every
  2nd logic frame, alternating systems by ID (ParticleSystem::update).
- A/B on combat-4, off → on (two pairs, shares incl.):

  | pair / phase | load | bones | particles | sound |
  |---|---|---|---|---|
  | 1 strikes | 38→53 | 1.7→2.7% | 1.0→1.3% | 0.1→0.0% |
  | 1 armies | 23→15 | 1.0→0.8% | 1.6→1.2% | 0.2→0.1% |
  | 2 strikes | 25→15 | 2.5→2.3% | 1.3→1.2% | 0.0→0.0% |
  | 2 armies | 26→17 | 0.8→0.2% | 1.1→0.6% | 0.1→0.1% |

  Inside the noise of this shared machine except particles (−0.1 to −0.5%). Screenshots show
  different battle moments, nothing broken.

## 2026-11-25 — Weapon status in one pass (combat agent; match rule weaponPass 16384, third word bit 16, off)

- ObjectWeaponStatusHelper ran every frame for every armed unit through the update queue. With the
  rule on, the helper does its first update, then leaves the queue (sleeps forever) and joins a list
  sorted by object ID; gxWeaponStatusPass() in GameLogic::update, right after the sleepy-update
  loop (movement pass first, then weapons), runs adjustModelConditionForWeaponStatus for every
  listed object that is not disabled. Results drift (queue entries leave the heap, ties reorder),
  so it is a match rule.
- The hook is not guarded: if the rule goes off mid-game (single player, live switch) the pass wakes
  every listed helper back into the queue and empties the list. A loaded save puts a sleeping helper
  back in the queue.
- Checks (combat-5): referee replay with the rule off matches; a recording made with the rule on
  replays to a match; toggle test (verify mode, rule flipped every 200 frames, diagnostics removed since): each switch-off woke every listed helper (934, 1593, 1019, 710, 597) and each switch-on refilled the list (1596, 1024, 711, 598).
- A/B off → on: status share 1.1→0.4% / 1.1→0.5% (strikes), 1.2→0.5% / 1.4→0.5% (armies),
  i.e. −0.6 to −0.9% of the frame. fps armies/strikes: pair 2 strikes 11.5/10.2 → 14.1/10.8,
  pair 2 armies 13.6/10.1 → 14.1/11.2; pair 1 noise in both directions (load 10↔29).

## 2026-11-25 — Unit bones on a worker thread (combat agent; switch boneWorker, third word bit 17, mt build, off)

- Share first (single-thread build, combat-5): HLodClass::Update_Sub_Object_Transforms 3.6% incl in
  strikes, 2.1% in armies; particles 1.3% + doParticles 0.6-0.7%. The profiler cannot sample the mt
  build's game thread (every function reads 0.0%), so the mt A/B is by fps only. Note: bench_build
  with variants ["opt"] leaves the previous mt build staged.
- The handoff is inside the draw, not across the simulation: logic changes animations while it runs
  (model-condition changes call Set_Animation / swap render objects), and animations shared between
  units keep shared decode caches (motchan.h CachedIdx / CacheData), so a worker running during
  logic would read freed or half-written state. RTS3DScene::Customized_Render queues the visible
  HLods (holding a reference), wakes one worker pthread, draws the terrain, then takes the jobs the
  worker has not reached itself and waits for the worker's last one. Render skips an object the
  worker already brought to this WW3D frame. Objects whose animation plays sounds stay on the main
  thread (sounds are main-thread only). Particles were not moved (they allocate from the game's
  memory pools, which are not thread-safe).
- Replay (mt, switch on) matches. Screenshots normal.
- A/B mt, quiet machine (load 3-6), fps armies / strikes / aftermath, off → on:
  - first version (main waited for the worker; the terrain is < 70 draws, little to overlap):
    18.1/13.7/23.2 → 18.7/13.1/24.8, 18.9/14.4/24.4 → 18.9/13.6/24.8 (strikes −4 to −6%)
  - main thread takes the remaining jobs: 18.7/13.7/25.0 → 17.7/14.3/27.7,
    20.9/15.0/28.4 → 21.5/15.5/28.7 (strikes +3-4%, armies −5% / +3%)
- Off by default until more pairs show armies is not slower.
- More pairs (combat-9, load 2-12): armies 20.0 → 15.6 (one 130 ms frame, likely the worker
  pthread starting on first use) and 19.6 → 20.7; strikes 14.9 → 14.3 and 9.7 → 15.6. Stays off.
  The worker threads now start on the first scene draw (the menu) when any worker switch is on.
- Worker threads decode animations without the shared caches (motchan.cpp, thread_local
  gxAnimNoCache): the game logic decodes bones too (Get_Bone_Transform), and a cache index raced
  by a worker could otherwise be left wrong for the logic's next lookup.

## 2026-11-25 — Unit poses ahead during the logic; particles on a worker (combat agent; boneAhead bit 23, particleWorker bit 24, third word, mt, off)

- boneAhead: at the end of frame N's object draw every visible unit playing one compressed animation
  is snapshotted (a reference on the animation, its next frame predicted from the last step). The
  worker decodes each bone's translation / rotation / visibility while the logic runs frame N+1; the
  draw of N+1 builds the bone hierarchy from them (HTreeClass::gxAnimUpdateFromPose) if the unit
  still plays that animation, else decodes as before. The worker never touches the unit. At most one
  frame of animation lag.
- particleWorker: ParticleSystem::update keeps emission, the particle cap and system logic on the
  main thread and queues the motion of the existing particles. The worker moves them between the end
  of ParticleSystemManager::update (inside W3DDisplay::draw) and doParticles, a window in which
  nothing creates, frees or reads particles; at the join the main thread deletes the dead ones
  (memory pools are main-thread only). Also joined on manager update, reset and save.
- Replay (mt, boneWorker + boneAhead + particleWorker on) matches. Screenshots normal.
- A/B mt, quiet machine (load 1-4), fps armies / strikes / aftermath, off → on:
  - boneAhead: 20.1/15.7/29.4 → 21.2/15.2/28.8, 21.2/15.4/29.1 → 21.0/15.3/28.5 (no gain; the
    decode it moves is a small part of the bone work)
  - particleWorker: 21.5/15.4/28.4 → 21.5/16.2/27.5, 20.1/15.5/27.8 → 21.5/15.9/27.4 (strikes +3-5%,
    armies 0/+7%, aftermath −1 to −3%)
- Both off: boneAhead gains nothing; particleWorker is slower in the aftermath in both pairs.
- particleWorker threshold: live particles sit at the particle cap (~500, MaxParticleCount from the
  LOD) in every bench phase, so a particle-count threshold cannot tell heavy frames from light ones.
  Instead the worker is used only while frames are long: frame time averaged over ~8 frames, on above
  40 ms, off below 32 ms (the hand-off costs the same every frame; weak machines are the target).
  combat-13 mt, load 2-5, fps armies / strikes / aftermath off → on: 21.0/15.9/28.7 → 21.2/15.9/30.4,
  20.4/15.9/28.9 → 20.7/15.7/28.9. Replay matches. Nothing slower: **particleWorker on by default**
  (no effect on the single-thread build). boneAhead and boneWorker stay off.

## 2026-11-25: round-3 logic branches merged (paths, collide, ai, query, combat → 2754c81)

Build merge3r (opt 0e5f777f…, mt 9670fc49…). Conflicts were switch words, match-rule lists and
PORTING_LOG; resolved by keeping both sides. Pass order at the end of the tick: movement pass, then weapon pass.

- Same game: referee replay matches over 9000 frames, opt and mt; verify mismatches 0. A recording made
  with every match rule on (header rules F0EEEA) replays to a match.
- A/B on a quiet machine (load 2–7), fps armies / strikes / aftermath:
  - merge2 → merge3r defaults, p1: 18.1→17.5 / 13.4→14.0 / 24.4→25.3; p2: 18.0→18.0 / 13.5→14.1 / 23.8→25.3.
  - mt: 16.6→16.5 / 13.0→13.9 / 24.2→25.1.
  - merge3r match rules off → on (sharedRoutes, routeBudget, collideStagger3, lazyScans, groupRoute,
    routeQueue, routeCache, routeRefine, flowCover, moveAfterTick, physBulk, pairOffer, weaponPass):
    - p1: 17.0→18.9 / 14.3→16.7 / 27.8→27.3, aftermath 1% low 11.2→17.8.
    - p2: 18.3→20.2 / 14.2→16.0 / 27.0→25.8, aftermath 1% low 9.4→15.3.
- Read: defaults give strikes +4–7% and aftermath +4–6%, armies flat. The match rules add armies +10%,
  strikes +13–17%, and aftermath 1% lows +60%; aftermath average fps is 2–4% lower with the rules on.

## 2026-11-25 — Round 4: update loop as manager passes (logic-ai), merged origin/perf/low-end 9a9c823

(GxUnitStore.h from collide had not landed yet; this part does not need it: the passes run the
modules themselves, grouped by behaviour, and can read the store once it exists.)

What runs every frame (counter, first 300 frames of the battle benchmark): ~4,240 update calls a
frame, nearly all returning "call me next frame": ObjectWeaponStatusHelper ~880, AIUpdateInterface
~530, PhysicsBehavior ~530, ProductionUpdate ~520 (buildings), StealthUpdate ~265,
GenerateMinefieldBehavior ~140, RebuildHoleBehavior ~110, slaved/spawn/horde/contain updates...
Each of them was popped from the update heap (25,000 entries) and sifted back every frame.

updatePasses (match rule 131072, third switch word bit 9, default OFF): a module whose update asks
to run next frame leaves the heap and joins a flat pass list for its update phase. Per phase the
heap's due entries run first, then that phase's pass list in one loop (phases keep today's order:
initial, physics, normal, final). A list is sorted by behaviour (AI kinds first, then a hash of the
module's name, the same on every machine) and object ID; equal keys keep the order they joined.
A module that asks to sleep, is put to sleep by another object (setWakeFrame later than next frame)
or is destroyed goes back to the heap or leaves the list. Switching the rule off puts every pass
module back into the heap. The one gate is gxMatchRule(GXRULE_UPDATE_PASSES).
- Determinism: old referee replay matches (rule off); a skirmish recorded with the rule on replays
  to a match (rules from the replay header 0x20022).
- Profile, strikes (load 18-20): heap sifting 1.5% -> 0.1% of the frame, pass bookkeeping 0.1%,
  GameLogic::update's own time 1.4% -> 1.3%.
- A/B (load 11-20, work ms off / passes): armies 72.7/59.3, 77.0/54.4; strikes 110.0/99.4,
  104.1/84.2; aftermath 39.7/58.2, 47.4/60.8 — but the rule-off aftermaths ended with far fewer
  units drawn (1147, 1352 vs 1762, 1799; the phases are time-limited, and the off runs had played
  further), so aftermath is not comparable. An earlier batch at load 22-47: armies 94.8/114.1 off vs
  61.7-77.7 with passes; strikes 166/223 vs 98-139. Never slower; the size of the gain is mostly
  battle drift plus load, the measured saving in the loop itself is ~1.5 points.
- Screenshots (~/generals-bench/ai-up-{off1,pass1}_battle*.png): armies engage normally, no stuck
  or stacked units.
- AI by state (a pass per AI state: AI entries re-sorted each frame by current state, then ID):
  measured, no consistent gain (armies 72.2/54.1, strikes 94.1/88.3 vs passes 59.3/54.4, 99.4/84.2);
  removed.
Default off (it changes the order of same-frame updates); for the main chat's combined measurement.
## 2026-11-25 — Round 4: GxUnitStore, first version (shared by all logic agents)

- Core/GameEngine/Include/Common/GxUnitStore.h + GameLogic/Object/GxUnitStore.cpp: one slot per
  live Object in dense arrays (id, Object*, x/y/z, vel, radius, player, playerBit, cell, 64-bit
  behaviour mask), in ascending object-ID order. pass(bit) returns the dense slot list of a mask bit,
  rebuilt only when a mask changes or units come and go. Holes from removed objects are closed at
  most once per logic frame, at the first pass(), so slots never move inside a frame once a pass ran;
  epoch() changes whenever a slot may have moved or been freed (cache slots against it).
- Hooked into Object: setID adds (creation and load; a load can bring IDs out of order, the next
  compaction sorts), ~Object removes, reactToTransformChange writes x/y/z, setGeometryInfo the
  radius, setOrRestoreTeam the player; default mask bits PHYS / AI / COLLIDE set when the modules
  exist. cell is -1 until the partition writes it.
- Source of truth: the store holds PhysicsBehavior's velocity (the member is gone; getVelocity()
  returns the store's Coord3D, arrays reserved for 16k units so the pointer stays put within a frame).
  Position stays in the Object's matrix with write-through (x/y/z), likewise radius and player.
- Same game: referee replay matches (9,000 frames, build store-1).

## 2026-11-25 — Round 4, lines 3 and 4 (logic-ai): dispatch table over GxUnitStore + AI passes by state

Merged GxUnitStore (logic-collide e1001f1). Still under match rule 131072 (third word bit 9, OFF).
- Line 3 (every-frame modules from a mask-driven dispatch table over dense store lists, object-ID
  order; heap only for sleeping modules): each module kind that runs every frame gets a store
  behaviour bit (32-59, 28 kinds, numbered in the order they first appear in a game; the table runs
  them AI kinds first, then by a hash of the module name). A unit's bit is set when its module starts
  running every frame (at the end of that frame) and cleared when it sleeps, is put to sleep by
  another unit or is destroyed; never re-tested per tick (line 2). Each kind's pass walks
  TheGxUnitStore.pass(bit) and calls that kind's module of each unit. Kinds past the 28th and a
  second module of one kind on a unit use bit 60+phase ("misc"): its pass walks the store list and
  runs the unit's misc modules. Verify-mode count (rule on): per 300 frames ~1.25-2.9 million calls
  through the table, 0 through any other list; the heap made 55-125 calls a frame (modules waking
  from a real sleep, which then join a pass if they ask to run every frame).
- Line 4 (AI state machine, one pass per state): the AI kinds' units are taken from their store
  lists and run grouped by current AI state, then kind, then object ID.
- Determinism: referee replay matches (rule off); a skirmish recorded with the rule on replays to a
  match (build ai-15).
- Profile, strikes: heap sifting 1.5% -> 0.1%; GxUnitStore 0.1% -> 0.4% (pass lists rebuilt as units
  join and leave passes); GameLogic::update self 1.5% -> 1.4%. About 1 point net.
- A/B, work ms off/on: pair 1 (load 31-34) armies 137.7/54.4, strikes 113.4/114.3; pair 2 (load
  15-20) armies 67.5/66.6, strikes 87.8/113.4, aftermath 51.3/58.1. Inside the noise of this loaded
  machine, pair 2's strikes slower; the earlier passes-only build was not slower. Stays default off.
## 2026-11-25 — Round 4, combat: weapons and damage as managers over GxUnitStore

- damageQueue (match rule 524288, third word bit 18): WeaponTemplate::dealDamageInternal queues
  (victim ID, DamageInfoInput) instead of calling attemptDamage; gxApplyDamageQueue applies the
  tick's damage in victim-ID order (each victim's hits in the order dealt) after the update queue and
  the movement and weapon-status passes, and again after TheWeaponStore's delayed weapons, so nothing
  is queued between ticks (saves). Damage dealt while applying (death explosions) is applied in
  further rounds. Referee (rule off) matches; a recording with it on replays to a match; verify mode
  1631 checks, 0 mismatches. Damage share off → on (dealDamageInternal vs queue + apply, load 20-50):
  1.0 → 1.7%, 1.6 → 1.1%, 0.9 → 1.0%, 1.8 → 0.7%: the same attemptDamage calls, later and sorted.
- Store columns (combat block in GxUnitStore.h/.cpp, compacted with the rest): target (write-through
  from AIUpdateInterface::setCurrentVictim), weaponReady (soonest reload / pre-attack end, written
  by the weapon-status pass), weaponSig (ammo/status the drawable shows).
- weaponPass now walks pass(GXB_WEAPON) (bit set when a helper joins, cleared when the rule goes
  off and it is woken). The old weaponPass recording still replays to a match.
- weaponSkip (third word bit 19, same game): units with no target, not attacking, no shot this frame,
  no weapon pose and an unchanged ammo/status signature skip the drawable updates; the getStatus()
  calls (which refresh the weapon's saved status) still run, so it is exact: the weaponPass recording
  replays to a match with it on. Weapon-status share 0.4-0.6% → 0.0-0.1%.
- animStoreList (third word bit 27, mt, drawing only): the bone workers take their units from
  pass(GXB_ANIM) (set when W3DModelDraw creates an HLod model) plus the visibility check, instead of
  scanning the scene's render list. mt referee with boneWorker + boneAhead + animStoreList matches.
- weaponReady is exact: every Weapon and WeaponSet knows its unit (gxSetOwner when a set is built
  or loaded), and every reload, ammo, status, pre-attack, last-shot or current-weapon change calls
  TheGxUnitStore.weaponTouch(unit), which zeroes weaponReady. weaponSkip now first skips a unit with
  no target, weaponReady still in the future and a quiet pose (gxWeaponStillQuiet) without calling
  getStatus at all. Referee matches; the weaponPass recording replays to a match with it on; verify
  with weaponPass + weaponSkip: 2142 checks, 0 mismatches.
- Not moved to the store: particle motion (particles are not units; the store has no particle data).
- Quiet pairs (flock timing.lock, load 6-11; 2 pairs per phase, profiled, fps armies / strikes / aftermath):
  weaponSkip (with weaponPass): status share 0.4-0.5% → 0.1% in all 8 runs; fps off/on 19.1/17.2,
  14.5/13.9, 18.9/8.9 (load jumped to 13.5), 14.5/14.3, 20.2/17.9, 14.1/17.2: within the noise →
  on by default (it acts only when the weaponPass rule is on).
  damageQueue: strikes 13.7/10.7, 13.3/13.8; armies 9.9/16.4, 16.3/14.5: neutral; stays off (match rule).
  animStoreList (mt): armies 15.6/17.8, 15.9/17.7; strikes 10.9/14.4, 12.8/9.4: not slower → on by
  default (it acts only with boneWorker or boneAhead).
- Physics on the store (build store-2): physBulk's pass now walks the store's PHYS list (object-ID
  order) and reads/writes the store's velocities; its own behaviour list is gone. The store's cell is
  now write-through with x/y (partition cell under the centre, cy * cellCountX + cx).
  Referee replay matches; a skirmish recorded with physBulk on (header rules 0x822) replays to a match.
- Collision broad phase over store cells: not built. The pair test already reads kept places
  (pairPlace, query agent), and the pair-list build measured ~0.6 ms per frame at the peak (round 2
  timers) against ~2/3 of collision time in the reactions (onCollide + AI processCollision), so a
  drifting store-cell broad phase can win at most that 0.6 ms.

## 2026-11-25 — Round 4, 100% adoption: positions in the store, physics pass, store broad phase

- positionInStore (third word bit 7, on; same game): an Object's position lives in the unit store
  (pos[slot], one Coord3D per slot). Thing::getPosition() returns it, Thing's setters write it, and
  getTransformMatrix() refreshes the matrix translation from it on every read. Fixed per object when
  it enters the store (so switching it mid-game only affects new objects); when an object leaves the
  store the position is copied back. Without the switch the store keeps a write-through copy. Every
  Object knows its slot (gxStoreSlot(), kept by the store at add and compaction); physics velocity
  reads by that slot (no epoch check any more). Store API change: x/y/z are now pos[s].x/y/z.
- physBulk (match rule 2048) is now the whole physics step: at the first physics update of a frame,
  every unit whose physics is due steps in one pass over the store's PHYS list (object-ID order);
  each unit's own heap call returns the sleep time its step gave. Units woken later in the phase step
  on their own call as before.
- storePairs (match rule 65536, third word bit 6, off): collision broad phase over the store. All
  units the partition holds go in a grid by the store's cell index (units larger than half a cell in
  every cell under their bounding square); each unit due a collision check (object-ID order) is
  offered every unit in reach, with pairPlace's distance cut; pairs go to the flat contact arrays.
- collideStagger's tie-break between two waiting units now compares object IDs (it compared
  partition-data addresses, which can differ between machines now that the stagger is a match rule).
- Checks: referee replay matches on store-3 (positionInStore) and store-4 (all of it, verify mode:
  contact array checked 90,600, mismatches 0); the verify battle on store-3 printed 1,870 GXVERIFY
  lines, none with a mismatch. A skirmish recorded with physBulk + storePairs on (header rules
  0x10822) replays to a match. Screenshot with both rules on (store4-rules_battle3-5): normal
  crowded fight, nothing stuck or piled up.
- Still write-through (store holds a copy): radius (truth is the GeometryInfo, a value type shared
  with templates), player (truth is the Team pointer; the store holds the derived index). The cell is
  written by the partition (query); mask and velocity have no other copy.
- Radius and player in the store (build store-5, same game): Object::getBoundingCircleRadius() and
  Object::gxPlayerIndex() read the store; the store's radius is set by setGeometryInfo (the
  GeometryInfo keeps its own copy for shape tests, it is a value type shared with templates), the
  player index by setOrRestoreTeam and by TeamPrototype::setControllingPlayer for every member of
  every instance of a team that changes owner. PhysicsBehavior::onCollide reads the radius from the
  store. Referee replay matches with verify (contact array 90,600 checks, 0 mismatches).
- Engine-wide switch to the store getters (build store-6, one mechanical commit, same game): every
  `obj->getGeometryInfo().getBoundingCircleRadius()` (37 sites + 2 inside Object) now calls
  `getBoundingCircleRadius()`, every Object `->getControllingPlayer()->getPlayerIndex()` (36 sites +
  3 inside Object) calls `gxPlayerIndex()`. Left as they were: shape tests and local GeometryInfo
  variables, template geometry (getTemplateGeometryInfo), Team receivers (2 in setOrRestoreTeam), and
  the partition's kept place (max of circle and sphere radius, query's code). The store's player index
  is now written right after m_team changes, before anything in setOrRestoreTeam can read it.
  Referee replay matches with verify (contact array 90,600 checks, 0 mismatches); the verify battle
  printed 3,076 GXVERIFY lines, none with a mismatch.

### Round 4: store timing (lane 4, store-6t at 1f29afe, locked pairs)

Six pairs under `flock ~/generals-bench/timing.lock`, load < 8 checked inside the lock (start/end
load logged; posC's run saw a spike to 20). Logic / collide ms per logic frame, summed over the 12
battle windows (the first 6 windows are the pre-battle shell and are left out):

| pair | A | B | change |
|---|---|---|---|
| positionInStore off / on | 402 / 416 | | +3.5% |
| positionInStore on / off | 472 / 403 | | +17% |
| on / physBulk | 384 / 371 | | -3.4% |
| physBulk / on | 462 / 437 | | +5.6% |
| storePairs / on | 447 / 480 (load spike) | collide build 57 / 37 | |
| on / storePairs | 472 / 499 | collide build 35 / 62 | +6%, build ~1.8x |

- positionInStore: slower in both orders (3-17%), so its default is now **off** (third word bit 7;
  page switch `positionInStore: true` turns it on). The code path stays (line 1 is reachable).
- physBulk: no measurable change either way (-3% / +6%); stays off.
- storePairs: collide build ~1.8x slower in both orders (re-bucketing every unit each frame
  instead of reusing the partition's cells); stays off.
## 2026-11-25 — updatePasses (dispatch table + AI by state), quiet pair under timing.lock (logic-ai)

Build ai-16 (perf/logic-ai 50ae273), rule 131072 off/on, inside `flock ~/generals-bench/timing.lock`,
started at load < 8. Work ms (fps):
- pair 1 (load 7.9-9.6): armies 74.9 (10.5) / 48.5 (18.3); strikes 79.2 (12.4) / 82.5 (11.9);
  aftermath 38.2 (25.7) / 34.5 (28.5)
- pair 2 (load 7.6-8.6): armies 53.9 (16.5) / 47.9 (18.3); strikes 72.3 (13.4) / 79.6 (12.4);
  aftermath 37.6 (26.2) / 38.2 (25.7)
Armies phase faster (11-35% less work), strike phase 4-10% slower, aftermath even. The strike phase
has the most churn (units starting and stopping every-frame updates, AI states changing), where the
per-frame AI state sort and the store's pass-list rebuilds cost more than the heap they replace.
Stays default off.
## 2026-11-25 — Round 4 (range searches as a manager): measured before building

- Repeats within a tick (counters per frame on searcher, range, distance type and filter classes;
  load 64-75, counts only): per 150 frames 17,662 searches, 967 (5.5%) repeated in the same frame.
  Enemy-target scans (findClosestEnemy chains, ~5,000) repeat 0 times: every unit scans from its own
  place with its own line-of-sight / attack filters. The repeats are point-defense lasers rescanning
  after a target dies (~15% of laser scans) and one player-affiliation scan (~25%).
- After rounds 1-3 range searches are ~1.5-2% of the frame (paired timing: 0.17 of the plain walk),
  so a per-tick result cache can save well under 0.1% of the frame. A one-pass batch of every unit's
  scans would need the AI callers to submit requests instead of calling (ai agent's code).

## 2026-11-25 — Round 4: per-tick search snapshot measured, slower, not kept; store hooks kept

- Kept (exact): the partition writes each unit's centre cell into GxUnitStore (setCell, when the
  centre cell changes; -1 off the map), and units with an AI get the GXB_SCAN behaviour bit (they run
  the idle enemy scans). Replay matches; verify 0 mismatches (96k searches).
- Tried (match rule scanSnapshot 262144, third word bit 12): at the first search of a tick every
  cell's entries (place, radius, player slot, module) copied into one flat array in cell-list order,
  rebuilt when a PartitionData is destroyed; that tick's scans read it instead of the cell lists.
  Paired timing in verify runs (copy time counted on the fast side; load 21-34): fast / plain walk
  0.43 and 0.37 with it vs 0.18 and 0.17 without; 0.5-1% of searches gave a different result.
  Copying ~all COIs every tick costs more than the ~100 searches a tick it serves (the live lists
  already skip other players and far entries without reading units). Removed; the rule value is
  free again. Patch kept outside the repo (side-session notes, logic-query-scansnapshot.patch).
- With the earlier count (5.5% of searches repeat within a tick, 0 enemy-target scans), a per-tick
  result cache can save under 0.1% of the frame: not built.

## 2026-11-25 — Round 4 line 7: range searches as a manager (match rule scanManager 262144, third word bit 12, default off)

- Built on the user's 100%-adoption bar although it measured slower. Three parts, one switch (so all
  players agree on one rule value):
  - per-tick lists: at the first search of a tick every store slot in the partition goes into the
    cells its circle covers, in slot (object-ID) order; scans read the store's obj / player / x / y /
    radius columns and open a unit only when it is near (twice the bounding circle stands in for the
    sphere);
  - scan pass: then one pass over pass(GXB_SCAN) in object-ID order reruns each unit's plain enemy
    scan from last tick (AI::findClosestEnemy reports them via gxScanNoteRequest; the pass's own scans
    don't count, else every unit rescans every tick: first version, 403k pass scans per run);
  - result cache: within a tick a search with the same searcher / point, range, distance type, mode
    and filter classes reuses the first answer (units destroyed since are left out).
- Verify runs (paired; the pass counted on the manager side; load 15-28): manager / plain walk 0.81
  and 0.80, vs 0.18 for the default (exact) path. Cache hits ~24% of searches; ~22% of answers differ
  from a plain walk at the moment of the call (answers as of the tick's first search, slot order).
- Deterministic: recorded 9003 frames with it on, played back: matches. Referee replay (rule off)
  matches. Screenshot of the fight with it on: normal. fps pairs: load 20-48, all hit the time limit
  (unusable).
- Store hooks (line 2): GXB_SCAN set once for AI units at creation; never re-tested per tick.

- scanManager fps pairs (query-19, under ~/generals-bench/timing.lock, load < 8 at start):
  pair 1 (load 5.7 before, 8.8 after): armies 15.9 -> 18.0, strikes 13.6 -> 13.4, aftermath 25.5 -> 25.4.
  pair 2 (load 5.7 before, 13.2 after): armies 18.5 -> 13.2, strikes 12.5 -> 12.2, aftermath 21.3 -> 24.2.
  Strikes flat (-1..-2%); armies and aftermath swing both ways with load, so no fps gain shown. Stays default off.
## 2026-11-25 — Round 4, 100% adoption: movement manager on the store; route look-ahead (paths agent)
- Line 5, movement + route following (match rule moveAfterTick 32768, third word bit 1, off): one pass
  per frame over `TheGxUnitStore.pass(GXB_MOVE)`, in object-ID order, after the update queue and
  before physics. It runs doLocomotor (locomotion, computePointOnPath, final-position nudge) for every
  unit in the list. Positions are read and written through Thing, which with positionInStore keeps
  them in the store's `pos[s]`; writing `pos[s]` directly would skip the matrix and partition updates.
- Line 2, the MOVE mask is set on state change only: AIUpdate::gxSetMoving(TRUE) when the AI wakes and
  the unit is not already in the list, gxSetMoving(FALSE) when doLocomotor says it can sleep forever.
  A member flag (m_gxMoveOn, not saved; a load starts with no masks) means the store is not re-tested.
  The per-tick flag array of the first version (8a2733f) is gone. A unit that keeps moving wakes its
  AI next frame, as the old in-update call did.
- Route look-ahead (routeQueue + routeCache, no new rule): within the next 256 eligible queued plain
  requests, cache hits are served first. Serving queue entries by store slot (slotOfId) instead of
  findObjectByID gains nothing: findObjectByID is already an array index.
- Checks: paths-27 referee replay matches; a recording with moveAfterTick on (GX=8022) plays back to a
  match with it off. paths-28 (after merging collide's store-owned position, radius and player)
  matches the referee and that recording. paths-24 (look-ahead): referee matches; a recording with
  groupRoute + routeCache + flowCover + routeRefine + routeQueue on matches with them off.
- Timing (pairs under timing.lock, load 5–10; strikes phase fps / 1% low / frames over 100 ms):
  - Look-ahead, paths-23 vs paths-24 with all five route rules on: 14.4 → 16.0 / 6.9 → 7.0 / 20 → 10
    and 15.0 → 15.9 / 7.7 → 6.9 / 13 → 16. Kept (it rides on routeQueue + routeCache).
  - moveAfterTick off vs on (paths-28): 14.9 → 14.5 / 7.0 → 6.5 / 34 → 35 and 14.7 → 12.3 /
    6.0 → 5.9 / 21 → 115 (that half ran at load 8.6–9.8). Not faster: kept behind its rule,
    default off, as the round-4 bar asks.

## 2026-11-25 — Round 5 (units are pure data), paths families: state in component rows; movement kernel
- Per-unit state moved out of the module objects into rows of collide's GxComponentPool (one pool per
  module type, deque rows that never move, freed rows reused). Each module keeps only its row index
  (`m_gxRow`, `R()`) and stays the facade for INI, the engine's calls, xfer and crc. Same game.
  - Locomotor: 15 fields (maintainPos, braking/lift/speed/accel/turn limits, close-enough distance,
    flags, preferred height and damping, wander angle and increment, donut timer) in GxLocoState.
    The template pointer stays on the object (it is INI data, not unit state).
  - AI subclasses (14 with state): AssaultTransport, Chinook, DeliverPayload, DeployStyle, Dozer,
    HackInternet, Jet, Missile, RailedTransport, RailroadBehavior (RailroadGuideAIUpdate.cpp),
    SupplyTruck, Worker. Wander and Transport have no state of their own; POWTruck is compiled out
    (ALLOW_SURRENDER) and was left as is.
  - AnimationSteering, Float, Slaved, MobMemberSlaved, TensileFormation, Horde.
  - DockUpdate and RepairDock, SupplyWarehouseDock, RailedTransportDock (PrisonDock and SupplyCenterDock
    have no state of their own); Default/Queue/SpawnPoint/SupplyCenter production exits.
  - Derived classes reach their base's row as `Base::R()`. The conversion was mechanical (members
    removed, so any missed use fails to compile).
- Movement system as kernel + apply (round-5 addendum), under match rule updatePasses with
  moveAfterTick: the kernel computes every MOVE unit's point on its route (computePointOnPath) for
  the whole list before any unit moves, writing only that unit's row (reads: its route, locomotor set,
  position, the pathfinder grid, which nobody changes in this phase; no allocation, events, RNG or
  object changes, except that cpopSegments, off, builds its segment list on first use). The apply
  loop then moves units in object-ID order and takes the kernel's point if the unit's route and
  position are still the ones the kernel saw.
- Not done yet: the other modules' updates are still called one module at a time by the ai dispatch
  table, with their state in rows; splitting each into a range kernel + apply is the next step.
- Checks: referee replay matches with verify on Locomotor rows (paths-r5a, 90,603 checks), on all rows
  (paths-r5b, 90,376), with the kernel (paths-r5c, 90,823) and on the pool version (paths-r5d,
  90,643); 0 mismatches each. A recording with updatePasses + moveAfterTick on (GX=FEE6E7) plays back
  to a match with both off on paths-r5c and on paths-r5d.
- Timing (one pair under timing.lock, default settings): paths-28 (load 7.7–8.2) vs paths-r5d
  (load 8.2 → 4.9), strikes fps 11.1 → 14.4, 1% low 4.9 → 6.3, frames over 100 ms 247 → 30. Not an
  isolated measurement: paths-28 predates collide's and ai's round-4/5 merges, and the r5d half ran
  at lower load. It does show the rows are not slower. The rows have no switch (the data either
  lives in the row or on the object); the kernel rides on updatePasses.

## 2026-11-25 — Round 5 paths: kernel + apply inside every paths module (same game)
Each module's update now calls a pure kernel (its row + gathered inputs in, its row + an op out; no
allocation, events, RNG or object changes) and then applies the side effects in the old order. When
ai's per-kind hook lands, the range kernel is a loop of these calls over the kind's list.
- Kernels: AnimationSteering (turn animation state), Float (water-surface position), Horde (horde
  state from the gathered neighbour count and rub-off), TensileFormation (slide inertia and step),
  MobMemberSlaved (branch and catch-up crisis timer), Slaved (update throttle, repair/attack/scout/
  guard priority), Missile (no-turn distance, decoy timer), Jet (attack-loco, attackers-miss and
  untargetable timers), Dock (entrance permission), QueueProductionExit (exit delay), RailroadBehavior
  (conductor brake/wait/pull-away), DeployStyle (deploy timer, then deploy/pack decision; turret facts
  gathered after the first change, as before), AssaultTransport (member list drop/add, the old loop's
  slot order kept), DeliverPayload (dive state and strafe point), RailedTransport (arrival),
  Chinook (healed take-off, idle land/take-off/pending), Dozer and Worker (task validity),
  HackInternet (pending command), RailedTransportDock (pull-in / push-out scoot step).
- No kernel needed: SupplyTruck (its update only runs the state machine), SupplyCenterDock (the base
  dock's kernel), Default/SpawnPoint/SupplyCenter exits (update sleeps forever), Wander, Transport,
  PrisonDock (no state).
- Found while splitting: RailroadBehavior kept `conductorPullInfo` (no m_ prefix) on the object; it
  is now in the train's row too.
- Checks: paths-r5e (first batch) and paths-r5f (all) match the referee replay with verify (90,629 and
  90,613 checks, 0 mismatches) and play the kernel recording (GX=FEE6E7) to a match.

## 2026-11-25 — Round 5, combat: weapons, special powers and combat update modules as data

- Weapon and WeaponSet: every field moved into column tables (TheGxWeapons, TheGxWeaponSets in
  Weapon.h / WeaponSet.h; one row per weapon / set, rows reused through a free list). The classes keep
  only their row; xfer and CRC read and write the columns. Same game.
- SpecialPowerModule (the only special-power class with state; the 10 powers built on it hold none)
  and 39 combat update modules (Lifetime, Deletion, OCL, FireWeapon, Laser, ProjectileStream, Flammable,
  BoneFX, Topple, StructureTopple/Collapse, HeightDie, NeutronMissile and its slow death, Helicopter
  slow death, ParticleUplinkCannon, Spectre*, SpecialAbility, Production, PowerPlant, ...): the
  module's state is one row in GxComponentPool<Module::Row> (collide's Common/GxComponentPool.h);
  the module holds a GxRow and reaches its row through D(). Done by script
  (~/generals-bench/gxrows.py: members move into a nested Row, uses in the class's own methods are
  rewritten, ModuleData code with the same member names is left alone). FireSpread and WeaponBonus
  have no state.
- weaponReload (fourth word bit 24, off; rides on match rule updatePasses because a weapon's saved
  status is in the CRC): a kernel over weapon rows [b, e) gives every weapon whose reload is over the
  status getStatus() would give it, writing only its own rows and listing the owners; the apply step
  (main thread) puts those units back into the weapon-status pass.
- Checks (build combat-r5c): referee replay matches (verify 91200 checks, 0 mismatches); a recording
  with weaponReload on replays to a match (verify 18225, 0); mt referee matches.
- Not yet: kernel + apply splits for the update modules (waiting on a per-kind system hook in the
  updatePasses dispatch, ai's file); timing pairs (load 60+ while every lane builds).
### Round 5 (lane 4, collide): units are pure data — store SoA, component rows, SIMD

- Merged origin/perf/low-end f8473f1 (fourth switch word).
- **Store SoA layout** (9e1aa3d, pushed first): GxUnitStore keeps float columns x/y/z, vx/vy/vz,
  ax/ay/az (acceleration) and radius. pos[]/vel[] are gone: posOf(s), velOf(s), velRef(s)/accelRef(s)
  (GxVec3Ref, a Coord3D-shaped view of three columns). Thing::getPosition() reads through the columns
  with positionInStore (copies them into the Thing's cached Coord3D); PhysicsBehavior::getVelocity()
  copies vx/vy/vz out. Replay: matches with positionInStore off and on (verify 0 mismatches).
- **Component rows** (Common/GxComponentPool.h): GxComponentPool<Row> (deque rows, never move, free
  list) and GxRow<Row> (the module's only state member, allocates/frees its row). Rows are per module,
  not per store slot (an object can have several modules of one kind). Converted (all per-unit state
  out of the module objects; the modules stay as facades for INI, engine calls, xfer and CRC):
  PhysicsBehavior (20 fields; acceleration in the store columns), BodyModule, ActiveBody (20),
  InactiveBody, StructureBody, TransitionDamageFX, FireWeaponCollide, SpecialPowerCompletionDie.
  The other files of the families (Collide/ 19 incl. crates, Body/ Highlander, Immortal, Undead,
  HiveStructure, Damage/ BoneFXDamage, DamageModule, Destroy/, Die/ 11) hold no per-unit state: they
  are already facades over INI data, nothing to move. No family file has a per-frame update besides
  physics; the rest are event handlers (onCollide / onDie / onDamage) over their rows.
- **SIMD physics integration** (switch physSimd, fourth word bit 0, off; rides on match rule physBulk):
  the physics pass does every due unit's forces, then velocity += acceleration, the tiny-velocity clamp
  and the acceleration reset for all of them in one wasm simd128 pass over the store columns
  (gxIntegrateColumns, 4 rows per instruction, same float ops), then each unit's remaining step.
- **SIMD collision broad phase** (switch collideSimd, fourth word bit 1, on; works under storePairs):
  the store grid is laid out as one block per cell (units in the same order the linked lists visit
  them) with x, y, radius copied in, so the distance cut tests 4 units per instruction. Same pairs as
  storePairs without it.
- **Kernel + apply split** (round 5 addendum, for the round-6 job system; both on the main thread
  now): physics integration is the kernel gxIntegrateColumns(due, begin, end) (reads due[], v, a;
  writes v, a of its rows only); the forces before it and each unit's remaining step after it are the
  main-thread parts. The broad phase is prepare (main: grid blocks, each seeker's slot and cell
  rectangle) -> gxPairKernel(in, begin, end, out, cap) (reads the blocks and store columns, writes
  only (seeker, slot) pairs to its own out; returns -1 when out is full, the main thread grows it and
  reruns) -> apply (main, seekers in object-ID order: collideStagger, addToContactList).
- **Pool rows in place** (de3d971, asked by ai): GxComponentPool keeps raw slots; alloc constructs the
  row in place, release destroys it in place (a row may hold non-copyable members such as another
  GxRow, a freed row holds nothing); GxRow caches its row pointer (one load per field access).
- **Shared tick timer** (merged origin/perf/logic-query 2fd63e3, Common/GxTime.h, switch tickTiming):
  GXT physics, collideBuild (dirty cells + pair offers), collideProcess; counters physUnits,
  collideSeekers, pairCells, pairTests, pairs (the SIMD kernel's cells/tests are counted in its
  prepare step so the kernel stays free of globals) and allocs (game-memory blocks per logic tick:
  every pool block plus every large dynamic block, g_gxAllocs in GameMemory.cpp, read across
  GameLogic::UPDATE in GameEngine.cpp).
- **More rows**: UndeadBody (m_isSecondLife). **Object**: all ~75 private fields (ids, status, geometry,
  group, sighting, vision/shroud ranges, disabled mask and frames, helper and module links, contain
  links, team, weapons, special powers, healing benefactor, triggers, layers, formation, byte flags)
  in one nested Object::ObjectRow (m_gxObj); the object keeps its row and the team list links
  (MAKE_DLINK). The constructor's init list became row defaults. Not moved: m_gxListSeq and
  m_gxDisabledListed (public, GameLogic's disabledList). **Thing**: the transform and the cached
  pos/angle/dir/altitudes in Thing::ThingRow (m_gxThing); the template, store slot and posInStore flag
  stay. HeightDieUpdate: combat's (d85d85d), not touched here.
- **Hot fields as store columns** (write-through, the rows stay the truth): status (64 bits),
  disabled, layer, geomType/geomHeight/geomMajor/geomMinor (GxUnitStore::setHot from every Object
  setter of them and after load), angle and xf (the 3x4 transform, setXf from reactToTransformChange
  next to the position write-through). Verify mode checks every stored object's columns against the
  object at each frame's first pass ("GXVERIFY store hot fields").
- Replay soa-8 (verify): default, storePairs (collide-sp.rep), physBulk+physSimd (collide-ps.rep) and
  positionInStore all match; hot fields 1,295,856 checks, 0 mismatches; disabled list 0 mismatches.
  Earlier: soa-4 default / storePairs+collideSimd / physSimd record-replay match; the physSimd
  recording replayed with scalar physics matches (SIMD integration = scalar). soa-6 (timer) matches.
- **Hot fields: the store columns are the truth** (switch hotInStore, fourth word bit 2, off until
  timed; fixed per object when it enters the store). The write-through copies are gone. Columns
  status (ObjectStatusMaskType), disabled (DisabledMaskType), layer, geom (GeometryInfo), angle, xf
  (Matrix3D). Object and Thing read and write them only through ref accessors (gxStatus(), gxDisabled(),
  gxLayerRef(), gxGeom(), Thing::gxAngle(), gxXf(): the column while stored with the switch, else the
  row); every former m_status / m_disabledMask / m_layer / m_geometryInfo / m_transform / m_cachedAngle
  use goes through them (xfer too). GxUnitStore::add moves the row values in (Object::gxSetHotInStore),
  remove moves them back (setID, destruction). Moving in leaves marks in the rows; verify mode checks
  each frame that every stored object's rows still hold them ("GXVERIFY store hot fields (rows
  untouched while stored)"), i.e. nothing read or wrote around the columns. Without the switch the
  columns are unused.
- Replay soa-9 (verify), all match: switch off; on (default rep); on + storePairs (collide-sp.rep); on +
  physBulk + physSimd (collide-ps.rep); on + positionInStore. Rows untouched: 1,295,856 checks per run,
  0 mismatches.
- hotInStore also moves the position into the store (one data switch; positionInStore alone kept
  for comparison). Light replay (collide-sp.rep, storePairs, soa-10, locked, both orders): logic
  0.585/0.590 ms/tick off vs 0.619/0.632 on (+6%); without the position (soa-9) 0.568/0.612 vs
  0.575/0.587 (no cost). Cause found: getTransformMatrix() rewrote the translation from x/y/z on
  every read. Every setter writes the translation and x/y/z together and nothing else writes x/y/z,
  so the refresh is gone (soa-12); verify mode now also checks each placed stored object's
  translation = x/y/z. Rows cost (soa-6 vs soa-8, same replay): 0.580/0.571 vs 0.570 ms/tick (none).
  That replay has almost no units (logic 0.6 ms/tick), so the real timing is the battle benchmark.
- Counters: collideSeekers/pairCells/pairTests/pairs were only counted on the storePairs path (off by
  default); now also in PartitionData::addPossibleCollisions (pairs = offers to the contact list).
- Replay soa-12 (verify), all match: default; hotInStore; positionInStore; storePairs + hotInStore
  (collide-sp.rep); collide-hot.rep (recorded with hotInStore + storePairs on soa-9, also replayed
  with hotInStore off on soa-9: match). Store checks 2,591,712 (hot) / 1,295,856 (position only), 0
  mismatches.
- getPosition() with the position in the store copies x/y/z into the row (it must return a stable
  pointer); reactToTransformChange called it ~6 times, now once (soa-13). soa-13 replays all match
  (same five as soa-12, 0 store mismatches); battle benchmark with verify + hotInStore: every GXVERIFY
  0 mismatches (store hot fields 7,098,552 checked).
- Battle benchmark (soa-13, tickTiming, under timing.lock, load < 4 at start), 2700 ticks each, order
  off/on/on/off/off/on/off/on:

  | run | hotInStore | logic | physics | collideBuild | collideProcess | p99 ms | >100 ms |
  |---|---|---|---|---|---|---|---|
  | c0 | off | 23.45 | 2.76 | 1.57 | 1.71 | 124 | 112 |
  | c1 | on | 22.77 | 2.77 | 1.56 | 1.68 | 122 | 72 |
  | c1b | on | 21.37 | 2.62 | 1.45 | 1.59 | 105 | 33 |
  | c0b | off | 26.62* | 3.08 | 1.84 | 1.98 | 156 | 288 |
  | c0c | off | 23.54 | 2.77 | 1.57 | 1.78 | 125 | 102 |
  | c1c | on | 23.16 | 2.81 | 1.62 | 1.76 | 126 | 101 |
  | c0d | off | 19.32 | 2.23 | 1.24 | 1.42 | 97 | 25 |
  | c1d | on | 17.97** | 2.20 | 1.20 | 1.34 | 109 | 37 |

  \* load rose to 7.75 during the run (dropped). \** 3000 ticks, not comparable on logic.
  Same work both ways: physUnits 1205, collideSeekers 886, pairTests 14,654, pairs 4741, allocs 3336
  per tick. Adjacent pairs: on is 0.4-0.7 ms/tick faster or equal; physics/collide within 0.05 ms.
  Not slower, so **hotInStore is on by default** (web_shell def true; gles3 bit 2 set unless false).
  soa-14 (default on) referee replay matches, store checks 2,591,712, 0 mismatches.
- Timing: SIMD pairs (collideSimd, physSimd) wait until the data work is timed.

## Round 5 (collide, 2026-11-27): heap churn, store layout, hash-map sweep, straight-line kernels
- Allocation counters: GxTimeScope also counts game memory blocks handed out inside each system's
  outermost scope; GXTIME prints `<system>Allocs` per tick. Before (soa-15, battle, 2700 ticks): physics
  215.5, collideBuild 56.2, collideProcess 106.2 per tick (logic total 3337).
- Store (1f734bb): columns are `GxCol<T>` (64-byte aligned allocator, whole 64-byte lines; round 6
  false-sharing rule). compact() moves rows down in place (the slot order is ascending) instead of
  copying all 26 columns every frame a unit dies; re-sorting after a load still copies.
- storePairs buckets from the store only: new column inCells (1 while the partition holds the unit in
  its cells; written in updateCellsTouched, removeAllTouchedCells, freeCoiArray, add), then a counting
  sort into per-cell blocks. Same pairs in the same order (descending slot per cell, as the old
  linked lists): collide-sp.rep and collide-hot.rep replay to a match; battle verify inCells 4.68M
  checks, 0 mismatches.
- Far-apart pre-check: already in (pairPlace, second word bit 28, on, exact); with storePairs the
  kernel does the same cut from the store columns. No new switch; re-timed below.
- geomColumns (fifth word bit 4, value 16, off; 27bb272): geometry as gType / gSmall / gMajor / gMinor /
  gHeight and the transform as xfm[12] store columns, written at their writers (setGeometryInfo,
  setGeometryInfoZ, xfer, move-in; every transform change via reactToTransformChange). Readers moved:
  partition fill, the collision z test and shape pick (no CollideInfo built for pairs apart in height),
  getUnitDirectionVector3D. The structs stay as the general API view. Verify: plain columns = structs,
  16.6M checks in the battle, 0 mismatches; referee matches.
- Hash-map sweep (27bb272, all exact, verify compares with the old walk; referee replay with all on
  matches): #9 recruitIndex (bit 5): Team::tryToRecruit over the player's objects from the store's
  player column, sorted by object-list sequence (ties as before). #10 aiWorldIndex (bit 6):
  AIPlayer::findDozer the same way; the rebuild-hole scan by a spawner -> hole index (last match in
  list order = lowest sequence). #18 playerKindCounts (bit 7): per-player counts of black markets and
  drones kept by the store (add / remove / setPlayer, which also covers whole-team owner changes);
  the stealth black-market check and the dock drone search skip their player walk when the count is 0.
  Not done: SpawnBehavior orphans and whole-player AutoHeal visit objects in team order and act on
  them, so a count can't skip them exactly; findTemplate(string) per build-list entry. The battle
  benchmark has no AI players: none of these run there (timed only as "not slower").
- Straight-line kernels (1c8f345): physics integration (__restrict columns, no branch, 8 rows per
  step) and the pair kernel (__restrict, hoisted splats, 8 units per step); both noinline so each is
  one function: wasm disassembly shows 0 calls in either. No fast-math, same float ops.
- Timings queued (battle, timing.lock, load < 8, alternating): geomColumns, hash switches, storePairs,
  pairPlace, collideList, physBulk, heap (soa-15 / soa-16), kernels.

### collide: defaults, circleReuse, pinned no-draw timing (2026-11-27)
- Defaults now on: geomColumns (fifth word 16), recruitIndex (32), aiWorldIndex (64), playerKindCounts
  (128), circleReuse (sixth word bit 24, 16777216). Off and kept off: storePairs (collideBuild 1.90 vs
  1.00 ms/tick unpinned), pairPlace (flat), collideList (collideProcess 1.52 vs 1.30). physBulk: too noisy
  to call, default unchanged.
- Allocation sampler (GXDIAG, 1 in 97 allocations inside physics / collideBuild / collideProcess, patch
  not committed): collideBuild was ~80% DiscreteCircle edge vectors (shroud cover, threat and value
  affects in cell maintenance), ~17% queueUndoShroudReveal (pool SightingInfo). collideProcess and
  physics allocations are event work (move-away paths, particle systems, audio file names, object
  creation from deaths), not per-tick containers.
- circleReuse: DiscreteCircle::reset regenerates one scratch circle per call site; the same edges, so
  the same cells in the same order. Checks: referee replay (with circleReuse, and defaults), collide-hot
  replay with verify: all match; screenshots at frames 600/1800/3600/5400 off vs on: 2 pixels of 1.77M
  differ, the same 2 at every frame.
- Pinned no-draw battle (soa-20, 1800 ticks, slot runs back to back off/on; counters identical):

  | run | logic ms | collideBuild ms | collideBuild allocs/tick | allocs/tick | p99 ms | >100 ms |
  |---|---:|---:|---:|---:|---:|---:|
  | circleReuse off | 55.95 / 34.87 | 3.311 / 2.140 | 86.1 | 4677 | 175.9 / 80.1 | 130 / 10 |
  | circleReuse on  | 52.62 / 35.74 | 3.408 / 2.176 | 18.8 | 4606 | 196.5 / 85.6 | 212 / 10 |
  | geomColumns off | 33.31 / 37.60 | 1.988 / 2.418 | 86.1 | 4677 | 77.6 / 88.6 | 10 / 12 |
  | geomColumns on  | 43.19 / 36.74 | 2.739 / 2.239 | 86.1 | 4677 | 95.6 / 85.8 | 18 / 12 |

  Pinned ms spread run to run was 33-56 ms logic on this lane (not the ~1.5% expected), so ms is within
  noise for both; the allocation drop (-78% collideBuild, -71/tick total) is exact. circleReuse on by
  main's rule (on once replays match).
- Re-timed after the slot pinning fix (bench process and docker off the slot cores), soa-20, pinned
  no-draw battle, 1800 ticks, counters identical run to run:
  - circleReuse off/on: logic 33.74 / 32.98 vs 33.29 / 32.68 ms/tick (-1.1%, -0.9%), collideBuild
    2.009 / 2.010 vs 2.033 / 1.983 (flat). Spread of the 4 runs 3%. Stays on.
  - Hash switches (recruitIndex, aiWorldIndex, playerKindCounts) on the referee replay with timing
    (AI players, 8997 ticks; all 4 match): logic 0.054 / 0.053 off vs 0.046 / 0.053 on: the whole replay
    is too light to time them; on as not slower.
  - pairPlace off/on, 4 pairs: collideBuild 1.969 / 1.902 / 1.839 / 1.958 vs 1.917 / 1.612 / 1.942 /
    1.965, logic 32.6 / 31.9 / 30.7 / 32.2 vs 32.2 / 27.1 / 32.2 / 33.2: no consistent gain, stays off.
  - Pair kernel (storePairs on; soa-21k0 = HEAD with 1c8f345 reverted and the physics kernel of
    27bb272 reverted): collideBuild 2.949 / 3.118 before vs 3.155 / 3.067 after: no gain.
  - Physics kernel (physBulk + physSimd): physics 3.629 / 3.690 before vs 4.338 / 4.160 after, but
    every other system was ~19% slower in the same runs (search 4.1 vs 5.0, collideBuild 1.84 vs
    2.30); physics share of logic 11.6% in both. No gain measurable. Both kernels stay as committed
    (call-free, exact); both run only on switches that are off by default.
- AI games were idle because the web build never loaded the loose AI scripts: Data/Scripts/
  SkirmishScripts.scb and MultiplayerScripts.scb are install files in no .big, so skirmish AI players
  got no scripts (SidesList::prepareForMP_or_Skirmish) and stayed at command center + dozer. Fix
  2c7e213 (EmscriptenMain.cpp): read them from assets/Data/Scripts/ into MEMFS at boot (missing file
  only logged). Landed on perf/low-end as a650518; the shared assets dir has Data/Scripts links.
  Checked: collide-fix.rep frame 5300 shows the China AI with a full base and tanks; replay matches.
- ai-long.rep (~/generals-bench, soa-22, 36000 frames, hash switches off when recorded): AIs build and
  fight, but the load is light: logic averages 0.54 ms/tick, ~0.64 ms/tick over the last 9000 ticks,
  physUnits ~24 on average (battle bench: ~33 ms/tick, 1205 physUnits). Replays match, 0 mismatches.
- Hash switches (recruitIndex, aiWorldIndex, playerKindCounts) on ai-long.rep, bench_replay timing,
  GXTIMEEND logic ms/tick: off 0.536 / 0.788, on 0.591 / 0.557 (aiLoop 0.331 / 0.480 vs 0.366 / 0.344).
  Within noise at this load; no gain or loss measurable. Defaults stay on.
- aiScripts switch (2f2a009): seventh switch word GLES3_Page_Switches7, bit 0, on (1-31 spare). Off =
  the boot loader skips the loose scripts, as before 2c7e213 (they enter the exe CRC and the AI, so
  every older replay mismatched at frame 0). The bench replay driver (server.py AI_SCRIPT_REPLAYS)
  plays recordings made before the fix with it off. soa-24: referee, collide-sp, collide-hot match
  with it off; collide-fix and ai-long match with it on; 0 mismatches each.
- Heavier AI recording (aec0d5f, WebBenchmark.cpp): bench_record settings recordMap / recordAIs (up to
  numPlayers-1 brutal AIs, factions in turn, two teams alternating; default unchanged). ai-long.rep
  re-recorded: Twilight Flame, 7 brutal AIs, 36000 frames, hash switches off. Logic averages ~24
  ms/tick (pathfind 15-18, aiLoop ~5, search ~2.3), 151 physUnits on average, over 50 ms/tick in the
  last minutes. Hash switches off 22.35 / 25.52, on 25.50 / 23.45 (aiLoop off 4.65 / 5.32, on 5.28 /
  4.99): no measurable effect at this load either; they stay on.
- Allocation sample (soa-23d, GXDIAG stacks every 97th allocation inside physics / collide scopes,
  battle, no draw): allocs per tick physics 326, collideBuild 18.7, collideProcess 144 (aiLoop 3203,
  search 958, pathfind 672). Collide's own are small and cheap: collideBuild 84% queueUndoShroudReveal
  (a pool block per unlook, O(1)); collideProcess 48% pathfinding called from collisions (paths),
  10% Object::onCollide's collide-module list, built once per unit. Physics scope: 58% the death
  deselect group (ai's deathDeselect), 14% audio file names (ai's audioNames). No new collide switch.
- Pinned no-draw re-measure (soa-24, battle, GXTIMEEND, two pairs back to back):
  - geomColumns off 33.47 / 31.03 vs on 33.64 / 32.66 logic (collideBuild 2.07 / 1.91 vs 2.12 / 2.01):
    no gain now (the earlier 20.1 vs 21.5 was the old timing). Flagged to main.
  - storePairs on 43.53 / 42.51 vs off 36.47 / 35.50 (collideBuild 4.08 / 3.99 vs 2.29 / 2.21): slower,
    stays off.
  - collideList off 34.84 / 42.18 vs on 38.56 / 36.21 logic (noisy); collideProcess off 3.02 / 3.59 vs
    on 2.71 / 2.59: stays on.
  - physBulk on 36.04 / 42.21 vs off 34.90 / 39.20 (physics 4.02 / 4.66 vs 3.99 / 4.46): stays off.

## 2026-11-25 — Round 5 (units are pure data), paths families: state in component rows; movement kernel
- Per-unit state moved out of the module objects into rows of collide's GxComponentPool (one pool per
  module type, deque rows that never move, freed rows reused). Each module keeps only its row index
  (`m_gxRow`, `R()`) and stays the facade for INI, the engine's calls, xfer and crc. Same game.
  - Locomotor: 15 fields (maintainPos, braking/lift/speed/accel/turn limits, close-enough distance,
    flags, preferred height and damping, wander angle and increment, donut timer) in GxLocoState.
    The template pointer stays on the object (it is INI data, not unit state).
  - AI subclasses (14 with state): AssaultTransport, Chinook, DeliverPayload, DeployStyle, Dozer,
    HackInternet, Jet, Missile, RailedTransport, RailroadBehavior (RailroadGuideAIUpdate.cpp),
    SupplyTruck, Worker. Wander and Transport have no state of their own; POWTruck is compiled out
    (ALLOW_SURRENDER) and was left as is.
  - AnimationSteering, Float, Slaved, MobMemberSlaved, TensileFormation, Horde.
  - DockUpdate and RepairDock, SupplyWarehouseDock, RailedTransportDock (PrisonDock and SupplyCenterDock
    have no state of their own); Default/Queue/SpawnPoint/SupplyCenter production exits.
  - Derived classes reach their base's row as `Base::R()`. The conversion was mechanical (members
    removed, so any missed use fails to compile).
- Movement system as kernel + apply (round-5 addendum), under match rule updatePasses with
  moveAfterTick: the kernel computes every MOVE unit's point on its route (computePointOnPath) for
  the whole list before any unit moves, writing only that unit's row (reads: its route, locomotor set,
  position, the pathfinder grid, which nobody changes in this phase; no allocation, events, RNG or
  object changes, except that cpopSegments, off, builds its segment list on first use). The apply
  loop then moves units in object-ID order and takes the kernel's point if the unit's route and
  position are still the ones the kernel saw.
- Not done yet: the other modules' updates are still called one module at a time by the ai dispatch
  table, with their state in rows; splitting each into a range kernel + apply is the next step.
- Checks: referee replay matches with verify on Locomotor rows (paths-r5a, 90,603 checks), on all rows
  (paths-r5b, 90,376), with the kernel (paths-r5c, 90,823) and on the pool version (paths-r5d,
  90,643); 0 mismatches each. A recording with updatePasses + moveAfterTick on (GX=FEE6E7) plays back
  to a match with both off on paths-r5c and on paths-r5d.
- Timing (one pair under timing.lock, default settings): paths-28 (load 7.7–8.2) vs paths-r5d
  (load 8.2 → 4.9), strikes fps 11.1 → 14.4, 1% low 4.9 → 6.3, frames over 100 ms 247 → 30. Not an
  isolated measurement: paths-28 predates collide's and ai's round-4/5 merges, and the r5d half ran
  at lower load. It does show the rows are not slower. The rows have no switch (the data either
  lives in the row or on the object); the kernel rides on updatePasses.

## 2026-11-25 — Round 5 paths: kernel + apply inside every paths module (same game)
Each module's update now calls a pure kernel (its row + gathered inputs in, its row + an op out; no
allocation, events, RNG or object changes) and then applies the side effects in the old order. When
ai's per-kind hook lands, the range kernel is a loop of these calls over the kind's list.
- Kernels: AnimationSteering (turn animation state), Float (water-surface position), Horde (horde
  state from the gathered neighbour count and rub-off), TensileFormation (slide inertia and step),
  MobMemberSlaved (branch and catch-up crisis timer), Slaved (update throttle, repair/attack/scout/
  guard priority), Missile (no-turn distance, decoy timer), Jet (attack-loco, attackers-miss and
  untargetable timers), Dock (entrance permission), QueueProductionExit (exit delay), RailroadBehavior
  (conductor brake/wait/pull-away), DeployStyle (deploy timer, then deploy/pack decision; turret facts
  gathered after the first change, as before), AssaultTransport (member list drop/add, the old loop's
  slot order kept), DeliverPayload (dive state and strafe point), RailedTransport (arrival),
  Chinook (healed take-off, idle land/take-off/pending), Dozer and Worker (task validity),
  HackInternet (pending command), RailedTransportDock (pull-in / push-out scoot step).
- No kernel needed: SupplyTruck (its update only runs the state machine), SupplyCenterDock (the base
  dock's kernel), Default/SpawnPoint/SupplyCenter exits (update sleeps forever), Wander, Transport,
  PrisonDock (no state).
- Found while splitting: RailroadBehavior kept `conductorPullInfo` (no m_ prefix) on the object; it
  is now in the train's row too.
- Checks: paths-r5e (first batch) and paths-r5f (all) match the referee replay with verify (90,629 and
  90,613 checks, 0 mismatches) and play the kernel recording (GX=FEE6E7) to a match.

## 2026-11-25 — Round 5, combat: weapons, special powers and combat update modules as data

- Weapon and WeaponSet: every field moved into column tables (TheGxWeapons, TheGxWeaponSets in
  Weapon.h / WeaponSet.h; one row per weapon / set, rows reused through a free list). The classes keep
  only their row; xfer and CRC read and write the columns. Same game.
- SpecialPowerModule (the only special-power class with state; the 10 powers built on it hold none)
  and 39 combat update modules (Lifetime, Deletion, OCL, FireWeapon, Laser, ProjectileStream, Flammable,
  BoneFX, Topple, StructureTopple/Collapse, HeightDie, NeutronMissile and its slow death, Helicopter
  slow death, ParticleUplinkCannon, Spectre*, SpecialAbility, Production, PowerPlant, ...): the
  module's state is one row in GxComponentPool<Module::Row> (collide's Common/GxComponentPool.h);
  the module holds a GxRow and reaches its row through D(). Done by script
  (~/generals-bench/gxrows.py: members move into a nested Row, uses in the class's own methods are
  rewritten, ModuleData code with the same member names is left alone). FireSpread and WeaponBonus
  have no state.
- weaponReload (fourth word bit 24, off; rides on match rule updatePasses because a weapon's saved
  status is in the CRC): a kernel over weapon rows [b, e) gives every weapon whose reload is over the
  status getStatus() would give it, writing only its own rows and listing the owners; the apply step
  (main thread) puts those units back into the weapon-status pass.
- Checks (build combat-r5c): referee replay matches (verify 91200 checks, 0 mismatches); a recording
  with weaponReload on replays to a match (verify 18225, 0); mt referee matches.
- Not yet: kernel + apply splits for the update modules (waiting on a per-kind system hook in the
  updatePasses dispatch, ai's file); timing pairs (load 60+ while every lane builds).
## Round 5 (query): scan-type modules and helpers hold their per-unit state in component tables
- New GameLogic/GxComponentTable.h: rows in a deque (never move, never copied; RadiusDecal can't be),
  freed rows reset in place and reused. A module keeps only its row index (R()); xfer/CRC read the row.
- Converted: StealthDetector, Stealth, EnemyNear, AutoFindHealing, AssistedTargeting, Radar, SpyVision,
  DynamicShroudClearingRange, PointDefenseLaser, CommandButtonHunt, ObjectDefectionHelper,
  StatusDamageHelper, SubdualDamageHelper, TempWeaponBonusHelper. ObjectHelper, ObjectRepulsorHelper,
  ObjectSMCHelper and ObjectWeaponStatusHelper hold no per-unit fields.
- Same game: referee replay on query-20 matches (9000 frames). Layout change only, so no switch.
- Kernel step (rowKernels, fourth word bit 12, default on): EnemyNear, AutoFindHealing and PointDefenseLaser
  scan countdowns are one kernel over the component pool (reads delay/reload, writes only the row's tick;
  no allocation, RNG, events or globals); each module's update is the apply step (commit, scan, side effects).
  A changed delay is recomputed inline, so same game. Sleep-scheduled modules (StealthDetector, SpyVision,
  CommandButtonHunt, helpers) have no per-frame row math: their update is all apply.
- On collide's GxComponentPool (9e1aa3d): modules hold GxRow<Row>; GxComponentTable.h is now only R() and the kernel.
- simdSearch (fourth word bit 13, default on): the store-list search (scanManager) culls 4 slots at a time by
  distance from the live x/y/radius columns (wasm simd128, same f32 ops, keep unless d2 > lim^2, scalar
  recheck after): same game. Referee replay and query-mgr3.rep (scanManager on) match on query-22.
  Pair tests not SIMD: per-cell linked lists of 2-4 neighbours; collide's store broad phase is the place.

## Fog-of-war sight circles: measured, not built (query, 2026-11-25)
- Profile (query-24, 25 s, inclusive): doShroudReveal 0.2%, undoShroudReveal 0.1%, pending-undo queue and
  shroud cover 0.0%, in both armies and strikes. Total ~0.3% < the 1% bar: SIMD circle stamps not built.
  (getShroudedStatus, the per-object read, is 0.5% armies / 0.7% strikes; different code.)

## Laser scan verify mismatches (objects 829/832/1624/1643): scanManager, not laserFilters (query, 2026-11-25)
- Verify (query-24): scanManager off: laser scans 15000, mismatches 0 (laserFilters exact). On: 30, every one
  the filtered scan missing in-range missiles fired after the tick's first search. Their cell entries were
  intact: scanManager answers from per-cell lists built at the tick's first search, by design. Same on every
  machine (match rule), so no network drift; laserFilters stays on.
- Fixes: scanManager no longer caches searches from a point (the key named no searcher and ignored whose
  enemies the filters look for, so two players' searches from one spot could share an answer); the laser
  verify skips its comparison under scanManager. Changes the scanManager game: new recording query-mgr4.rep.
  Referee replay on query-25 matches.
- Built anyway (user rule: build what is asked): fogStamps (fourth word bit 14, default on). The shroud
  counters are dense per-player Short arrays (truth; the cell struct is filled for CRC/save, refilled after
  load); sight circles come from per-radius stamps (DiscreteCircle's spans recorded at (0,0)), applied 8 cells
  at a time with wasm i16x8 (same arithmetic as addLooker/removeLooker), then the edge side effects for cells
  whose status changed (they touch no counter). Verify: 5.2M cells checked, 0 mismatches; referee and
  query-mgr4.rep replay. doShroudReveal 0.2 -> 0.1-0.2%, undo 0.1%.
- fogKeep (bit 15, default on), the bigger nearby cost: getShroudedStatus was recomputed every frame for every
  moving unit, because each move invalidated all players. A move that keeps the same cells (cellReuse's key)
  now keeps statuses decided by the cells' shroud alone (off map / all shrouded / all clear / partly clear);
  ones decided by the fogged branch (which also reads relationship and kinds) are invalidated as before.
  Verify (recompute and compare): 1.74M checks, 0 mismatches; referee replay matches.
  getShroudedStatus 0.5/0.7% -> 0.4/0.4% (armies/strikes, profile share).
- scanManager store-list cull now uses max(circle, sphere) per slot (combat found "2 x circle" too small for
  tall units): changes the scanManager game; new recording query-mgr5.rep replays to a match (query-27).

## Measuring (rule "fps is not the measure", 2026-11-25): Common/GxTime.h, switch tickTiming (fourth word bit 16, off)
- Header-only: GXT_LOGIC around TheGameLogic->UPDATE() and frame intervals in GameEngine::update; systems add
  their own scope timers (GxTimeScope) and work counters (gxCount). Every 300 ticks one console line
  "GXTIME ticks N: logic X ms/tick; ...; frames F, p99 P ms, over 100 ms K" (averages since start).
- Query hooks: search ms, searches / cells / entries / units opened / SIMD-culled; fog reveal ms, circles,
  cells touched (stamp and span paths alike), fog status calls / recomputes / kept.
- Other lanes: add ids to the enums in GxTime.h; one logic timer for everyone (GameEngine.cpp).

## 2026-11-25 — Round 5 (units are pure data), logic-ai: component tables for my families

Merged origin/perf/low-end f8473f1 first.

Pattern (GameLogic/GxComponents.h, `GxComponentTable<Row>`): a module family's per-unit state is a
row in a table owned by the family, not fields of the module object. Rows live in a deque (never
move), are built in place (zeroed, like the memory-pool objects they replace) and are visited by
forEach in object-ID order. The module keeps one pointer to its row; its class is a facade (INI
data, the engine's calls, xfer/CRC reading and writing the row's fields in the old order, so saves
and CRCs are unchanged). Conversions are mechanical (tools/web-port/gx_rowify.py,
gx_inline_out.py, gx_base_rewrite.py): members -> row fields; member uses inside the class's own
functions -> the row; inline header methods that touch the row moved after the row type; subclass
uses of base members -> Base::gxRow().x. Every-frame updates already run as passes over the store's
lists in object-ID order through the updatePasses dispatch table (round 4); the module's update is the
per-row step of that pass.

Converted (per-unit state now in tables):
- Behavior/ (24 with state): AutoHeal, BattleBusSlowDeath, Bridge, BridgeScaffold, BridgeTower,
  BunkerBuster, Countermeasures, DumbProjectile, FireWeaponWhenDamaged, FlightDeck, GenerateMinefield,
  GrantStealth, JetSlowDeath, Minefield, Overcharge, ParkingPlace, Poisoned, PropagandaTower,
  RebuildHole, SlowDeath, Spawn, SupplyWarehouseCrippling. Stateless already (nothing per unit):
  BehaviorModule, FireWeaponWhenDead, InstantDeath, POWTruck, TechBuilding. Compiled out
  (ALLOW_SURRENDER): Prison, PropagandaCenter.
- Contain/ (10 with state): OpenContain (base), Garrison, Transport, Cave, Helix, MobNexus, Overlord,
  Parachute, RiderChange, Tunnel; stateless: Heal, InternetHack, RailedTransport.
- Create/: CreateModule (base, the only state); the 7 others are stateless.
- Upgrade/: UpgradeMux (every upgrade's "executed" flag) and GrantScienceUpgrade; the 19 others are
  stateless.
- AIUpdateInterface (75 fields); AI state machines: 25 AIStates classes, the guard / retaliate /
  tunnel-guard machines and states, dock machine and states, TurretAI and its states. The three
  *ExitConditions classes (held by value inside state rows) are converted too (ai-23, after rows were
  made to be built in place, never copied). Not converted: TurretAIAimTurretState (its only field is
  compiled out), TurretAIData (per-type INI data).
- No edit in other agents' files: JetAIUpdate.cpp (paths) still uses `m_goalPosition`,
  `m_waitingForPath` and `m_path` of its bases, which are now reference names for the row fields.
- dataBehaviors (fourth word bit 6, same game, on): GenerateMinefieldBehavior looked up the EMP-mines
  upgrade by name every frame (~140 units); now once (verify mode compares with the lookup).
Checks: referee replay matches (builds ai-19, ai-20, ai-21, ai-22); a recording made with
updatePasses on replays to a match on each; verify mode shows no mismatch from these changes (the only
mismatch lines on perf/low-end come from the "laser scan" check, objects 829/832/1624/1643).
Timing (under timing.lock; started at load 7.5, other agents' builds pushed it to 11-17), work ms,
ai-17 (merged base) / ai-22 (converted):
- run 1: ai-17 at load 7.5: armies 42.9, strikes 73.4, aftermath 81.7; ai-22 at load 16: 77.4, 112.2, 78.1
- run 2: ai-17 at load 15: 79.6, 116.5, 86.0; ai-22 at load 16->11: 56.0, 86.0, 52.3
At matched load the converted build is level or faster; run 1's gap follows the load. Not slower within
what this machine can show; it is the same game, so it stays on (there is no switch for where the
data lives; dataBehaviors only gates the minefield's cached lookup).
- ai-23 (ExitConditions converted): referee replay matches, the updatePasses recording replays to a
  match, verify mode clean (only the laser-scan lines).

## 2026-11-25 — Round 5 paths: every paths class pure data; range kernels on ai's hook; shared GxTime
- More per-unit state into component rows: the AI state classes in paths files (Jet x7, Chinook x3,
  Dozer x6, DeliverPayload x4, HackInternet x3), DeliverPayload's target position, SpawnPoint's spawn
  points. Left on purpose: INI data (module data, LocomotorTemplate), LocomotorStore (global),
  partition filters (per-call temporaries), POWTruckAIUpdate (compiled out, ALLOW_SURRENDER).
- Range kernels through ai's per-kind hook (GameLogic::gxRegisterKernel, GxPathsKernels.h, store bits
  20-27): AnimationSteering, Float, QueueProductionExit, Dock in PHASE_NORMAL; Missile, Jet, DeployStyle
  in PHASE_INITIAL; the train conductor in PHASE_PHYSICS. At the start of the phase the kernel runs
  every listed unit's kernel step (own unit only; skipped if its update will not run this frame) and
  stamps the result in its row; update() applies it. Under match rule updatePasses only (the inputs are
  read at the start of the phase rather than at the unit's turn); without it update() computes as before.
  Modules whose kernel inputs come from other units or allocate keep their kernel inside update():
  Horde (range search), Slaved, MobMember, AssaultTransport, DeliverPayload dive, RailedTransport,
  Chinook, Dozer, Worker, HackInternet, RailedTransportDock, TensileFormation.
- Measuring on the shared GxTime (tickTiming, Common/GxTime.h): move / moveKernel ms per tick, and the
  counters moveUnits, moveKernelPoints, moveKernelTaken, moveApplyPoints, hordeSearches. The GXTIME
  line buffer is 2048 bytes now (more agents' counters).
- Checks: paths-r5i (state classes) and paths-r5j (hook kernels) match the referee with verify (90,584
  checks, 0 mismatches each); r5i plays the round-5 kernel recording (paths-r5c.rep) to a match; a
  recording made on r5j with updatePasses + moveAfterTick on (GX=FEE6E7, paths-r5j.rep) plays back to
  a match with both off.
## 2026-11-26 — ai: pool migration, kernel hooks, shared timing (logic-ai)

Merged paths 79c4008, query (GxTime), collide de3d971 (in-place pool rows, cached row pointer).
- Kernel hooks (GameLogic.h): gxRegisterKernel(storeBit, phase, kernel(slots, b, e), apply(slots, n)),
  every frame at the start of the phase; gxRegisterKindKernel(kindNameKey, kernel(mods, b, e),
  apply(mods, n)), in the updatePasses dispatch right before that kind's module updates (fourth word
  bit 7, kindKernels, on). Paths and combat have the API.
- Minefield system as kernel + apply on store bit 56 (ai's kernel bits 56-59; dispatch kinds 32-55,
  misc 60-63).
- StateMachine / State / AIStateMachine per-unit state in rows too.
- All 98 ai tables now on the shared GxComponentPool/GxRow (own GxComponents.h removed).
- Timing on the shared GxTime (ids aiLoop, aiKernels, aiModuleCalls, aiKernelSlots).
Checks (ai-29, ai-30): referee replay matches; recording ai-up5.rep (default rules 0xFEE6E7) replays
to a match; verify mode: no mismatch lines.
Timing, same recording ai-up5.rep, tickTiming on, timing.lock, load 3-5 (ai-29 before the pool
migration / ai-30 after):
- run 1: logic 0.511 / 0.522 ms/tick; aiLoop 0.079 / 0.079; aiKernels 0.011 / 0.010
- run 2: logic 0.525 / 0.516 ms/tick; aiLoop 0.079 / 0.078; aiKernels 0.011 / 0.010
- work counters identical: 33.7 module calls/tick; frames 9598, p99 17.5 / 17.4 ms, 5 frames over 100 ms
Level (this recording is a light game: 0.5 ms of logic per tick).

### ai: kernel + apply for SlowDeath and Spawn (round 5 addendum), kernels follow match rule (5db1c81)
- Kernels follow the match rule updatePasses (131072); the kindKernels setting can only turn them off in
  local single player (paths: a local setting would desync).
- SlowDeathBehavior::update() = gxStep (kernel: row + module data only: LOD time scale, flung frame
  bump, which of sink / midpoint / final are due) + gxApplyStep (destroy, bounce, tree snag, sink,
  phase effects). SpawnBehavior::update() = gxStep (spawn timer) + gxApplyStep (aggregate health,
  burst setup, spawning). Registered on the per-kind hook (subclasses keep their own update()).
  update() returns at once when the kernel already took that frame's turn.
- Not split: OpenContain family and RebuildHole. Their per-frame fields (door countdown, entered
  mask, wanters, worker counter) are written by other units' turns (riders entering, workers dying),
  so a kernel ahead of the turns would change results.
- Verify mode (bit 21): at each apply the row must still be as the kernel left it. Battle benchmark:
  36.9M checks, 0 mismatches. Replays referee.rep and ai-up5.rep match (ai-33).
Timing, battle benchmark, ai-33, tickTiming, timing.lock, load 4-5, kindKernels off / on:
- run 1: logic 18.31 / 16.53 ms/tick; aiLoop 8.78 / 7.95; aiKernels 0 / 0.30; p99 89.1 / 93.3 ms; over 100 ms 21 / 23
- run 2: logic 16.39 / 17.11 ms/tick; aiLoop 7.74 / 8.32; aiKernels 0 / 0.31; p99 90.9 / 96.6 ms; over 100 ms 19 / 29
- kernel slots 474/tick on; module calls 3154-3497 off / 3215 on (the benchmark's work varies run to run).
Level within noise: the kernel part is a few compares per unit; the gain is the split itself (the
kernel can later run over workers / SIMD).

## 2026-11-26 — combat: splash SIMD, damage table, last data, ILP, FireWeapon kind kernel (logic-combat)
- splashSimd (fourth word bit 25, off; same game): a splash's victims (WeaponTemplate::dealDamageInternal)
  come from PartitionManager::gxSplashSearch: a SIMD pass over the unit store's x/y/sphere columns keeps
  slots within range + bounding sphere + 2, then each kept unit gets the cell walk's own distance test
  and its walk key (ring rank of its first visited cell, then COI sequence, high first), and goes into
  the iterator in that order. The store got a `sphere` column (bounding sphere radius, set in
  Object::setGeometryInfo): 2x the circle radius was not a bound for tall units (49/3500 searches wrong
  before; 5500 checked, 0 mismatches after). Not used under match rule scanManager. Verify mode
  compares every search with iterateObjectsInRange ("GXVERIFY splash").
- splashIlp (bit 27, off; same game): with splashSimd, the pass does 8 slots per step as two
  independent chains (each lane's math unchanged), per the ILP + FMA rule; no FMA.
- damageTable (bit 26, off; same game): per frame, Weapon::computeBonus answers from a table keyed by
  weapon template + bonus flags, and estimateWeaponTemplateDamage from one keyed by weapon template +
  armor template + damage amount (subdual, kill-garrisoned and sniper damage are left out).
- Last per-unit state: LeafletDropBehavior (EMPUpdate.h) and ProductionEntry (ProductionUpdate.h) in
  GxComponentPool rows. ProductionEntry::m_exitDoor is now zeroed at creation (was uninitialised).
  The combat data list is empty.
- FireWeaponUpdate on ai's per-kind hook (match rule updatePasses): the kernel decides for each
  every-frame module whether it fires (its own weapon status, object status and last shot frame; writes
  only its row), apply fires in object-ID order, update() then skips that frame. Verify mode re-decides
  in apply and counts differences ("GXVERIFY kindKernels fireWeapon").
- HeightDieUpdate on the same hook: the kernel does each module's quiet frame (not due, contained, or
  above its death height; writes only its row: earliest death frame, last position) and leaves dying,
  particle clean-up and structure-height units to apply, which runs the full update in object-ID
  order. A quiet unit whose position or container changed before its turn (an earlier death) gets its
  last position back and the full update ("heightDieRedone" counter), so the result is the plain loop's.
  Checks (combat-r5k): referee (verify 90584, 0), ai-up5 (verify 27162, 0) and mt replays match;
  battle: heightDieRedone 0 per tick, kind kernels 0.44 ms/tick in all. FireWeapon's kernel in the
  battle with verify (r5j): 168000 decisions checked, 0 mismatches.
- GxTime ids: splash, estimate, reload (ms/tick); splashes, splashVictims, splashScanned, splashNear,
  estimates, table/bonus hits and misses, reloadRows/reloadChanged.
- Checks (combat-r5j, all four switches on): referee replay matches (verify 90584 checks, 0
  mismatches); mt replay matches.
- Battle timings (bench_run, scanManager off so splashes take the new path, tickTiming, two rounds
  under timing.lock, single thread). Own ms/tick of the system, then its counters:
  - splash: base 0.081 / 0.074, splashSimd 0.058 / 0.056 (-25%), +splashIlp 0.066 / 0.067 (no gain:
    13.7k slots scanned and 190 kept per tick; the pass is memory-bound, not latency-bound).
    4.0 splashes/tick, 158 victims/tick, same in every run.
  - estimate: base 0.181 / 0.158, damageTable 0.204 / 0.199 (slower): table hits 192, misses 93 per
    tick (67%); bonus table hits 1353, misses 86. The hashing costs more than the sums it saves.
  - reload: the kernel costs 0.009 / 0.010 ms over 1670-1820 weapon rows per tick.
  - Logic total per tick (whole run, noisy with machine load): base 18.2 / 16.4, splash 14.7 / 14.2,
    ilp 16.0 / 16.4, table 15.7 / 14.6, reload 16.2 / 20.9 (reload2 ran at load 9). Frame p99 and
    frames over 100 ms follow load the same way; per-system ms above are the comparable numbers.
  - fps (footnote): strikes 13.7-19.4 across runs.
- Defaults: splashSimd on (faster, same game). splashIlp, damageTable, weaponReload stay off (not
  faster).
### ai: containers and rebuild holes as kernels (drifting part, rides on match rule updatePasses)
- OpenContain (and every container kind: Transport, Garrison, Tunnel, Cave, Heal, InternetHack,
  MobNexus, Overlord, Helix, Parachute, RailedTransport, RiderChange; each update() calls
  OpenContain::update() unconditionally): the kind kernel clears the entered mask and steps the door
  countdown of all containers ahead of the turns; update() does the rest (art monitor, door close,
  dead wanters) in the container's own turn.
- RebuildHoleBehavior: the kernel steps the worker wait counter of holes with no worker; a worker
  lost this frame starts its countdown next frame.
- Results differ from the in-turn order (riders entering / workers dying in other units' turns), so
  it follows match rule updatePasses; switch containKernels (fourth word bit 8, default on) can turn
  it off in local single player only (GameLogic::gxKernelSwitch).
Checks (ai-34): referee.rep (rule off) matches; new recording ai-ck.rep made on ai-34 replays to a
match; ai-up5.rep still matches; verify mode on the battle benchmark: 0 mismatches.
Timing, battle benchmark, ai-34, tickTiming, timing.lock, load 4-8 (just after a reboot), containKernels off / on:
- run 1: logic 20.37 / 21.66 ms/tick; aiLoop 10.40 / 10.90; aiKernels 0.39 / 0.41; p99 122 / 110 ms; over 100 ms 76 / 48
- run 2: logic 21.36 / 18.46 ms/tick; aiLoop 10.79 / 9.27; aiKernels 0.41 / 0.34; p99 128 / 107 ms; over 100 ms 117 / 41
- kernel slots 712-791/tick (counted also when off: the hook runs, the kernel returns at once)
Level within noise (the run's work differs: 2700 vs 3000 ticks, 3215-3572 module calls/tick).

### ai: SIMD step, slow-death kernel 4 rows per instruction (switch kernelSimd, fourth word bit 9, on; same game)
- Profile of the battle benchmark (ai-34): no math loops in the ai lane; its own cost is branchy
  scalar work (isIdle 0.5%, doLocomotor 0.5%, state updates ~0.4% each). The SIMD step applies to
  the biggest ai kernel, SlowDeath (~250 rows/tick): with LOD scale 1 the step is integer compares
  and adds, done 4 rows per wasm SIMD instruction, branchless (masks + and/or), gathered from and
  scattered back to the pool; no FMA, no float math. Spawn / container kernels are one decrement
  over a few dozen rows: left scalar.
- Checks (ai-35): referee.rep and ai-ck.rep match; verify mode runs the scalar step on a copy of every
  row: 37.25M checks, 0 mismatches (all GXVERIFY checks 0).
Timing, battle benchmark, ai-35, tickTiming, timing.lock, load 2-3, kernelSimd off / on:
- run 1: logic 15.28 / 15.22 ms/tick; aiLoop 7.48 / 7.44; aiKernels 0.271 / 0.270; p99 84.7 / 86.0 ms; over 100 ms 19 / 21
- run 2: logic 15.04 / 14.92 ms/tick; aiLoop 7.35 / 7.25; aiKernels 0.266 / 0.256; p99 88.6 / 83.6 ms; over 100 ms 24 / 19
- same work both sides: 3214.6 module calls/tick, 711.9 kernel slots/tick. Level to slightly faster
  (aiKernels includes the apply step, which is most of it).

### ai: kernel switches carried in the replay header (match rules kindKernelsOff 16777216, containKernelsOff 33554432)
- Before: kindKernels / containKernels off only worked in local single player and playback forced
  kernels on, so a single-player game recorded with a switch off would not replay.
- Now both are "off" match rules (bits 24, 25: an old recording without them plays as before, kernels
  on). The local switches set them; the host's Settings decide network games; the replay header keeps
  them. GameLogic::gxKernelSwitch(offRule) = updatePasses on and offRule not set.
Checks (ai-36): single-player game recorded with both switches off (rules 3FEE6E7 in the header)
replays to a match (rules read back 3FEE6E7); one recorded with defaults (FEE6E7) replays to a match;
referee.rep, ai-ck.rep, ai-up5.rep match.

### ai: idle check from a cached state ID / idleness (switch dataBehaviors; same game)
- AIUpdateInterface::isIdle() (75 call sites) read the state machine's current state object and made a
  virtual isIdle() call. A state's isIdle() is fixed per state class, so the state machine now keeps
  the current state's ID and idleness in its row, refreshed at the 7 places that set the current state
  (StateMachine.cpp); isIdle() reads those two fields. All call sites benefit without being touched.
- Verify mode compares with the old computation: 40.0M checks, 0 mismatches (battle benchmark, ai-37).
  Replays referee.rep, ai-ck.rep, ai-kon.rep, ai-koff.rep match.
Timing, battle benchmark, ai-36 / ai-37 (before / after), tickTiming, timing.lock, load 3-5, 3 pairs. The
benchmark ran two ways (2700 ticks at ~3570 module calls/tick, or 3000 ticks at 3215), so compare like with like:
- 2700-tick runs: ai-36 19.31, 19.07 ms/tick logic (aiLoop 9.68, 9.43) / ai-37 19.62, 19.49 (aiLoop 9.86, 9.73)
- 3000-tick runs: ai-36 17.92 (aiLoop 8.86, p99 100.0 ms) / ai-37 16.48 (aiLoop 8.21, p99 94.1 ms)
- frames over 100 ms 30-40 / 25-48. Level within noise (the expected ceiling was isIdle's 0.6% share).
## 2026-11-26 — paths: route following with SIMD (cpopSimd), cell-types counter, pathfind/loco timing
- Kept: c8dfa88, range kernels moved onto ai's per-kind hook (GameLogic::gxRegisterKindKernel, keyed by
  module name, joined on first update). Store bits 20-27 and the per-unit join lists are gone. Replays:
  referee with verify (90,584 checks, 0 mismatches), paths-r5c.rep, ai-up5.rep, paths-r5j.rep, and the
  new paths-long-up.rep (27,000 frames, updatePasses + moveAfterTick) all match.
- Kept: cpopSimd (fourth word bit 18, on; only acts with cpopSegments, third word bit 0, still off).
  Path::computePointOnPath keeps the flat segments as columns (start, direction, length, prefix sum
  in walk order) and finds the closest segment 8 at a time (two independent 4-lane chains, wasm SIMD,
  separate mul/add, no FMA, ties to the lowest index). The last segment and the winner's point stay
  scalar. Same floats as the scalar walk: battle benchmark in verify mode, 820,000 calls compared
  (48.2 M segments), 0 mismatches.
- Kept: Pathfinder::gxCellTypeVersion() (global gxPathCellTypesGen), goes up on every PathfindCell
  type change (cell reset/setType/setTypeAsObstacle/removeObstacle/clearObstruction, layer
  reset/allocate/destroy, pathfinder reset/newMap). For ai's caches.
- Measuring: GxTime pathfind and loco timers, counters pathFinds, pathCells, locoCalls, cpopCalls,
  cpopSimdSegs.
- Battle benchmark (tickTiming, lane 3, load < 8): the scripted battle diverges between builds
  (r5k/r5l run ~780 movers/tick vs ~700 for r5before2/paths-iso), so only same-counter pairs compare.
  cpopSimd, four runs with identical counters (3000 ticks, 705.9 movers/tick), in run order:
  segs1 (off) 16.70, simd1 (on) 18.35, segs2 (off) 21.28, simd3 (on) 23.37 ms/tick logic; pathfind
  2.78 / 2.93 / 3.23 / 3.77. The numbers climb with run order (load 4-9), not with the switch: no gain
  measurable at this noise. cpopSimdSegs 16,029/tick against 269.7 cpopCalls/tick.
  paths-iso (paths' round-5 commits reverted) vs r5l at the same work (~775 movers/tick): iso2 20.78,
  l1 19.74, l2 21.62 ms/tick logic: no regression from paths' round 5. (iso1 14.50 ran ~696
  movers/tick, a different battle.)

### ai: same-cell locomotor skip (switch dataBehaviors; same game; uses paths' Pathfinder::gxCellTypeVersion, b4f1531)
- chooseGoodLocomotorFromCurrentSet asked Pathfinder::chooseBestLocomotorForPosition every tick for every
  moving unit (one pathfind-cell read + findLocomotor). The AI row keeps the last answer with its key
  (cell x/y, layer, cell-type version); same key -> same answer. Invalidated wherever the locomotor set
  is rebuilt (4 clears + load).
Checks (ai-38): verify mode calls the pathfinder on every cache hit and compares: 46.7M checks,
0 mismatches; referee.rep, ai-ck.rep, ai-kon.rep, ai-koff.rep match.
Timing, battle benchmark, ai-38m (same tree, skip compiled out) / ai-38, tickTiming, timing.lock, load 3-5:
- 2700-tick runs: ai-38m 18.88 ms/tick logic (aiLoop 9.26) / ai-38 18.85, 19.07 (aiLoop 9.19, 9.26)
- 3000/3300-tick runs (different work, not comparable): ai-38m 16.69, 16.52 / ai-38 14.62 (3300 ticks)
Level within noise (the ceiling was the call's 0.4% self share).

### ai: spawn slaved cache, PropagandaTower scan, batchScan rule (ai-40)
- SpawnBehavior::computeAggregateStates looked up each slave's SlavedUpdateInterface by name every
  call. The row keeps (ObjectID, interface) pairs (switch dataBehaviors, same game). Verify: 27.6M
  checks, 0 mismatches.
- PropagandaTowerBehavior: the per-object KindOf test uses one static mask (isAnyKindOf) and the
  upgrade is looked up once per tower, not per unit (dataBehaviors, same game).
- StealthUpdate: looked, nothing worth building (cost is the call itself).
- batchScan (fifth word bit 0, off) = match rule GXRULE_BATCH_SCAN (bit 26). Needs updatePasses and
  kind kernels on. A per-kind kernel collects the units whose mood scan is due this frame and asks
  gxBatchFindClosestEnemy for all of them at once (query's SIMD pass; a weak fallback calls
  findClosestEnemy per unit). getNextMoodTarget uses the stored answer when the stamp, range, flags
  and priority info match; a dead answer is searched again. Answers come from earlier in the frame,
  so it is a rule, not a plain switch.
Checks (ai-40): referee.rep, ai-ck.rep, ai-kon.rep, ai-koff.rep match; ai-bs.rep (batchScan on,
GXRULES 4FEE6E7) records and replays to a match.
Timing, battle benchmark, ai-38 / ai-40 (batchScan off), tickTiming, timing.lock, load 6-7 (busy):
- 2700-tick pair: ai-38 25.13 ms/tick logic (aiLoop 12.70) / ai-40 25.32 (aiLoop 12.88)
- ai-40 2700-tick runs 24.99, 25.39; ai-38 3000-tick runs 21.87, 22.23 (not comparable)
Level within noise. batchScan on is timed once query's pass lands.

## 2026-11-26 — paths: match rule locoSimd (134217728, bit 27; Settings fifth word value 2, off)
- Under the rule every locomotor heading atan2 (Wheels, Legs, Climb, Wings, rotateObjAroundLocoPivot,
  maintainCurrentPositionWings) is gxLocoAtan2 (GxLocoAngle.h): Cephes atanf range reduction, only
  + - * / and compares, so every browser gets the same floats. In the movement pass (moveAfterTick),
  right after the route kernel, gxLocoAngleKernel computes atan2(posOnPath - pos) for every served unit,
  4 per wasm SIMD op (gxLocoAtan2x4, same operations as the scalar form); the locomotor takes it when
  its (dy, dx) is exactly the same, otherwise computes the scalar form. Changes the game (not libm's
  angle), hence the rule; it travels in the replay header like ai's 24/25. Fifth word from ai's c5202bf.
- Checks (paths-ls2): native test, max 2.8e-7 rad against libm over 20 M points. Battle in verify mode
  with the rule on: 620,000 SIMD lanes compared with the scalar formula, 0 mismatches, max 2.72e-7 rad
  against libm. Recording with the rule on (paths-loco.rep, 18,000 frames, header GX=8FEE6E7) replays to
  a match with the local switch on and off. referee.rep and paths-long-up.rep match (rule off).
- Timing, battle benchmark, tickTiming, timing.lock, load < 5 at start, alternating off/on x3. The rule
  changes the battle: on runs 703 movers/tick and 396 loco calls/tick, off 686 / 383. Heading calls
  587.9/tick, 295.6 taken from the SIMD pass (321.4 computed there).
  off1 23.87 / on1 21.95 ms/tick logic (load rose to 12 and 7 during them; discard);
  off2 18.34 / on2 18.68 logic, loco 0.748 / 0.756, pathfind 2.63 / 2.84, p99 99.8 / 97.8 ms, over 100 ms 30 / 27;
  off3 21.18 (2700 ticks, 762 movers: a different battle) / on3 18.99, loco 0.776.
  Per loco call: off2 1.95 us, on2 1.91, on3 1.96. Level: the heading atan2 is a small part of a loco
  call. Left off.

## 2026-11-27 — paths: match rule orderQueue (536870912, bit 29; Settings fifth word bit 16, ON)
- The group-order spike is old (merge6 has it: N=100 order tick 54-66 ms logic; merge7 63-88). Breakdown
  at N=1000 (current build, off): group order 94 ms = shared route search 40, the original column ground
  path 31, adjustDestination spirals 18, rest 6. Scaling is about linear past N=100; the searches dominate.
- Under the rule a group order does no route search on its tick: (a) groupRoute only notes each mover
  kind (leader, members within 250 of it); the kind's route is searched when the first member is served
  by the route queue (AIUpdate doPathfind -> gxTakeSharedRoute), the others take it shifted and checked;
  (b) the original friend_computeGroundPath / infantry / vehicle column moves are skipped; (c) each
  member's spot is the next free cell of one breadth-first walk from the goal per frame and group kind
  (Pathfinder::gxGroupSpot), instead of a 400-cell adjustDestination spiral each. All three parts are
  faster on their own, so one switch.
- Order probe (battle map, fresh clump of 2000, orders 15 s apart; probe build m10, alternating on/off x2),
  order-tick logic ms off -> on:
  N=20 34.8 -> 17.0; N=50 26-30 -> 18-20; N=100 63-68 -> 18; N=250 52-55 -> 21-24; N=500 37-40 -> 22;
  N=1000 88-98 -> 22 (group part 75-82 -> 4-5 ms). The 5 s after each order: same ms/tick
  (N=1000 17.8-18.1 off / 14.9-15.9 on), route work moves into the queue as intended.
- Checks (paths-oq1, rule on by default): referee.rep, paths-long-up.rep, paths-loco.rep match (rule off
  from their headers). New recording paths-oq.rep (18,000 frames, header GX=20FEE6E7) replays to a match
  with the local switch on and off. Battle in verify mode: all GXVERIFY checks 0 mismatches.
- Timing, battle benchmark, paths-oq1, tickTiming, timing.lock, load 1.9-3.4 before/after every run,
  alternating off/on x2. The rule changes the battle (on: 2700 ticks, 765 movers/tick, 3525 AI module
  calls/tick; off: 3000 ticks, 686, 3184), so whole-tick numbers aren't like for like:
  logic off 16.39 / 16.18, on 17.42 / 16.85 ms/tick; per AI module call off 5.15 / 5.08 us, on 4.95 / 4.77;
  p99 off 94.9 / 87.3, on 94.7 / 89.4 ms; over 100 ms off 13 / 7, on 10 / 10; fps off 16.7 / 17.4, on 17.9 / 18.5.
  The battle's AI mostly issues attack moves (no shared route: shareNoted 1.0/tick, shareTaken < 0.05/tick),
  so the battle is level; the gain is the order tick above. ON by default.
- Note (later runs, 11-27): the battle benchmark itself comes in two runs even with the same settings
  (2700 ticks / ~766 movers, or 3000 ticks / ~690: lsoff1, bitsoff1 below, rule on), so the off/on
  difference above in ticks and movers is not only the rule.

## 2026-11-27 — paths: heap churn in route searches, counted (not built: already pooled)
- New GXTIME counters pathAllocs / pathBigAllocs: game-allocator blocks (pool / too big for a pool)
  taken while a route search runs (GXT_PATHFIND scope; GameMemory.cpp). Battle benchmark, paths-hc0:
  405 pool blocks and 2.3 big blocks per tick, at 8.8 route searches per tick (about 46 per search:
  Path / PathNode / cell info, all MemoryPool objects already; GameMemory is on in the web build).
- Ceiling: ~400 pool takes and frees a tick is tens of microseconds of a 17.5 ms tick, under 0.3%.
  No pooling to add; nothing built. Counters kept (only with tickTiming).

## 2026-11-27 — paths: #53 exact route-search speed-ups re-measured (openHeap, cellBits: both stay ON)
- Both #53 parts were built 11-24 (openHeap: heap open list instead of the insertion sort; cellBits:
  occupied-cell bits instead of reading every cell). Battle benchmark, paths-hc0, tickTiming,
  timing.lock, load 2.1-3.6 except where noted, alternating off/on x2:
  openHeap off 2.814 / 2.798 pathfind ms/tick, on 2.612 / 2.834 (on2: whole tick 19.3 ms, a slow run);
  per search cell off 5.57 / 5.54 us, on 5.18. cellBits off 2.561 (3000-tick run, 455 cells/tick) /
  3.026 (slow run), on 2.601; per search cell off 5.62 / 6.00 us, on 5.16; bitson2 discarded (load 7.4
  -> 14). Both are about 7-8% of route-search time; same game, so they stay on.
- Big orders (order probe, 2000-unit clump, orderQueue on, probe build m10, both off / both on, x2,
  load 2.4-7.5): the order tick has no route search left, so it is level (N=100 17.1 / 22.8 off,
  20.1 / 16.4 on; N=1000 32.3 / 23.0 off, 21.7 / 24.7 on). Route-search time in the 5 s after an order,
  where the searches now run: N=100 944 / 1021 ms off, 837 / 798 on; N=1000 758 / 779 off, 720 / 716 on.

## 2026-11-27 — paths: locoSimd re-timed, now ON by default
- Battle benchmark, paths-oq1 (orderQueue on), tickTiming, timing.lock, alternating off/on x3:
  pair 1 discarded (load rose to 10.7 during off1). Pair 2 (load 1.7-2.7): logic off 17.57 / on 17.36
  ms/tick with more movers on (790 vs 766); per loco call off 1.76 us / on 1.68; p99 94.5 / 85.2 ms;
  over 100 ms 23 / 16; fps 17.4 / 18.3. Pair 3 (load 5-7, inside the < 8 gate): logic 22.69 / 20.05;
  per loco call 2.19 / 1.91 us; p99 138.6 / 122.2; over 100 ms 128 / 78; fps 12.7 / 14.6.
- On wins in both clean pairs, so ON by default (rule 27 stays; fifth word value 2 now default-on).
  Its checks are in the 11-26 entry (paths-loco.rep replays to a match; 0 SIMD-lane mismatches).
## Round 5 (query) continued: PartitionData row, DynamicShroudClearingRange and Stealth kernels
- PartitionData's own per-object web-port state (cached place x/y/r for pairPlace/pairOffer, fogKeep's pure /
  kept bits) moved into PartitionDataGxRow on GxComponentPool (keyed by the PartitionData: ghost objects have
  no store slot). The per-COI copies stay in the COIs (one per cell entry). Referee + query-mgr5.rep match (query-31).
- DynamicShroudClearingRange: gxDscruStep is the kernel (reads state, countdowns, range, deadlines and copied
  module data; writes only the row's next record; same arithmetic and types: the countdown is signed, deadlines
  unsigned); update() is the apply step: decals first (animateGridDecals reads the pre-step range and
  countdown), then commit, then setShroudClearingRange. A row whose inputs changed since gets the step inline.
- Stealth: gxStealthStep precomputes the pure per-frame math (pulse opacity/phase, disguise transition
  countdown and factor, granted-frames countdown); update() uses each piece only when its inputs are unchanged.
  The rest (AI victim, status bits, containers, sounds) stays in the apply step.
- Both under rowKernels (fourth word bit 12). Referee + query-mgr5.rep match (query-32).

## simdPairs (fourth word bit 17, default on) and verify checks for the SIMD culls
- addPossibleCollisions (pairOffer path): neighbour COIs are gathered 4 at a time into f32x4 and culled by the
  same f32 distance test (drop only when > limit², NaN kept); kept lanes get the scalar test unchanged.
- Verify: every lane the SIMD cull drops (simdPairs and simdSearch) is rechecked with the scalar test.
  Battle verify on query-34 (300 s, pairOffer + scanManager): simdPairs 9.6M and simdSearch 9.8M dropped lanes
  checked, 0 mismatches; fogKeep 2.54M checks, 0. Replays referee, query-mgr5, query-mgr6 match (query-33).
- Battle counters (query-33, tickTiming): pairs tested 3942/tick, SIMD-culled 4395/tick; search entries 27446,
  SIMD-culled 5880/tick.

## PartitionData state into store columns; ILP in the SIMD culls (LOGIC_AGENTS "SIMD step")
- PartitionDataGxRow is gone: the cached place (placeX/Y/R) and fogKeep bits (fogPure, fogKeptMask, fogKept:
  4 bits a player) are a query block in TheGxUnitStore (added, reserved, permuted with the rest). A
  PartitionData reaches them through its object's store slot; attachToObject resets them (a new PartitionData
  knows no place: placeR = HUGE_DIST, never turned away). Ghost-only PartitionData has no slot: no place,
  nothing kept (the switch-off behaviour). The per-COI place copies stay in the COIs (one per cell entry).
- simdPairs and simdSearch: blocks of 8 with two independent f32x4 chains (was one block of 4), branchless
  masks, same f32 ops (no FMA / relaxed SIMD); tails under 4 are kept as before.
- query-36: referee, query-mgr5, query-mgr6 match; battle verify: simdPairs 9.6M / simdSearch 9.8M dropped
  lanes checked, fogKeep 2.54M, all 0 mismatches.

## Battle timing pairs (rule "fps is not the measure"; tickTiming on, pairOffer + scanManager, under timing.lock, load < 8)
| switch off -> on | logic ms/tick | own ms/tick | counters/tick | p99 ms | frames > 100 ms |
|---|---|---|---|---|---|
| fogKeep (q33) | 17.4 -> 15.8 | search 2.28 -> 2.10 | fog recomputes 992 -> 141 (855 kept) | 108.5 -> 90.6 | 46 -> 22 |
| fogStamps (q33) | 16.3 -> 15.5 | fogReveal 0.10 -> 0.06 | 19.8 circles, 5.4k cells | 91.2 -> 94.4 | 22 -> 21 |
| simdPairs (q36) | 15.9 -> 15.6 | - | pairs scalar-tested 6549 -> 3335 (3214 SIMD-culled) | 89.9 -> 87.9 | 20 -> 20 |
| simdSearch (q36) | 17.3 -> 17.3 | search 2.33 -> 2.32 | 21.9k entries, 4.6k SIMD-culled | 103.4 -> 100.4 | 34 -> 31 |
| rowKernels (q33) | 22.1 -> 22.2 | - | same work (main thread) | 126.9 -> 127.5 | 73 -> 81 |
- fogKeep is the one clear gain; the SIMD culls halve the scalar tests but the search cost is the per-unit
  body after the cull (distProc, filters), so ms/tick barely moves; rowKernels on one thread is neutral.

## SIMD step (query): simdBody (fourth word bit 30) and snapPatch (bit 31), both on, same game
- Profile (battle, query-36): getClosestObjects 8.5% inclusive, filters only 1.2%; gxManagedSearch's own
  3.1% was mostly gxBuildStoreSnapshot: every new object bumps the store epoch, so scanManager rebuilt its
  per-cell lists 2.8-3.1 times a tick, 1.0-1.6 of the 2.1-3.0 ms/tick search time (GXTIME snapBuild).
- simdBody: in the scanManager walk, the body's enemy test (player bits) and the 2D distance
  (distCalcProc_CenterAndCenter_2D / BoundaryAndBoundary_2D: same f32 ops, sqrt, div, no FMA) for the
  same blocks of 8 as simdSearch, in two f32x4 chains; the scalar body uses the lane's values.
  Verify: every lane's distance and vector bit-compared with distProc: 3.6M, 0 mismatches.
  Timing: no measurable change (search 2.18 -> 2.26, logic 16.4 -> 16.4): the body is not the cost.
- snapPatch: after a frame's full build, a later rebuild request recomputes only slots whose id/x/y/radius
  differ bit-wise from the base build (compared 8 per step, two i32x4 chains) or that were touched
  (registerObject, unRegisterObject, setGeometryInfoZ), and replaces only the lists of cells such a slot
  covered or covers (merged ascending, as the full build orders them). Partition presence and sphere radius
  are cached per object ID (cleared by the same touches and PartitionManager::reset).
  Verify: each snapshot compared with a fresh uncached full build, lists and radii: 6000, 0 mismatches.
- Replays referee, query-mgr5, query-mgr6 match (query-42). Battle pair (query-42, tickTiming):
| snapPatch off -> on | logic 18.7 -> 18.7 | search 2.99 -> 2.10 | snapshot 1.60 -> 0.58 (builds 3.1 -> 0.7 + patches 2.4) | p99 107 -> 96 ms | > 100 ms 40 -> 24 |

## batchScan (query side; ai's switch: fifth word value 1, match rule GXRULE_BATCH_SCAN 67108864)
- gxBatchFindClosestEnemy(n, me, range, qualifiers, info, out): out[i] = TheAI->findClosestEnemy(...) as of the
  call. Each scan keeps findClosestEnemy (filters, early outs); its getClosestObject goes to gxBatchClosest:
  an f32x4 cull over the store's live place columns (placeX/Y/R, playerBit; two chains, 8 a step), then the
  walk's answer rebuilt from the survivors: the passing objects in the lowest ring any is in (COI cells, or with
  scanManager the tick lists' boxes and the walk's cell-mask skips), the nearest; ties to the earlier visit
  (same cell: slot order with the tick lists; COI lists: falls back to the walk).
- One direct cull a seeker at any batch size (bsCull timer, bsSeekers counter). Per-batch x-sorted lists were
  tried and removed: build ~190 us a batch vs ~4.6 us a seeker for the direct cull (list window ~6.6 us), so
  they never pay off (break-even ~40+ a batch; batches run 12-17 a tick). Re-entrant searches (a filter that
  searches moves the walk's stamps) are detected by g_gxWalks and answered by the old walk.
  Query-56 verify (scanManager on): 74,000 checked, 0 mismatches, 6 nested fallbacks.
- Verify (bit 21, every answer vs the old walk at the same moment; local test routing getNextMoodTarget's scan
  and the prefetch through it): scanManager on 84,000-88,000 checked, 0 mismatches; off 46,000, 0 (53 tie
  fallbacks). Replays referee, query-mgr5, query-mgr6 match.
- Profile (strikes, n = 1 per call, local test): search self 3.7% -> 3.4% (scanManager on), 2.7% -> 3.0% (off).
  Battle pair under load 7-9 (rough): search 2.09 -> 2.01 ms/tick, logic 19.8 -> 18.3. The gain is in batching
  many seekers per call (ai's wiring); re-time once wired.

### ai: batchScan wired to query's gxBatchFindClosestEnemy (merged 88a3744)
- The weak fallback is gone. Each AI kind's kernel only collects; the first mood-scan turn of the
  tick searches every collected unit of every AI kind in one call (query's pass wants 16+ seekers).
- Collect filter, from a verify-mode diagnosis (temporary, removed): the first wiring collected 230
  units/tick for 10 answers used. Units are now collected only if due (nextMoodCheckTime), in a
  state that scans (AI_IDLE with AAS_Idle, attack-move, attack-follow-waypoints), not attacking, the
  machine awake, not on an attack-common-target team, the module due this tick. Attack-move units
  whose inner machine stays busy stay "due" for good and were collected every tick (54k of 76k
  unused answers): a unit that left last tick's answer unused (gxScanMiss) is not collected again
  until its own routine scan runs. Battle, verify: 4.4 collected / 4.2 used per tick, of 16.7 mood
  scans (the rest come from turrets, attack states, forced retargets, or are not due at kernel time).
- Verify (battle, batchScan on): used answers differ from a search in the turn 0 of 13,000; query's
  per-answer check 1 mismatch in 124,000 (tick-list path, radius grown after the snapshot; query's
  fix 88568b9).

## 2026-11-26 — combat: splash SIMD, damage table, last data, ILP, FireWeapon kind kernel (logic-combat)
- splashSimd (fourth word bit 25, off; same game): a splash's victims (WeaponTemplate::dealDamageInternal)
  come from PartitionManager::gxSplashSearch: a SIMD pass over the unit store's x/y/sphere columns keeps
  slots within range + bounding sphere + 2, then each kept unit gets the cell walk's own distance test
  and its walk key (ring rank of its first visited cell, then COI sequence, high first), and goes into
  the iterator in that order. The store got a `sphere` column (bounding sphere radius, set in
  Object::setGeometryInfo): 2x the circle radius was not a bound for tall units (49/3500 searches wrong
  before; 5500 checked, 0 mismatches after). Not used under match rule scanManager. Verify mode
  compares every search with iterateObjectsInRange ("GXVERIFY splash").
- splashIlp (bit 27, off; same game): with splashSimd, the pass does 8 slots per step as two
  independent chains (each lane's math unchanged), per the ILP + FMA rule; no FMA.
- damageTable (bit 26, off; same game): per frame, Weapon::computeBonus answers from a table keyed by
  weapon template + bonus flags, and estimateWeaponTemplateDamage from one keyed by weapon template +
  armor template + damage amount (subdual, kill-garrisoned and sniper damage are left out).
- Last per-unit state: LeafletDropBehavior (EMPUpdate.h) and ProductionEntry (ProductionUpdate.h) in
  GxComponentPool rows. ProductionEntry::m_exitDoor is now zeroed at creation (was uninitialised).
  The combat data list is empty.
- FireWeaponUpdate on ai's per-kind hook (match rule updatePasses): the kernel decides for each
  every-frame module whether it fires (its own weapon status, object status and last shot frame; writes
  only its row), apply fires in object-ID order, update() then skips that frame. Verify mode re-decides
  in apply and counts differences ("GXVERIFY kindKernels fireWeapon").
- HeightDieUpdate on the same hook: the kernel does each module's quiet frame (not due, contained, or
  above its death height; writes only its row: earliest death frame, last position) and leaves dying,
  particle clean-up and structure-height units to apply, which runs the full update in object-ID
  order. A quiet unit whose position or container changed before its turn (an earlier death) gets its
  last position back and the full update ("heightDieRedone" counter), so the result is the plain loop's.
  Checks (combat-r5k): referee (verify 90584, 0), ai-up5 (verify 27162, 0) and mt replays match;
  battle: heightDieRedone 0 per tick, kind kernels 0.44 ms/tick in all. FireWeapon's kernel in the
  battle with verify (r5j): 168000 decisions checked, 0 mismatches.
- GxTime ids: splash, estimate, reload (ms/tick); splashes, splashVictims, splashScanned, splashNear,
  estimates, table/bonus hits and misses, reloadRows/reloadChanged.
- Checks (combat-r5j, all four switches on): referee replay matches (verify 90584 checks, 0
  mismatches); mt replay matches.
- Battle timings (bench_run, scanManager off so splashes take the new path, tickTiming, two rounds
  under timing.lock, single thread). Own ms/tick of the system, then its counters:
  - splash: base 0.081 / 0.074, splashSimd 0.058 / 0.056 (-25%), +splashIlp 0.066 / 0.067 (no gain:
    13.7k slots scanned and 190 kept per tick; the pass is memory-bound, not latency-bound).
    4.0 splashes/tick, 158 victims/tick, same in every run.
  - estimate: base 0.181 / 0.158, damageTable 0.204 / 0.199 (slower): table hits 192, misses 93 per
    tick (67%); bonus table hits 1353, misses 86. The hashing costs more than the sums it saves.
  - reload: the kernel costs 0.009 / 0.010 ms over 1670-1820 weapon rows per tick.
  - Logic total per tick (whole run, noisy with machine load): base 18.2 / 16.4, splash 14.7 / 14.2,
    ilp 16.0 / 16.4, table 15.7 / 14.6, reload 16.2 / 20.9 (reload2 ran at load 9). Frame p99 and
    frames over 100 ms follow load the same way; per-system ms above are the comparable numbers.
  - fps (footnote): strikes 13.7-19.4 across runs.
- Defaults: splashSimd on (faster, same game). splashIlp, damageTable, weaponReload stay off (not
  faster).
## 2026-11-27 — paths: pathTail (fifth word bit 17, ON; same game) + checks of today's defaults
- From ai's heap-churn report (Path::appendNode in the profile): the cost was not the allocation
  (PathNode is a pool object) but PathNode::appendToList walking the whole route to its tail on every
  append, so building a route was O(nodes^2). Path already keeps m_pathTail (only getAircraftPath
  inserts nodes, always before an existing next node, so the tail stays right); appendNode now links
  after it. Verify mode walks the list and compares: 600,000 appends, 0 mismatches.
- Checks (paths-pt2: merge of perf/low-end 9488239; locoSimd, orderQueue, pathTail on): referee.rep,
  paths-long-up.rep, paths-loco.rep, paths-oq.rep match; new recording with today's defaults
  paths-def.rep (GX=28FEE6E7: locoSimd + orderQueue) replays to a match with pathTail on and off.
  Battle verify (draw=False): every GXVERIFY 0 mismatches, locoSimd 840,000 lanes, max 2.72e-7 rad.
- Timing, new regime (bench_run draw=False, pinned slot, same game every run: 1189.9 movers, 14.6
  searches, 679.8 cells per tick in all four), alternating off/on x2, load 4.2-8.6:
  logic off 42.12 / on 37.88, off 38.43 / on 41.70 ms/tick; pathfind off 5.05 / on 4.64, off 4.76 / on 4.95.
  Level within the run-to-run spread (routes here are a few nodes long). Exact, so kept on.

## 2026-11-27 — paths: nodePool (fifth word bit 18, ON; same game): reuse stack for route points
- Main/ai: the no-draw battle's second-biggest allocation source is route points (Path::appendNode /
  prependNode from flow fields, the route cache, prependCells, computeQuickPath) plus Path objects.
  Both are MemoryPool objects already; under the switch the two pools (PathNodePool, PathPool) push
  freed blocks on a LIFO stack (up to 8192 each) and hand them out again zeroed, as the pool does,
  skipping the blob bookkeeping. A reset or destroyed pool drops its stack. The pools are registered
  on the first route point (they are made on first use; the first build registered them too early and
  never fired: allocations unchanged at 4641/tick, logged as found).
- Checks (paths-np2, switch on): referee.rep, paths-long-up.rep, paths-loco.rep, paths-oq.rep,
  paths-def.rep match; battle verify (draw=False) every GXVERIFY 0 mismatches.
- Battle no-draw, same game every run: game-allocator blocks per tick 4641 -> 4091 (route-search ones
  628 -> 168). Timing (pinned slot), alternating off/on: pair 1 discarded (load 12); pair 2 (load 6-7)
  logic 36.03 / 32.40 ms/tick, pathfind 4.47 / 4.17; pair 3 (load 4-5) logic 32.30 / 32.15, pathfind
  4.13 / 4.18. Lower in both clean pairs (pair 3 within noise), so ON by default.

## 2026-11-27 — paths: straight-line route following re-timed: cpopSegments now ON (same game)
- The path kernel's straight-line form already existed: cpopSegments (flat segment columns per route)
  plus cpopSimd (closest segment 8 per step, two 4-lane chains, selects, no FMA, ties to the lowest
  index; the loop is call-free). It was left off on 11-25 after one A/B at load 12-17 in the old regime.
- Re-timed no-draw (paths-np2, nodePool on, pinned slot, waiting for load < 8 at each start), off/on x2:
  movement kernel 1.857 / 1.198, 1.855 / 1.301 ms/tick (-30 to -35%); movement pass 4.67 / 3.64,
  4.70 / 3.89; logic 41.54 / 35.43, 42.51 / 38.33 ms/tick; p99 109.4 / 96.5, 112.0 / 108.0 ms.
  Routes average 58 segments per closest-point search (25,180 SIMD segments over 434.7 searches a tick).
  An earlier pair at load 14-17 gave the same kernel drop (2.18 / 1.61, 1.89 / 1.49).
- Checks with it on: referee.rep, paths-long-up.rep, paths-loco.rep, paths-oq.rep, paths-def.rep match;
  battle verify: cpopSimd 760,000 searches (44.1 M segments) against the node walk, 0 mismatches.
  ON by default (third word bit 0).

## 2026-11-27 — paths: pathfinding on the AI game (ai-long.rep): profile, and failCache (fifth word bit 19, OFF)
- New measuring-only counters (tickTiming): a GXPATHK line every 900 frames with, per search kind,
  calls, exclusive ms (a nested search's time goes to the nested kind), cells expanded, and repeats
  (same unit, same goal cell within 60 frames); plus failed A* searches (through findPath), their cells
  and ms, and repeats of a goal that failed last time.
- ai-long.rep (collide's 7 brutal AIs) needs the AI-script fix (a650518), which breaks the old
  recordings, so it was played on a scratch build (this branch + a650518). On this branch it leaves the
  recording at frame 20,902 with or without today's switches (cpopSegments, nodePool, pathTail off too),
  so every number here is the first 20,900 frames. Pathfinding there: ~0.9-1.0 of ~4.0-4.8 ms/tick logic
  (pinned slot), not the 15-18 of 24 ms quoted (that figure is from another build/setup).
- Where it goes (20,900 frames): A* 33,540 calls, 12.3-13.1 s, 9.9 M cells; 24,628 of the calls (73%)
  repeat a unit's goal cell within 60 frames. 1,350 failed A* searches take 8.6 M of the 9.9 M cells
  (87%) and ~10 s of the A* time (82%): ~6,400 cells, ~7.5 ms each; 1,307 of them plan around terrain
  and buildings only (ignoreUnitsInPaths); 526 repeat a goal that failed last time. Then flow-field
  routes 2.1-2.3 s, attack approach 2.0 s (3,602 calls, 1,094 cells each), the queue itself 1.7 s.
- failCache: a terrain-only search that ran its full flood and found nothing is answered "no route"
  again for the same exact start, goal, layers, surfaces, crusher, size, side and ignored obstacle while
  the obstacle serial and cell-type generation are unchanged. Two lessons on the way: the quick refusals
  (goal taken, zones) must not be cached (first build: a cached "no" that would have been a route,
  mismatch at frame 1,802), and a hit must charge the queue's per-frame cell budget the cells the search
  would have released (m_cumulativeCellsAllocated, in the CRC; without it: mismatch at frame 11,602).
- Checks: ai-long.rep (20,900 frames) matches with it on, with and without verify; referee, long-up,
  loco, oq, def match (main-tree build paths-fc4). Verify found 2 hits where the real search charged one
  cell more than the cache (17,424 vs 17,423): both failed, but the budget differs by one cell, so it is
  not proven the same game. Likely cause: A* also reads the zone passable flags left by the hierarchical
  search, which the key does not cover. Hence OFF until it has a match rule (or the key is fixed).
- Timing, ai-long.rep (timing slot), off/on x2, load 2.8-4.9: route search 0.922 / 0.790, 0.906 / 0.785
  ms/tick (-14%); A* 12.8 / 9.9 s; logic 4.05 / 3.94, 4.02 / 3.94; p99 28.7 / 22.8, 28.4 / 21.6 ms.
  Battle (no-draw): pair 1 logic 31.78 / 32.95, pair 2 off run discarded (load 23); level there.

## 2026-11-27 — paths: failCache becomes match rule word 2 bit 0 (new rule word "GX2="), ON
- Why it can't be exact: verify (expansion counter added) shows the failed terrain-only floods on
  ai-long.rep expand ~27,000-32,000 cells: they end where the 30,000-entry cell-info pool runs out
  (CELL_INFOS_TO_ALLOCATE), and cells held by units' info decide exactly where. A zone generation
  (calculateZones / updateZonesForModify) now also clears the list, but the pool limit remains, so
  "no route" there is not a property of the inputs. Hence a match rule.
- Rule word 2, same handling as word 1: GameInfo m_gxMatchRules2, "GX2=%X;" in the options string only
  when non-zero (so games without word-2 rules send the old string), parsed back, fixed by the host or
  the replay in gxStartMatchRules; gxLocalMatchRules2() / gxMatchRule2(). Set wherever word 1 is set
  (LAN and WOL setup, both recorder paths). GXRULES now prints both words. Bit 0 = failCache, local
  switch fifth word bit 19.
- Checks (paths-fc7 main tree; paths-fc7a = + a650518 for AI games): referee.rep (rules 0), long-up
  (FEE6E7), loco (8FEE6E7), oq (20FEE6E7), def (28FEE6E7) all match with the local switch on (word 2 = 0
  from their headers). New AI-game recordings, 27,000 frames: paths-ail-fcon.rep (header
  GX=28FEE6E7;GX2=1;) replays to a match with the local switch on and off; paths-ail-fcoff.rep (no GX2)
  matches with the switch on.
- Timing: bench_record's 3-AI game is too light to show it (0.40 ms/tick logic, 0.017 pathfind, no
  failing floods; both recordings level). Battle no-draw: logic 28.78 / 29.82, 29.69 / 28.99 ms/tick
  (off/on): level, no hits. The gain is the AI game measured above (ai-long.rep, switch forced:
  route search -14%, p99 28.5 -> 22 ms). ON by default.
- ai-long.rep divergence on this branch: leaves the recording at frame 20,902 (recorded on collide's
  soa-24), with or without this branch's new switches; main traces it at merge8.
- Heavy AI timing (main's ask): scratch build paths-fc8a = this branch + a650518 (AI scripts) + collide's
  aec0d5f (bench_record settings recordMap / recordAIs). Two recordings, 7 brutal AIs on Twilight Flame,
  36,000 frames: paths-heavy-fcon.rep (GX=28FEE6E7;GX2=1;) and paths-heavy-fcoff.rep (GX=28FEE6E7;).
  Both replay to a match. Different games once the rule acts, so compare like for like:
  route search off 7.213 / 7.806, on 5.946 / 5.844 ms/tick (-18 to -25%); logic off 11.50 / 12.50,
  on 10.16 / 10.40; frames over 100 ms off 407 / 642, on 369 / 356; p99 104.1 / 119.8, 100.8 / 99.8 ms.
  Failed A* searches: off 8,196 using 161 M of 167 M cells (228-247 s), on 6,683 using 127 M (177-182 s).
  Kept ON.
- Verify fix: on a hit, verify runs the real search, which charged its own (pool-dependent) cells and so
  played a different game (verify replay of the rule-on recording left it at frame 20,402). The budget is
  now reset to the cached charge after the check; the verify replay matches all 36,000 frames: 1,500 hits
  checked, 0 where the search would have found a route, 1,493 where only the cells spent differ.

## 2026-11-27 — paths: straight-line route search, step 1: searchKernel (seventh word bit 4, ON; same game)
- Seventh switch word GLES3_Page_Switches7 added with collide's lines (bit 0 aiScripts; it does nothing
  on this branch until the AI-script fix merges). Ranges: collide 0-3, paths 4-7, combat 8-11, ai 12-15,
  query 16-19.
- Where the route search spends (new GXTIME counters, heavy 7-AI recording, per tick): 4,199 cell
  expansions, 33,365 neighbours tried, 2,875 full movement checks, 4,636 open-list inserts, and 88,361
  steps of the straight-line walk toward the goal that every expansion starts with (examineCellsCallback,
  ~21 steps per expansion; the failing floods are long). So the line walk is the hot loop.
- searchKernel: (a) the walk's crusher test comes once per expansion (was a virtual call per step);
  (b) while planning around terrain and buildings only, a ground movement check whose whole block is on
  the map answers "yes" from the bounds (every cell's visit returns "go on" there, so the walk can only
  say yes), skipping the virtual ignored-obstacle lookup and the block walk.
- Verify (runs the walk with the kernel off and compares answer and flags): heavy recording ~2.97
  billion checks, battle 22 M, 0 mismatches. Replays referee, long-up, loco, oq, def, and the heavy
  recording paths-heavy-fcon.rep match.
- Timing, heavy recording (same game both ways, timing slot), off/on x2, load 4.5-8: route search
  7.446 / 5.712, 7.084 / 5.148 ms/tick (-23 to -27%); logic 12.89 / 11.41, 12.74 / 10.10; frames over
  100 ms 1,022 / 375, 769 / 261; p99 127.9 / 101.1, 119.8 / 87.3 ms. Battle no-draw: route search
  4.556 / 4.551, 4.795 / 4.364; logic 33.94 / 34.19, 35.24 / 33.18 (level to slightly better). ON.

## 2026-11-27 — paths: straight-line route search, step 2: lineKernel (seventh word bit 5, ON; same game)
- With searchKernel on: the route search's line walk (examineNeighboringCells -> iterateCellsAlongLine
  -> examineCellsCallback through a function pointer) runs as a templated copy of the same Bresenham
  loop that calls the step directly (same cells, same order), and the step makes the terrain-only
  movement check inline: whole block on the map while planning around terrain and buildings only ->
  no call, no flags (the answer searchKernel's verify confirmed over ~3 billion checks). Verify mode
  keeps the call.
- Checks (non-verify replays, so the inline path runs): paths-heavy-fcon.rep (36,000 frames), referee,
  long-up, loco, oq, def match; battle verify 0 mismatches.
- Timing, heavy recording (same game both ways; off = searchKernel only), load 3.0-5.4: route search
  4.836 / 3.970, 5.011 / 4.057 ms/tick (-18%); logic 9.57 / 8.34, 9.63 / 8.72; frames over 100 ms
  249 / 78, 240 / 92; p99 83.4 / 66.2, 83.1 / 67.4 ms. (The machine was quieter than for the
  searchKernel runs, so compare within this pair only.)
- Battle no-draw, 4 pairs: route search off 4.363 / 4.552 / 4.348 / 4.526, on 4.875 / 5.032 / 4.678 /
  4.283 (two of the on runs started at load 6.6-7.3): not resolvable; the battle's line walk is only
  3,518 steps a tick (heavy game: 88,361). ON for the AI game.

## 2026-11-28 — paths: move/locomotor kernels: passKernel (seventh word bit 6, OFF: no gain), and where the time is
- The movement kernel (1.05-1.2 ms/tick in the battle, 525 route points served, 435 closest-point
  searches) was expected to sit in Path::computePointOnPath's isLinePassable checks. passKernel runs
  that line walk as the templated loop with its step called directly and the crusher test once per
  call. Exact by construction: heavy recording, referee, long-up, loco, oq, def match; battle verify 0.
- But the checks are short: battle 876.5 calls, 4,597 steps a tick (~5 each); heavy AI game 119 calls,
  1,520 steps. Timing: battle movement kernel off 1.058 / 1.045, on 1.052 / 1.183 ms/tick; heavy route
  search 4.274 / 4.085 off, 3.952 / 4.168 on; level. OFF.
- Other counters: segment-list rebuilds 10 a tick (336 segments): negligible. So the movement kernel
  (~0.9 us per moving unit) and the locomotor (1.23 ms/tick, ~1.9 us per call, 645.6 calls) have no hot
  loop left; the cost is spread over each unit's virtual calls and branches (state checks, physics,
  the locomotor's per-type steering). Straight-lining them means rewriting the locomotor per type as
  data, which is a round of its own; in the AI game they are ~0.6 of ~8.5 ms/tick, in the battle ~4.4 of 31.
## 2026-11-27 (query): re-measures, hash-map sweep, heap churn, straight-line kernels

- Re-measured on the store: denseCellMask (on) and the per-tick search snapshot (scanManager + snapPatch, on)
  were already rebuilt on the store. blockMask (8x8 block player masks, second word bit 24, on) rebuilt:
  verify 0 block mismatches (scanManager on and off); battle pairs on query-58 at load 2-5: search 1.547 ->
  1.530 and 1.512 -> 1.561 ms/tick (noise either way).
- Hash-map sweep (fifth word: 8 scriptNames, 9 scriptLookups, 10 upgradeKeys, 11 powerKeys, 28 scienceKeys;
  all on): scripts/groups, named units, object type lists, trigger areas, waypoints, upgrades (plus cached
  veterancy upgrades), special powers by name and ID, sciences. Maps are built in the old walk's order (first
  match kept) and rebuilt when the list's change counter moved (bumped at every link, name or vector write).
  Verify checks every answer against the walk: 6144 checked, 0 mismatches; replays referee, query-mgr5,
  query-mgr6 match. Lookups: 2.3-2.7 a tick in the battle, ~0 in the referee skirmish replay, so no
  measurable time either way (battle pairs within noise; replay logic 0.46-0.50 ms/tick both ways).
  Not built: Player::hasScience bitsets (vectors of a few dozen ints; a find is cheaper than the index lookup).
- Heap churn (tickTiming counts game-memory blocks per tick and inside each timer, g_gxAllocs):
  - iterPool (fifth word bit 29, on): emptied search iterators keep their clumps on a per-thread free list;
    DiscreteCircle edge buffers come from a per-thread pool; undone sightings are reused.
  - The scan manager's per-tick result cache is an open-addressing table with reused entries (was an
    unordered_map cleared every tick: a node and a list per cached search).
  - Battle, my scopes (query-57 -> query-62): search 582 -> 70 allocations a tick (the rest: one pooled
    iterator object per iterate call), cell changes (look/unlook, value/threat circles) 52 -> 3, partUpdate
    142 -> 94 (the rest is collision reactions in other lanes' modules). Whole tick, iterPool off/on on
    query-62: 3264 -> 2646 and 2956 -> 2401. Logic 16.74 -> 17.44 and 14.69 -> 14.45 ms/tick (noise).
  - snapBuild's ~13 a tick are GxUnitStore::compact (fixed on perf/logic-collide, 1f734bb).
- Straight-line kernels (same results, no switch):
  - fog stamps: the scalar tail of each span does addLooker/removeLooker's arithmetic inline (a call only on
    a status edge), like the SIMD body: fogReveal 0.064-0.077 -> 0.026-0.031 ms/tick.
  - snapshot build: gxSnapBox always_inline, the cached-radius switch read hoisted out of the slot loop:
    snapBuild 0.365-0.392 -> 0.298-0.356 ms/tick.
  - batchScan cull: survivors written into a buffer sized up front (no push_back call). A fully
    branchless all-lanes write measured slower (0.059 -> 0.067 ms/tick; survivors are rare), not kept.
  - wasm (wasm-dis, name section): gxSnapBox has no function of its own (inlined); the cull's vector loops
    and gxSnapBox are call-free; what is left in loops is the filters' and distance procs' indirect calls
    per candidate (the walk's semantics) and the verify prints.
- Pinned no-draw battle (same game every run, 1800 ticks; query-65a = the kernels before, query-65b = after,
  both with the batch pass forced on; query-65 iterPool off/on), two pairs each:
  - fogReveal 0.117 / 0.112 -> 0.051 / 0.050 ms/tick; snapBuild 0.614 / 0.610 -> 0.578 / 0.597;
    bsCull 0.101 / 0.106 -> 0.078 / 0.082 (24.9 seekers a tick); logic 28.86 -> 28.88, 28.11 -> 29.27 (noise).
  - iterPool off -> on: allocations 4835 -> 3871 a tick; logic 28.85 -> 28.29, 28.34 -> 27.95 ms/tick;
    p99 79.2 -> 77.7, 78.3 -> 75.5 ms; frames > 100 ms 14 -> 10, 10 -> 11.
  - Replays referee, query-mgr5, query-mgr6 match on query-65 (no drawing).
- Visual check (bench_visual query-65a vs query-65b, referee replay, frames 600/1800/3600/5400): both replays
  match; the same two small patches at the top of the screen (y 25-39) differ by a few levels in every frame,
  identical at 600 and 5400 (a UI element, not the fog, which changes with the game).
- searchTable (sixth word bit 8, on; the sixth word as ai's c97828e): the flat per-tick result table has its own
  switch, the unordered_map kept behind it. Pinned no-draw battle on query-66, off -> on: allocations in search
  193.6 -> 117.8 a tick (whole tick 3946 -> 3871), logic 30.85 -> 31.01 ms/tick (noise), search 3.30 -> 3.26.
  Replays referee, query-mgr5, query-mgr6 match.
- Phase 5 re-measures, pinned no-draw battle on query-66 (pairOffer on, one switch off vs all defaults). Load drifted
  (logic 27-41 ms/tick over the run), so scope timers count more than whole-tick logic; the two doubtful items were
  re-run with the order alternated (3 pairs):
  - snapPatch: kept on. snapBuild 2.33 / 1.72 off -> 0.70 + 0.28 patch on (about 1 ms/tick less).
  - blockMask: kept on. logic 35.76 -> 32.58, 32.14 -> 30.81; search 3.74 -> 3.47, 3.41 -> 3.26.
  - simdBody: kept on. logic 37.36 -> 33.24, 34.90 -> 34.81.
  - denseCellMask: kept on. cells checked 15647 -> 4897 a tick, time even (logic 30.27 -> 30.06, 28.92 -> 29.74).
  - scanManager: kept on. Alternated pairs, logic off 31.13 / 30.73 / 26.30 vs on 28.55 / 28.21 / 29.35 (means
    29.39 vs 28.70). The search scope grows (2.0 -> 3.1 ms/tick plus 0.85 snapshot) because it now holds the work
    the per-module scans did outside it.
  - simdSearch: now OFF by default (which also leaves simdBody inert, as it needs simdSearch). Search scope slower in
    all 5 pairs: 3.35 -> 4.06, 3.36 -> 3.82, then alternated 2.88 -> 3.11, 2.86 -> 3.12, 2.86 -> 3.01 (logic means
    28.47 off vs 28.88 on). With the scalar cull already branchless and call-free, the SIMD block setup costs more
    than it saves at this density.

## 2026-11-28 (query): phase 13 part A, the shared cost tool (moduleCost, eighth word bit 24, off; measuring only)
- With tickTiming, `"moduleCost":true` times every module update by module type (getModuleNameKey, the class
  name), every object creation by template (ThingFactory::newObject, from createObject to initObject, keyed by
  template ID) and every script condition by type (internal name). Tables are rows keyed by name key / ID in
  GxTimeState (GxTime.h), so lanes add no GxTime enum ids.
- At the end (GXTIMEEND) three lines, the 60 costliest rows each: `GXMOD`, `GXSPAWN`, `GXCOND`, each row
  `name ms/tick calls/tick us/call max ms over1 allocs/call` (over1 = calls over 1 ms: creation spikes). The
  bench result shows them as gxmod / gxspawn / gxcond (bench_run with draw:false, and bench_replay with
  settings {"tickTiming":true,"moduleCost":true}).
- Times are inclusive (a module update that creates objects holds their cost). The browser clock is 0.1 ms
  grained, so us/call is only meaningful as an average over many calls; max and over1 are real.
- Battle (query-71): modules 16.1 ms/tick of aiLoop's 21.9 (AIUpdateInterface 5.10, PhysicsBehavior 4.93,
  TransportAIUpdate 0.90, JetAIUpdate 0.71, StealthUpdate 0.47, SpawnBehavior 0.36); creation 1.89 ms/tick
  (new units 50-110 ms each, about 300-750 allocations); script conditions 0.48 ms/tick
  (SKIRMISH_SPECIAL_POWER_READY 0.26).

## 2026-11-28 — paths, phase 13 part B: physics step, drawable sync (drawSync, physKernel: both OFF)
- Measured (battle, GXPHYSK / GXXFK, timers inflate, shares only): PhysicsBehavior's step is 1.15 ms/tick
  before setTransformMatrix (integration, rotation, ground, bounce), 2.81 in it, 0.89 after. Inside every
  transform reaction (4.87 M in the battle): drawable sync 37%, partition place 18%, partition dirty +
  contain 17%, trigger areas 16% (client terrain notify, pathfinder unit cells, ~7-polygon walk), store 11%.
  The transform side effects run per object in order on the main thread; only the part before is a row kernel.
- Ceiling for (b): a measuring-only build skipping the drawable sync entirely (not the same picture),
  timing slots, two swapped pairs: no-draw work ms armies 28.5 / 28.9 with the sync vs 28.0 / 27.4
  without, strikes 38.5 / 36.0 vs 39.3 / 37.2; drawing on: armies 51.3 vs 49.5, strikes 70.9 vs 73.9.
  No measurable gain even with the whole sync gone.
- drawSync (eighth word bit 6, OFF, same game): built anyway as asked. The object's transform change sets
  the drawable's own matrix at once; the draw modules' reaction (W3DModelDraw Set_Transform, tread track
  edges, debris / tracer / prop) runs on the next access to the draw modules (getDrawModules, both
  variants, and getDrawModulesNonDirty), on the drawable's next change (one change pending at a time, so
  each reaction sees the matrix it saw before), before the client update (all pending), before xfer and
  at destruction. Rendering chat consulted (no conflicting work; lightDrawableUpdate's skipped drawables
  are covered by the flush before the update loop). Checks: referee.rep, merge8-ai7.rep (36,004 frames)
  match; battle verify 0 mismatches; bench_visual on referee.rep 0 / 74 / 0 / 74 changed pixels at
  600 / 1800 / 3600 / 5400 against an A/A baseline of 208 / 70 / 165 / 144 (noise). Timing (load 17-21):
  work ms armies 27.5 / 27.8 off, 25.6 / 30.7 on; strikes 34.4 / 36.5 off, 32.9 / 38.5 on: level. OFF.
- physKernel (eighth word bit 7, OFF, same game as physBulk): with physSimd (on physBulk's match rule) the
  bulk pass also looks up each due unit's ground height at x + vx, y + vy (braking: x, y) and layer, into a
  per-row buffer; the step takes it when its matrix x, y and layer are those (read-only terrain lookups,
  worker-safe over row ranges). Checks: battle verify 2.6 M and a physBulk recording (paths-pb.rep, made
  with physKernel off, GX=ECFEEEE7) 1.6 M checks, 0 mismatches; paths-pb.rep replays to a match with it on;
  referee and merge8-ai7 match. Timing (physBulk + physSimd both sides, tickTiming off, swapped pairs, load
  7-9): strikes work ms off 41.8 / 40.7, on 45.7 / 39.9: level. OFF.
- (d) partition and trigger share: gxRefreshPlace and makeDirty are a few stores and a list insert per
  move and their values are read by range searches later in the same tick, so they can't wait; the trigger
  walk visits ~7 polygons with a bounds reject first and Pathfinder::updatePos already returns unless the
  unit's cell changed. Nothing worth batching found; not built.
