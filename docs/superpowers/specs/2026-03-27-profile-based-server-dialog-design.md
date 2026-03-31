# TigerVNC Profile-Based Server Dialog — Design Spec

**Date:** 2026-03-27
**Status:** Approved
**Approach:** Evolve existing ServerDialog (Approach A)

---

## Problem

The current TigerVNC connection dialog has three key failures:

1. **No password persistence.** The "Keep password for reconnect" checkbox only stores the password in RAM for auto-reconnect. There is no way to save a password with a connection.
2. **No profile management.** Settings are saved globally (one default config). The "Save As" / "Load" buttons require navigating FLTK's basic file chooser every time. There's no way to quickly switch between saved connections.
3. **Server history is shallow.** It stores only hostnames (last 20), with no associated settings or passwords.

## Solution

Replace the current ServerDialog with a profile-based connection dialog. Profiles are auto-saved on successful connect, stored as portable `.tigervnc` files, and presented in a list for one-click access.

---

## Dialog Layout

```
+-- TigerVNC ------------------------------------------------+
|                                                             |
|  VNC server: [_________________________]  [Connect]         |
|                                                             |
|  +- Saved Profiles -------------------------------------+   |
|  |  [lock] Office PC            192.168.1.50:5900       |   |
|  |  [lock] Home Server          10.0.0.5                |   |
|  |  [    ] Lab Machine          172.16.0.100            |   |
|  |                                                      |   |
|  +------------------------------------------------------+   |
|                                                             |
|  [Options...]  [Edit]  [Delete]                 [About...]  |
+-------------------------------------------------------------+
```

### Behavior

- **Host input + Connect:** Top bar for typing a new server or connecting to a known one.
- **Profile list:** `Fl_Hold_Browser` showing all saved profiles. Each entry displays a password indicator, nickname (or server address if no nickname), and server address.
- **Double-click a profile:** Immediately connects using saved settings and password.
- **Single-click a profile:** Selects it (populates host field, enables Edit/Delete buttons).
- **Edit button:** Opens a small edit dialog for nickname and password management.
- **Delete button:** Removes the profile file after confirmation.
- **Typing in host field:** Filters the profile list to matching entries.
- **Exact match on connect:** If the typed host matches an existing profile, that profile's settings and saved password are used automatically.

---

## Profile Storage

### Location

Profiles are stored as individual `.tigervnc` files in a `profiles` subdirectory of the VNC config directory:

- **Linux/macOS:** `~/.config/tigervnc/profiles/`
- **Windows:** `%APPDATA%\TigerVNC\profiles\` (via `core::getvncconfigdir()` + `/profiles/`)

### File Format

Extends the existing `.tigervnc` format (key=value, first line is identifier string). Two new fields:

```
TigerVNC Configuration file Version 1.0

ServerName=192.168.1.50:5900
ProfileName=Office PC
Password=4132ba5c0839d7ee
AutoSelect=1
FullColor=1
...
```

| Field | Description |
|---|---|
| `ProfileName` | User-friendly nickname. Optional — falls back to `ServerName` if absent. |
| `Password` | Hex-encoded obfuscated password using `rfb::obfuscate()`. Optional — if absent, AuthDialog will prompt. |

All other fields are identical to the current `.tigervnc` format and use the existing `parameterArray` for serialization.

### Filename Convention

Derived from server address: `<host>_<port>.tigervnc`

- `192.168.1.50:5900` → `192.168.1.50_5900.tigervnc`
- `myserver.local` → `myserver.local_5900.tigervnc` (default port appended)
- Characters invalid in filenames are replaced with `_`.

### Portability

Plain text files. Users can copy them between devices, edit in a text editor, or sync via cloud storage.

---

## Connection Flows

### First-time connection

1. User types host in input field, clicks Connect (or presses Enter).
2. Connection initiates. AuthDialog prompts for credentials as needed.
3. AuthDialog shows a **"Save password"** checkbox (replaces the old "Keep password for reconnect").
4. On successful connect, a profile `.tigervnc` file is auto-saved to the profiles directory.
5. If "Save password" was checked, the obfuscated password is included in the profile.

### Returning with saved password

1. User opens TigerVNC, sees profile in list.
2. Double-click → connects immediately using saved settings and password. No prompts.

### Returning without saved password

1. Double-click profile → connects, AuthDialog prompts for password.
2. User can check "Save password" to persist it going forward.
3. Profile is updated with the password on successful connect.

### Editing a profile

1. Select profile in list, click Edit.
2. Small dialog allows editing: nickname, toggling saved password (clear it), viewing server address.
3. Connection settings (encoding, fullscreen, etc.) are managed through the existing Options dialog and saved into the active profile.

### Host input matches existing profile

1. As user types, profile list filters to show matches.
2. On Connect, if typed host exactly matches a saved profile, that profile's settings and password are loaded automatically.

---

## Code Changes

### Modified Files

| File | Changes |
|---|---|
| `vncviewer/ServerDialog.h` | Rebuild class: add `Fl_Hold_Browser` for profile list, `Fl_Input` for host, profile management methods (`loadProfiles`, `saveProfile`, `deleteProfile`, `filterProfiles`), edit/delete button handlers |
| `vncviewer/ServerDialog.cxx` | Full rewrite of dialog layout and behavior. Remove `Fl_Suggestion_Input`, `Fl_File_Chooser`, server history methods. Add profile directory scanning, profile list population, double-click-to-connect, host field filtering |
| `vncviewer/parameters.h` | Add `saveProfile()` and `loadProfile()` declarations. Add password read/write support to save/load functions. Add `getProfilesDir()` helper |
| `vncviewer/parameters.cxx` | Implement profile-aware save/load. Add `ProfileName` and `Password` fields to file I/O. Add `getProfilesDir()` using `core::getvncconfigdir()`. On Windows: profiles stored as files (not registry) for portability |
| `vncviewer/AuthDialog.h` | Rename `keepPasswdCheckbox` to `savePasswdCheckbox`. Add method to check if user wants password saved |
| `vncviewer/AuthDialog.cxx` | Change checkbox label from "Keep password for reconnect" to "Save password". Show checkbox unconditionally when password is needed (not gated on `reconnectOnError`) |
| `vncviewer/CConn.cxx` | After successful auth with "Save password" checked, trigger profile update with password. Replace `savedPassword`/`savedUsername` static vars with profile-based persistence |
| `vncviewer/vncviewer.cxx` | Update startup flow: load default profile instead of registry/default.tigervnc. Ensure profiles directory is created on first run |

### New Files

None. All changes fit into existing files.

### Removed Functionality

| What | Why |
|---|---|
| `ServerDialog::loadServerHistory()` / `saveServerHistory()` | Replaced by profile directory scanning |
| `Fl_Suggestion_Input` usage in ServerDialog | Replaced by `Fl_Hold_Browser` + `Fl_Input` |
| `loadHistoryFromRegKey()` / `saveHistoryToRegKey()` (Windows) | Profiles replace registry-based history |
| `Fl_File_Chooser` usage in ServerDialog | No more file browser — profiles are listed directly |
| `SERVER_HISTORY` file / constant | No longer needed |

### Not Touched

| What | Why |
|---|---|
| `OptionsDialog` | Stays as-is. Future changes planned separately. |
| `Fl_Suggestion_Input` widget code | Still exists in `fltk/`, just not used by ServerDialog anymore |
| Core libraries (rfb, network, rdr, core) | No changes needed |
| `vncviewer/CMakeLists.txt` | No new files to add |

---

## Reconnect Behavior

When `reconnectOnError` is enabled and a connection drops, the current code uses in-memory `CConn::savedPassword` / `CConn::savedUsername` to auto-reconnect without prompting. With profiles, this changes:

- On reconnect, CConn reads the password from the active profile file instead of a static variable.
- If the profile has a saved password, reconnect is seamless (no prompt).
- If the profile has no saved password, AuthDialog prompts as before.
- The `CConn::savedPassword` / `CConn::savedUsername` static variables are removed.

## Password Security

Passwords are obfuscated using the existing `rfb::obfuscate()` function (DES-based, standard VNC obfuscation). This is the same mechanism used by `vncpasswd` and password file authentication throughout TigerVNC.

This is **not encryption** — it prevents casual reading of passwords in config files but is not resistant to determined attacks. This is the VNC standard and matches what every other VNC implementation does.

The obfuscated password is stored as a hex string in the `.tigervnc` profile file.

---

## Migration

On first run with the new code:

1. Create `profiles/` directory in the VNC config dir if it doesn't exist.
2. If a `default.tigervnc` (Linux/macOS) or registry config (Windows) exists with a `ServerName`, create a profile from it so the user's last connection appears in the list.
3. If server history entries exist (history file on Linux, registry on Windows), create stub profiles (host only, no password) for each entry.
4. Old history files/registry keys are left in place (not deleted) for backwards compatibility but are no longer read after migration.
