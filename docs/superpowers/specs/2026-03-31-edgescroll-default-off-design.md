# EdgeScroll Default Off — Design Spec

**Date:** 2026-03-31
**Status:** Approved

## Summary

Change the `EdgeScroll` setting to default to `false` instead of `true`. The setting is already honored dynamically at runtime (checked on every mouse event in fullscreen mode), so changing the default is sufficient to ensure it is off when connecting to a host unless the user has explicitly enabled it.

## Change

| File | Location | Before | After |
|------|----------|--------|-------|
| `vncviewer/parameters.cxx` | Line 69 | `true` | `false` |

## Behavior

- **Before:** EdgeScroll is active by default for all new users and fresh connections.
- **After:** EdgeScroll is inactive by default. Users who want it must enable it in the Options dialog (Input tab).
- **Mid-session toggling:** Unchanged — the setting still takes effect immediately when toggled in the Options dialog.
- **Saved configs:** Unaffected. Users who previously saved `EdgeScroll=true` will retain that value.

## Scope

One line changed. No new logic, no new state, no connection lifecycle changes needed.
