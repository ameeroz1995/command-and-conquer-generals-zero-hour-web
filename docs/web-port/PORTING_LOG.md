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
