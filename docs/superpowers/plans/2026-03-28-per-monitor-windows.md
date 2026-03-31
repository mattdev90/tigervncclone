# Per-Monitor Windows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Each DesktopWindow can be assigned to a specific host monitor sub-region; a keyboard shortcut (Ctrl+Shift+Alt+−) cycles the focused window through available host monitors.

**Architecture:** Add a `remoteRegion` (core::Rect) to Viewport that offsets draw calls and pointer event coordinates to a sub-region of the framebuffer. DesktopWindow stores the rfb::ScreenSet received from the server and cycles a `selectedRemoteMonitor` index on shortcut. CConn forwards the ScreenSet and, when `monitorWindowAssignments` is set in the profile, spawns multiple DesktopWindows on connect.

**Tech Stack:** C++17, FLTK 1.3, RFB protocol (rfb::ScreenSet / ExtendedDesktopSize), core::Rect, GTest

---

## File Map

| File | Change |
|------|--------|
| `vncviewer/Viewport.h` | Add `remoteRegion`, `settingRemoteRegion` flag, `setRemoteRegion()` |
| `vncviewer/Viewport.cxx` | Offset draw src coords, offset pointer events, guard resize |
| `vncviewer/DesktopWindow.h` | Add `remoteScreens`, `selectedRemoteMonitor`, `setRemoteScreens()`, `cycleRemoteMonitor()` |
| `vncviewer/DesktopWindow.cxx` | Implement cycle logic, window resize-to-monitor, snap-to-client-monitor |
| `vncviewer/CConn.h` | Add `cycleMonitor()`, change `desktop` to vector for multi-window |
| `vncviewer/CConn.cxx` | Forward ScreenSet in `setExtendedDesktopSize`, spawn N windows in `initDone` |
| `vncviewer/parameters.h` | Declare `monitorWindowAssignments` |
| `vncviewer/parameters.cxx` | Define `monitorWindowAssignments` as `core::StringParameter` |
| `vncviewer/ProfileEditDialog.h/.cxx` | Add monitor assignments input field |
| `tests/unit/profile.cxx` | Add parameter-parsing tests (already exists, extend it) |

---

## Task 1: Add monitorWindowAssignments parameter

**Files:**
- Modify: `vncviewer/parameters.h`
- Modify: `vncviewer/parameters.cxx`
- Modify: `tests/unit/profile.cxx`

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/profile.cxx`:

```cpp
#include "parameters.h"
#include <vector>

// Helper tested in this task
static std::vector<int> parseMonitorAssignments(const std::string& s)
{
  std::vector<int> result;
  if (s.empty()) return result;
  std::istringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    try {
      int v = std::stoi(token);
      if (v >= 1) result.push_back(v);
    } catch (...) {}
  }
  return result;
}

TEST(MonitorAssignments, EmptyStringGivesEmptyList)
{
  EXPECT_TRUE(parseMonitorAssignments("").empty());
}

TEST(MonitorAssignments, SingleMonitor)
{
  auto r = parseMonitorAssignments("1");
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], 1);
}

TEST(MonitorAssignments, TwoMonitors)
{
  auto r = parseMonitorAssignments("1,2");
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0], 1);
  EXPECT_EQ(r[1], 2);
}

TEST(MonitorAssignments, ReversedOrder)
{
  auto r = parseMonitorAssignments("2,1");
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0], 2);
  EXPECT_EQ(r[1], 1);
}

TEST(MonitorAssignments, InvalidTokensIgnored)
{
  auto r = parseMonitorAssignments("1,abc,2");
  ASSERT_EQ(r.size(), 2u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run in UCRT64: `cd /d/Coding/Tools/tigervnc/build && mingw32-make profile -j$(nproc) && ./tests/unit/profile.exe --gtest_filter="MonitorAssignments*"`
Expected: FAIL — `parseMonitorAssignments` not defined

- [ ] **Step 3: Add parameter declaration to parameters.h**

Add after the `fullScreenSelectedMonitors` extern:

```cpp
// vncviewer/parameters.h
extern core::StringParameter monitorWindowAssignments;
```

Also add the helper declaration:
```cpp
// Returns 0-based remote monitor indices from monitorWindowAssignments string.
// Input is 1-based comma-separated (e.g. "1,2"). Empty = [{0}] (single window, monitor 0).
std::vector<int> getMonitorWindowAssignments();
```

Add `#include <vector>` and `#include <string>` to the includes block if not already present.

- [ ] **Step 4: Add parameter definition to parameters.cxx**

Add near the other fullscreen parameters:

```cpp
core::StringParameter
  monitorWindowAssignments("MonitorWindowAssignments",
                           "Remote monitor indices to open windows for on connect "
                           "(1-based, comma-separated, e.g. \"1,2\"). "
                           "Empty = one window showing monitor 1.",
                           "");
```

Add the helper implementation in parameters.cxx:

```cpp
std::vector<int> getMonitorWindowAssignments()
{
  std::string s = monitorWindowAssignments;
  std::vector<int> result;
  if (s.empty()) {
    result.push_back(0);   // 0-based: remote monitor 0
    return result;
  }
  std::istringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    try {
      int v = std::stoi(token);
      if (v >= 1)
        result.push_back(v - 1);  // convert to 0-based
    } catch (...) {
      vlog.error(_("Invalid monitor index in MonitorWindowAssignments: %s"),
                 token.c_str());
    }
  }
  if (result.empty())
    result.push_back(0);
  return result;
}
```

Add `#include <sstream>` and `#include <vector>` to parameters.cxx includes if not already present.

- [ ] **Step 5: Update test to use the real header**

Replace the inline `parseMonitorAssignments` helper in the test with a call to `getMonitorWindowAssignments()` after setting `monitorWindowAssignments`. Update the tests:

```cpp
TEST(MonitorAssignments, EmptyStringGivesDefaultSingleMonitor)
{
  monitorWindowAssignments.setParam("");
  auto r = getMonitorWindowAssignments();
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], 0);   // 0-based index 0
}

TEST(MonitorAssignments, SingleMonitorConverts)
{
  monitorWindowAssignments.setParam("1");
  auto r = getMonitorWindowAssignments();
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], 0);   // 1-based "1" → 0-based 0
}

TEST(MonitorAssignments, TwoMonitorsConverted)
{
  monitorWindowAssignments.setParam("1,2");
  auto r = getMonitorWindowAssignments();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0], 0);
  EXPECT_EQ(r[1], 1);
}

TEST(MonitorAssignments, ReversedOrderPreserved)
{
  monitorWindowAssignments.setParam("2,1");
  auto r = getMonitorWindowAssignments();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0], 1);
  EXPECT_EQ(r[1], 0);
}
```

- [ ] **Step 6: Run tests to verify they pass**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make profile -j$(nproc) && ./tests/unit/profile.exe --gtest_filter="MonitorAssignments*"`
Expected: all PASS

- [ ] **Step 7: Commit**

```bash
git add vncviewer/parameters.h vncviewer/parameters.cxx tests/unit/profile.cxx
git commit -m "Add monitorWindowAssignments parameter with 1-to-0-based conversion"
```

---

## Task 2: Viewport remote region

Add a `remoteRegion` to Viewport so `draw()` sources pixels from a sub-rect of the framebuffer and pointer events are offset to the correct remote coordinates.

**Files:**
- Modify: `vncviewer/Viewport.h`
- Modify: `vncviewer/Viewport.cxx`

- [ ] **Step 1: Add members and declaration to Viewport.h**

In the `private:` section of `Viewport`, add:

```cpp
// When set, draw only this sub-region of the remote framebuffer.
// remoteRegion is in full framebuffer coordinates.
core::Rect remoteRegion;
bool settingRemoteRegion;  // guard: suppresses framebuffer recreate during region change
```

In the `public:` section, add:

```cpp
// Restrict rendering to a sub-region of the remote framebuffer.
// Pass an empty rect to show the full framebuffer.
void setRemoteRegion(const core::Rect& r);
```

- [ ] **Step 2: Implement setRemoteRegion in Viewport.cxx**

Add after the `Viewport::setCursor()` definition:

```cpp
void Viewport::setRemoteRegion(const core::Rect& r)
{
  remoteRegion = r;
  if (!r.is_empty()) {
    settingRemoteRegion = true;
    Fl_Widget::resize(x(), y(), r.width(), r.height());
    settingRemoteRegion = false;
  }
  damage(FL_DAMAGE_ALL);
}
```

- [ ] **Step 3: Guard framebuffer recreation in Viewport::resize()**

Find the block in `Viewport::resize()` that checks:
```cpp
if ((w != frameBuffer->width()) || (h != frameBuffer->height())) {
```

Wrap it with the guard:
```cpp
if (!settingRemoteRegion &&
    ((w != frameBuffer->width()) || (h != frameBuffer->height()))) {
```

- [ ] **Step 4: Offset draw source in Viewport::draw()**

Find the line in `Viewport::draw()` (the no-argument version):
```cpp
frameBuffer->draw(X - x(), Y - y(), X, Y, W, H);
```

Replace with:
```cpp
int srcOffsetX = remoteRegion.is_empty() ? 0 : remoteRegion.tl.x;
int srcOffsetY = remoteRegion.is_empty() ? 0 : remoteRegion.tl.y;
frameBuffer->draw(X - x() + srcOffsetX, Y - y() + srcOffsetY, X, Y, W, H);
```

- [ ] **Step 5: Offset draw source in Viewport::draw(Surface* dst)**

Find the analogous line in `Viewport::draw(Surface* dst)`:
```cpp
frameBuffer->draw(dst, X - x(), Y - y(), X, Y, W, H);
```

Replace with:
```cpp
int srcOffsetX = remoteRegion.is_empty() ? 0 : remoteRegion.tl.x;
int srcOffsetY = remoteRegion.is_empty() ? 0 : remoteRegion.tl.y;
frameBuffer->draw(dst, X - x() + srcOffsetX, Y - y() + srcOffsetY, X, Y, W, H);
```

- [ ] **Step 6: Offset pointer events**

In `Viewport::handlePointerEvent()`, find where the widget-relative mouse position is converted to remote desktop coordinates before being passed to `EmulateMB` or `sendPointerEvent`. It will look roughly like:

```cpp
core::Point remotePos(
  (int)floor((pos.x - x()) * ... ),
  (int)floor((pos.y - y()) * ... )
);
```

Add the region offset after the coordinate conversion:
```cpp
if (!remoteRegion.is_empty()) {
  remotePos.x += remoteRegion.tl.x;
  remotePos.y += remoteRegion.tl.y;
}
```

Note: The exact variable names depend on what you find in `handlePointerEvent`. The offset must be applied after any scaling but before the position is used.

- [ ] **Step 7: Initialise new members in constructor**

In `Viewport::Viewport(...)`, add to the initialiser list:
```cpp
remoteRegion(),       // default-constructed core::Rect is empty
settingRemoteRegion(false),
```

- [ ] **Step 8: Build and verify**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make vncviewer -j$(nproc)`
Expected: clean build with no errors.

Connect to a VNC server with a single monitor. Verify normal operation is unchanged.

- [ ] **Step 9: Commit**

```bash
git add vncviewer/Viewport.h vncviewer/Viewport.cxx
git commit -m "Add remote region crop to Viewport for per-monitor display"
```

---

## Task 3: DesktopWindow — store ScreenSet and cycle monitors

**Files:**
- Modify: `vncviewer/DesktopWindow.h`
- Modify: `vncviewer/DesktopWindow.cxx`

- [ ] **Step 1: Add members and declarations to DesktopWindow.h**

Add includes at the top of the header:
```cpp
#include <rfb/ScreenSet.h>
#include <vector>
```

Add to the `public:` section:
```cpp
// Called by CConn when the server sends its screen layout.
void setRemoteScreens(const rfb::ScreenSet& screens);

// Cycle the viewport to the next remote monitor. No-op if only one screen.
void cycleRemoteMonitor();
```

Add to the `private:` section:
```cpp
rfb::ScreenSet remoteScreens;
int selectedRemoteMonitor;  // 0-based index into sorted screen list

void applyRemoteMonitor();
static std::vector<rfb::Screen> sortedScreens(const rfb::ScreenSet& ss);
```

- [ ] **Step 2: Initialise new members in constructor**

In `DesktopWindow::DesktopWindow(...)`, add to initialiser list:
```cpp
selectedRemoteMonitor(0),
```

- [ ] **Step 3: Implement sortedScreens helper**

Add in DesktopWindow.cxx (before the public methods):

```cpp
// static
std::vector<rfb::Screen>
DesktopWindow::sortedScreens(const rfb::ScreenSet& ss)
{
  std::vector<rfb::Screen> v(ss.begin(), ss.end());
  std::sort(v.begin(), v.end(), [](const rfb::Screen& a, const rfb::Screen& b) {
    if (a.dimensions.tl.x != b.dimensions.tl.x)
      return a.dimensions.tl.x < b.dimensions.tl.x;
    return a.dimensions.tl.y < b.dimensions.tl.y;
  });
  return v;
}
```

Add `#include <algorithm>` if not already present.

- [ ] **Step 4: Implement setRemoteScreens**

```cpp
void DesktopWindow::setRemoteScreens(const rfb::ScreenSet& screens)
{
  remoteScreens = screens;
  // Clamp index in case the new layout has fewer screens.
  int count = (int)sortedScreens(remoteScreens).size();
  if (selectedRemoteMonitor >= count)
    selectedRemoteMonitor = 0;
  applyRemoteMonitor();
}
```

- [ ] **Step 5: Implement applyRemoteMonitor**

```cpp
void DesktopWindow::applyRemoteMonitor()
{
  auto screens = sortedScreens(remoteScreens);
  if (screens.empty())
    return;

  const rfb::Screen& screen = screens[selectedRemoteMonitor];
  viewport->setRemoteRegion(screen.dimensions);

  int newW = screen.dimensions.width();
  int newH = screen.dimensions.height();

  // Resize the window to match the selected monitor.
  // Keep the window snapped to the top-left of whichever client monitor
  // the window currently lives on.
  int clientX, clientY, clientW, clientH;
  Fl::screen_xywh(clientX, clientY, clientW, clientH,
                  x() + w() / 2, y() + h() / 2);

  // Clamp new position so window stays fully on that client monitor.
  int newX = std::max(clientX, std::min(x(), clientX + clientW  - newW));
  int newY = std::max(clientY, std::min(y(), clientY + clientH - newH));

  resize(newX, newY, newW, newH);
  repositionWidgets();
  redraw();
}
```

Add `#include <algorithm>` if not already present.

- [ ] **Step 6: Implement cycleRemoteMonitor**

```cpp
void DesktopWindow::cycleRemoteMonitor()
{
  auto screens = sortedScreens(remoteScreens);
  if ((int)screens.size() <= 1)
    return;
  selectedRemoteMonitor = (selectedRemoteMonitor + 1) % (int)screens.size();
  applyRemoteMonitor();
}
```

- [ ] **Step 7: Build and verify**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make vncviewer -j$(nproc)`
Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add vncviewer/DesktopWindow.h vncviewer/DesktopWindow.cxx
git commit -m "Add setRemoteScreens and cycleRemoteMonitor to DesktopWindow"
```

---

## Task 4: CConn — forward ScreenSet and expose cycleMonitor

**Files:**
- Modify: `vncviewer/CConn.h`
- Modify: `vncviewer/CConn.cxx`

- [ ] **Step 1: Add cycleMonitor declaration to CConn.h**

In the `public:` section:
```cpp
// Called from Viewport keyboard shortcut to cycle the active window's monitor.
void cycleMonitor();
```

- [ ] **Step 2: Implement cycleMonitor in CConn.cxx**

```cpp
void CConn::cycleMonitor()
{
  if (desktop)
    desktop->cycleRemoteMonitor();
}
```

- [ ] **Step 3: Forward ScreenSet in setExtendedDesktopSize**

Find `CConn::setExtendedDesktopSize(...)`. After the call to `CConnection::setExtendedDesktopSize(...)`, add:

```cpp
if (desktop)
  desktop->setRemoteScreens(layout);
```

The full method should look like:
```cpp
void CConn::setExtendedDesktopSize(unsigned reason, unsigned result,
                                   int w, int h,
                                   const rfb::ScreenSet& layout)
{
  CConnection::setExtendedDesktopSize(reason, result, w, h, layout);

  if (desktop)
    desktop->setRemoteScreens(layout);

  if ((reason == rfb::reasonClient) && desktop)
    desktop->setDesktopSizeDone(result);
}
```

- [ ] **Step 4: Build and verify**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make vncviewer -j$(nproc)`
Expected: clean build.

Connect to a multi-monitor VNC server. The window should resize to show only the first remote monitor's region.

- [ ] **Step 5: Commit**

```bash
git add vncviewer/CConn.h vncviewer/CConn.cxx
git commit -m "Forward rfb::ScreenSet to DesktopWindow and expose cycleMonitor"
```

---

## Task 5: Keyboard shortcut Ctrl+Shift+Alt+−

**Files:**
- Modify: `vncviewer/Viewport.cxx`

The shortcut is intercepted in `Viewport::handleKeyPress()` before the key is forwarded to the server.

- [ ] **Step 1: Add shortcut check to handleKeyPress**

In `Viewport::handleKeyPress(int systemKeyCode, uint32_t keyCode, uint32_t keySym)`, add at the very top of the function body (before the existing `shortcutHandler.handleKeyPress` call):

```cpp
// Ctrl+Shift+Alt+Minus — cycle this window to the next remote monitor.
// Intercepted here so it is never forwarded to the remote desktop.
if (keySym == XK_minus &&
    (Fl::event_state() & FL_CTRL)  &&
    (Fl::event_state() & FL_SHIFT) &&
    (Fl::event_state() & FL_ALT)) {
  cc->cycleMonitor();
  return;
}
```

`XK_minus` is available via the existing `#include <rfb/keysymdef.h>` (the `XK_LATIN1` block already pulled in by Viewport.cxx).

- [ ] **Step 2: Build and verify**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make vncviewer -j$(nproc)`
Expected: clean build.

Connect to a multi-monitor VNC server. Press Ctrl+Shift+Alt+−. The window should resize to show the next remote monitor. Press again to cycle back to the first.

- [ ] **Step 3: Commit**

```bash
git add vncviewer/Viewport.cxx
git commit -m "Add Ctrl+Shift+Alt+Minus shortcut to cycle remote monitor in focused window"
```

---

## Task 6: CConn — multi-window spawn from profile config

Change `desktop` from a single pointer to a `std::vector` so CConn can open N windows on connect.

**Files:**
- Modify: `vncviewer/CConn.h`
- Modify: `vncviewer/CConn.cxx`

- [ ] **Step 1: Change desktop to a vector in CConn.h**

Replace:
```cpp
DesktopWindow *desktop;
```
With:
```cpp
std::vector<DesktopWindow*> desktops;
```

Add `#include <vector>` if not present.

Keep `cycleMonitor()` public and update its implementation in the next step.

- [ ] **Step 2: Update CConn.cxx — all desktop usages**

In the constructor initialiser list, remove `desktop(nullptr)`.

In the destructor (`~CConn`):
```cpp
// Replace: if (desktop) delete desktop;
for (DesktopWindow* w : desktops)
  delete w;
desktops.clear();
```

In `processNextMsg` where authentication failure clears credentials:
```cpp
// No desktop change needed here — already handled via activeProfilePath
```

In every other place that currently says `if (desktop)` or calls `desktop->...`, replace with a loop:

```cpp
// Helper lambda used throughout — call a method on all windows
auto forEachDesktop = [&](auto fn) {
  for (DesktopWindow* w : desktops) fn(w);
};
```

Add this lambda at the top of each method that broadcasts to all windows, e.g.:

```cpp
// framebufferUpdateEnd, setCursor, setCursorPos, setLEDState,
// handleClipboardRequest, handleClipboardAnnounce, handleClipboardData,
// setName (updateCaption), bell → play sound once (only call on desktops[0])
```

Specific methods:
- `framebufferUpdateEnd`: `forEachDesktop([](DesktopWindow* w){ w->updateWindow(); });`
- `setName`: `forEachDesktop([](DesktopWindow* w){ w->updateCaption(); });`
- `setCursor`: `forEachDesktop([](DesktopWindow* w){ w->setCursor(); });`
- `setCursorPos(pos)`: `forEachDesktop([&pos](DesktopWindow* w){ w->setCursorPos(pos); });`
- `setLEDState(state)`: `forEachDesktop([state](DesktopWindow* w){ w->setLEDState(state); });`
- `handleClipboardRequest`: `forEachDesktop([](DesktopWindow* w){ w->handleClipboardRequest(); });`
- `handleClipboardAnnounce(available)`: `forEachDesktop([available](DesktopWindow* w){ w->handleClipboardAnnounce(available); });`
- `handleClipboardData(data)`: `forEachDesktop([data](DesktopWindow* w){ w->handleClipboardData(data); });`
- `resizeFramebuffer`: `forEachDesktop([&](DesktopWindow* w){ w->resizeFramebuffer(server.width(), server.height()); });`
- `setExtendedDesktopSize`: `forEachDesktop([&layout](DesktopWindow* w){ w->setRemoteScreens(layout); });`
  - Also: only the window that requested the resize should receive `setDesktopSizeDone` — keep that call on `desktops[0]` for now.

- [ ] **Step 3: Update initDone to spawn N windows**

Replace:
```cpp
void CConn::initDone()
{
  ...
  desktop = new DesktopWindow(server.width(), server.height(), this);
  fullColourPF = desktop->getPreferredPF();
  ...
}
```

With:
```cpp
void CConn::initDone()
{
  ...
  std::vector<int> assignments = getMonitorWindowAssignments();

  for (int idx : assignments) {
    DesktopWindow* w = new DesktopWindow(server.width(), server.height(), this);
    w->selectedRemoteMonitor = idx;   // set before screens arrive
    desktops.push_back(w);
  }

  // Use first window's preferred format
  if (!desktops.empty())
    fullColourPF = desktops[0]->getPreferredPF();
  ...
}
```

Note: `selectedRemoteMonitor` must be `public` in DesktopWindow for this to work. Change it from private to public in `DesktopWindow.h`, or add a `setInitialMonitor(int idx)` public method.

- [ ] **Step 4: Update cycleMonitor in CConn.cxx**

```cpp
void CConn::cycleMonitor()
{
  // Cycle only the focused window.
  for (DesktopWindow* w : desktops) {
    if (Fl::focus() && Fl::focus()->window() == w) {
      w->cycleRemoteMonitor();
      return;
    }
  }
  // Fallback: cycle first window.
  if (!desktops.empty())
    desktops[0]->cycleRemoteMonitor();
}
```

- [ ] **Step 5: Build and verify**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make vncviewer -j$(nproc)`
Expected: clean build.

Set `MonitorWindowAssignments=1,2` in a profile. Connect. Two windows should open, each showing the respective remote monitor.

- [ ] **Step 6: Commit**

```bash
git add vncviewer/CConn.h vncviewer/CConn.cxx vncviewer/DesktopWindow.h
git commit -m "Support multiple DesktopWindows from monitorWindowAssignments profile config"
```

---

## Task 7: ProfileEditDialog — monitor assignments field

**Files:**
- Modify: `vncviewer/ProfileEditDialog.h`
- Modify: `vncviewer/ProfileEditDialog.cxx`

- [ ] **Step 1: Add the input widget to ProfileEditDialog.h**

Find the existing input field declarations (e.g. server name, profile name). Add:
```cpp
Fl_Input* monitorAssignmentsInput;
```

- [ ] **Step 2: Add the UI element in ProfileEditDialog.cxx constructor**

In the constructor where other input fields are laid out, add after the last existing field:

```cpp
// Monitor window assignments
{
  monitorAssignmentsInput = new Fl_Input(LBLRIGHT(x, y,
                                                  INPUT_WIDTH,
                                                  INPUT_HEIGHT,
                                                  _("Monitor windows:")));
  monitorAssignmentsInput->tooltip(
    _("Remote monitor indices to open on connect (e.g. \"1,2\"). "
      "Ctrl+Shift+Alt+\xe2\x88\x92 cycles a window between monitors."));
  y += INPUT_HEIGHT + INNER_MARGIN;
}
```

- [ ] **Step 3: Populate field on open**

In the method that loads values into the dialog fields (typically called from the constructor or a `loadValues()` helper):
```cpp
monitorAssignmentsInput->value(monitorWindowAssignments);
```

- [ ] **Step 4: Save field on OK**

In the OK/save callback:
```cpp
monitorWindowAssignments.setParam(monitorAssignmentsInput->value());
```

Also persist it via the existing profile save mechanism (same pattern as other fields).

- [ ] **Step 5: Build and verify**

`cd /d/Coding/Tools/tigervnc/build && mingw32-make vncviewer -j$(nproc)`
Expected: clean build.

Open a profile in ProfileEditDialog. The "Monitor windows" field should appear. Enter `1,2`, save, reconnect. Two windows should open.

- [ ] **Step 6: Commit**

```bash
git add vncviewer/ProfileEditDialog.h vncviewer/ProfileEditDialog.cxx
git commit -m "Add monitor window assignments field to ProfileEditDialog"
```

---

## Self-Review Notes

- **Spec coverage check:**
  - ✅ Viewport sub-region crop → Task 2
  - ✅ `cycleRemoteMonitor` + shortcut Ctrl+Shift+Alt+− → Tasks 3, 4, 5
  - ✅ Profile parameter `monitorWindowAssignments` → Task 1
  - ✅ ProfileEditDialog field → Task 7
  - ✅ Multi-window on connect → Task 6
  - ✅ Window resizes to match monitor + snaps to client monitor → Task 3, Step 5

- **Type consistency:** `sortedScreens()` defined in Task 3 and used in Tasks 3/5. `getMonitorWindowAssignments()` defined in Task 1, used in Task 6. All match.

- **Known implementation detail to verify:** In Task 2 Step 6, the exact location of the pointer coordinate conversion inside `handlePointerEvent` must be found by reading the code. The offset `remoteRegion.tl.x/y` is added after any scaling transform.
