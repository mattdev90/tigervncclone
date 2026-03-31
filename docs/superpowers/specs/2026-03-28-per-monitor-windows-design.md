# Per-Monitor Windows Design

**Date:** 2026-03-28
**Status:** Approved — ready for implementation planning

---

## Problem

When connecting to a multi-monitor host (e.g. laptop with main screen + virtual monitor), TigerVNC shows the entire combined framebuffer in one window. The user cannot easily show each host monitor independently on a specific client monitor, nor switch which host monitor a given window is displaying without going into deep settings.

---

## Goal

- Each `DesktopWindow` can be assigned to show a specific host monitor (a sub-region of the combined framebuffer)
- A keyboard shortcut cycles the focused window through all available host monitors
- Profile config controls how many windows open on connect and which host monitor each starts on
- No persistent UI badge or indicator needed — user knows which monitor they're looking at

---

## Scope

**In scope:**
- Viewport sub-region cropping (render one host monitor's rect)
- Per-window `selectedRemoteMonitor` index with shortcut cycling
- Profile parameter for initial window→monitor assignments
- ProfileEditDialog field for configuring assignments
- Window resizes to match selected host monitor's resolution on cycle

**Out of scope:**
- Dragging windows between client monitors automatically
- Visual overlay/badge showing current monitor name
- Any changes to the RFB protocol or server side

---

## Architecture

### Data source

The VNC server sends each remote monitor's geometry via `ExtendedDesktopSize` as an `rfb::ScreenSet`. Each `rfb::Screen` contains a `dimensions` rect (x, y, w, h within the combined framebuffer). `CConn` already receives this — it just needs to be forwarded to `DesktopWindow`.

### Viewport cropping

`Viewport` currently renders the full framebuffer starting at offset (0, 0). A new method `setRemoteRegion(int x, int y, int w, int h)` tells it to:
- Source pixel data starting at (x, y) within the framebuffer
- Treat (w, h) as the logical size of the remote desktop for this window
- Offset all outgoing mouse/keyboard events by (x, y) so they land correctly on the host

### DesktopWindow changes

New members:
- `rfb::ScreenSet remoteScreens` — updated whenever `CConn` receives a new `ScreenSet`
- `int selectedRemoteMonitor` — index into `remoteScreens` (default: 0)

New method `cycleRemoteMonitor()`:
1. Increment `selectedRemoteMonitor = (selectedRemoteMonitor + 1) % remoteScreens.size()`
2. Call `viewport->setRemoteRegion(...)` with the new screen's rect
3. Resize the window to match the new screen's dimensions
4. If the new dimensions push the window off the current client monitor, snap it to the top-left corner of the client monitor it currently occupies

### CConn changes

- On `setExtendedDesktopSize()` / `setDesktopSize()`: call `desktop->setRemoteScreens(screens)`
- On connect: read `monitorWindowAssignments` from profile. For each entry, create a `DesktopWindow` with that initial `selectedRemoteMonitor` index. If the list is empty, create one window starting at index 0 (existing behaviour).

### Keyboard shortcut

New shortcut: `Ctrl+Shift+Alt+−` (minus/hyphen key)
Behaviour: calls `cycleRemoteMonitor()` on the currently focused `DesktopWindow`.
If `remoteScreens` is empty (server doesn't support `ExtendedDesktopSize`), the shortcut does nothing.

Registered in the existing shortcut infrastructure (`ShortcutHandler` / `DesktopWindow::handle()`).

### Profile parameters

New parameter in `parameters.cxx/.h`:

```
monitorWindowAssignments   IntList   default: empty
```

Semantics: comma-separated list of remote monitor indices (1-based, matching existing `fullScreenSelectedMonitors` conventions). Each entry = one window to open on connect, starting on that remote monitor. Converted to 0-based internally when indexing into `remoteScreens`. Empty = one window starting on remote monitor 1 (index 0 internally).

Example values:
- `""` → one window, shows remote monitor 1
- `"1,2"` → two windows, first on remote monitor 1, second on remote monitor 2
- `"2,1"` → two windows, first on remote monitor 2, second on remote monitor 1

### ProfileEditDialog

Add a text input field: **"Monitor windows (e.g. 1,2)"**
Bound to `monitorWindowAssignments`. Small help label: "One window per entry. Press Ctrl+Shift+Alt+− to cycle a window between monitors."

---

## Edge Cases

| Situation | Behaviour |
|-----------|-----------|
| Server doesn't support `ExtendedDesktopSize` | `remoteScreens` stays empty; shortcut is a no-op; all windows show full framebuffer |
| Profile assigns index 3 but server only has 2 monitors | Clamp to `index % remoteScreens.size()` on first `ScreenSet` received |
| Server reconfigures monitors while connected | Update `remoteScreens`; if `selectedRemoteMonitor` is now out of range, clamp to last valid index |
| Two windows assigned the same remote monitor | Both show the same sub-region — allowed, no special handling |

---

## Files Changed

| File | Change |
|------|--------|
| `vncviewer/Viewport.cxx/.h` | Add `setRemoteRegion(x, y, w, h)`; offset rendering and input events |
| `vncviewer/DesktopWindow.cxx/.h` | Add `remoteScreens`, `selectedRemoteMonitor`, `cycleRemoteMonitor()`, `setRemoteScreens()` |
| `vncviewer/CConn.cxx/.h` | Forward `ScreenSet` to window; create multiple windows from profile config on connect |
| `vncviewer/parameters.cxx/.h` | Add `monitorWindowAssignments` parameter |
| `vncviewer/ProfileEditDialog.cxx/.h` | Add monitor assignment input field |
| Keyboard handling (`DesktopWindow::handle()`) | Register `Ctrl+Shift+Alt+−` → `cycleRemoteMonitor()` |
