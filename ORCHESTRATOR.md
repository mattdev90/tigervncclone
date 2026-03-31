# ORCHESTRATOR.md — Living Repo Context

## Architecture Summary

TigerVNC is a C++ VNC viewer and server suite. The relevant component is **vncviewer** — a desktop VNC client built on FLTK (Fast Light Toolkit) for UI.

**Entry point:** `vncviewer/vncviewer.cxx` → calls `loadViewerParameters(nullptr)` to load defaults, then `ServerDialog::run()` to show the connection dialog, then creates `CConn` to handle the actual VNC connection.

**Connection flow:**
```
vncviewer.cxx main()
  → loadViewerParameters(nullptr)    // Load defaults (registry on Win, file on Linux)
  → ServerDialog::run()              // Show connection dialog, user picks server
  → CConn::CConn()                   // Establish connection
    → CConn::getUserPasswd()         // Called by rfb layer when auth needed
      → AuthDialog                   // Prompt user for password
      → saveProfile() if wantSave    // Persist password to profile
```

**Key modules:**
- `vncviewer/ServerDialog.cxx/h` — Connection dialog (being rebuilt)
- `vncviewer/parameters.cxx/h` — .tigervnc file I/O and profile management
- `vncviewer/AuthDialog.cxx/h` — Password prompt dialog
- `vncviewer/CConn.cxx/h` — VNC connection handler
- `vncviewer/OptionsDialog.cxx/h` — Settings dialog (NOT being touched)
- `common/rfb/obfuscate.h/cxx` — `rfb::obfuscate()` / `rfb::deobfuscate()` for passwords
- `common/core/xdgdirs.h` — `core::getvncconfigdir()` returns config directory path
- `vncviewer/fltk/layout.h` — FLTK layout macros: `OUTER_MARGIN=15`, `INNER_MARGIN=10`, `BUTTON_WIDTH=115`, `BUTTON_HEIGHT=27`, `INPUT_HEIGHT=25`

## Conventions

- **FLTK UI:** All widgets use FLTK (Fl_Window, Fl_Button, Fl_Input, etc.). No Qt, no native dialogs.
- **i18n:** All user-facing strings wrapped in `_("...")` macro from `"i18n.h"`.
- **Error handling:** `core::posix_error(msg, errno)` for system errors, `std::runtime_error` for others.
- **Platform guards:** `#ifdef _WIN32` for Windows-specific code. Registry on Windows, files on Linux/macOS.
- **File format:** `.tigervnc` files start with `"TigerVNC Configuration file Version 1.0"` header, then `Key=Value` lines. Values are escape-encoded (helper: `encodeValue()` / `decodeValue()` in parameters.cxx — these are **static** functions, not exported).
- **Logging:** `static core::LogWriter vlog("ClassName")` then `vlog.error(...)`, `vlog.info(...)`.
- **Tests:** Google Test. Tests link against `core`, `rfb`, `network` libraries. Test files live in `tests/unit/`. CMakeLists.txt there adds `add_executable` + `gtest_discover_tests`.
- **`VNCSERVERNAMELEN = 256`** — max server name buffer size, defined in `vncviewer/vncviewer.h`.
- **Build system:** CMake. Build from `build/` directory. Target: `cmake --build . --target vncviewer`.

## Key Dependencies

| Library | Purpose |
|---|---|
| FLTK | UI framework — all dialogs, widgets, event loop |
| rfb (common/rfb) | VNC protocol: `rfb::obfuscate()`, `rfb::deobfuscate()`, security types |
| core (common/core) | Configuration, logging, xdg dirs, platform utils |
| network (common/network) | `network::getHostAndPort()` — parses "host:port" strings |
| GTest | Unit testing |
| Intl/gettext | Internationalization (`_()` macro) |

## Known Fragile Areas

- **`encodeValue()` / `decodeValue()`** — These are `static` functions inside `parameters.cxx`. They are NOT exported. Subagents writing profile I/O in `parameters.cxx` can call them directly. Do NOT try to call them from other files.
- **Windows registry vs file storage** — On Windows, `loadViewerParameters(nullptr)` reads from registry. On Linux/macOS it reads from `default.tigervnc`. The new profile system uses **files on all platforms** (in the profiles subdir) for portability. The `#ifdef _WIN32` path in `saveViewerParameters` / `loadViewerParameters` should NOT be changed for the default-config path — only the new profile functions use files cross-platform.
- **FLTK column_widths** — `Fl_Hold_Browser::column_widths()` takes a pointer to an int array that must end with 0. The array must remain valid for the lifetime of the widget. Use a static or member array.
- **`Fl::event_clicks()`** — Returns number of clicks. Check `> 0` not `== 2` for double-click (some systems fire event_clicks=1 on double-click).
- **`fl_utf8towc` / `fl_utf8fromwc`** — FLTK UTF-8 helpers needed on Windows for wide-char file paths. Already included via `<FL/fl_utf8.h>` which is included by many FLTK headers.
- **`network::getHostAndPort()`** — Can throw on malformed input. Always wrap in try/catch.
- **Authentication flow** — `CConn::getUserPasswd()` is called by the rfb protocol layer (not directly by UI). It must handle: (1) password file, (2) saved profile password, (3) prompt via AuthDialog. Order matters.
- **`activeProfilePath` must be set BEFORE `CConn` is created** — The ServerDialog sets `CConn::activeProfilePath` before hiding and returning control to vncviewer.cxx which creates CConn.

## Decisions Made

1. **Profile storage:** Individual `.tigervnc` files in `{configDir}/profiles/` on all platforms (not registry on Windows) for portability.
2. **Password format:** `Password=<hex>` field in .tigervnc — hex-encoded obfuscated bytes using `rfb::obfuscate()` (DES-based, standard VNC obfuscation).
3. **Auto-save on connect:** Every successful connection auto-creates/updates a profile in the profiles dir.
4. **Replace history:** Old server history (registry on Windows, `tigervnc.history` on Linux) is replaced by profiles. One-time migration runs on first launch if profiles dir is empty.
5. **AuthDialog checkbox:** "Save password" (not "Keep for reconnect") — always shown when password is needed, not gated on `reconnectOnError`.
6. **CConn persistence:** `CConn::savedPassword`/`savedUsername` static vars removed. Replaced by `CConn::activeProfilePath` which points to the current profile file. Password loaded from file on reconnect.
7. **Double-click = immediate connect** from profile list. Single-click populates host field.
8. **Approach A:** Evolve existing ServerDialog class — same class name, same entry points, rebuilt internals.

## Current State

**PLANNED — not yet implemented.**

Implementation plan: `docs/superpowers/plans/2026-03-27-profile-based-server-dialog.md`
Design spec: `docs/superpowers/specs/2026-03-27-profile-based-server-dialog-design.md`

### Tasks

| # | Task | Status |
|---|---|---|
| 1 | Profile data structures + filename/password utilities + unit tests | DONE |
| 2 | Profile save/load/scan/delete I/O + unit tests | DONE |
| 3 | AuthDialog "Save password" checkbox | DONE |
| 4 | CConn profile-based password persistence | DONE |
| 5 | Rebuilt ServerDialog with profile list + one-click connect | DONE |
| 6 | vncviewer.cxx startup: profiles dir creation + history migration | DONE |
| 7 | Integration verification + dead code cleanup | DONE |

## Implementation Complete — Needs Build Verification

All code changes are done. The implementation has NOT been compiled yet.
Next step: build the project and fix any compilation errors.

### Known Risks / Things to Check at Build Time
- `parameters.cxx`: `saveProfile()` calls `network::getHostAndPort()` — needs the `network` lib linked (it is, via CConn, but check if the unit test binary links it too)
- `tests/unit/profile.cxx`: The `ProfileIOTest` class uses `mkdtemp` which is POSIX-only; guarded with `#ifndef _WIN32`
- `ServerDialog.cxx`: `column_widths(new int[3]{50, 200, 0})` — the int array is heap-allocated and leaked intentionally (browser lifetime = window lifetime); not a functional bug
- `CConn.cxx`: After the changes, if a server uses username+password auth, the username is NOT loaded from a profile (only password is). Username will always be prompted. This is by design for now.
- `loadHistoryFromRegKey()` still exists for the migration path — do NOT remove it

## Subagent Execution Notes

- Tasks 1 and 3 can run in **parallel** (no shared files).
- Task 2 depends on Task 1 (needs declarations from parameters.h).
- Task 4 depends on Task 3 (needs `getSavePassword()` in AuthDialog).
- Task 5 depends on Tasks 1, 2, 4 (needs all profile functions and CConn changes).
- Task 6 depends on Task 2 (needs `getProfilesDir()`, `loadAllProfiles()`, etc.).
- Task 7 depends on all others.
