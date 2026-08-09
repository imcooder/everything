# Live Search UI — Self-Test Cases

Covers the WTL main window search + result list wired to the USN-driven live
index (`CVolumeManager::SearchAsync` / `ISearchSink`). These are the minimum
acceptance cases before the UI milestone is considered done — "compiles" is
not "works."

**Platform**: Windows, NTFS fixed volume with write access for the test
account. Run `Everything.Core.exe` (or whatever the UI target ends up named)
as Administrator — USN journal access requires it.

**Setup**: pick a scratch folder the search query below will match, e.g.
`C:\Temp\evtest\`. Create it before each case if missing.

---

## Core interaction

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| UI1 | App starts | Launch the exe | Single window opens: search box (top) + results list (Name/Path columns) + status bar (bottom). No console window. |
| UI2 | Type to search | Type `delta` in the search box | Result list updates without pressing Enter or a Search button; only files/folders whose name contains `delta` (case-insensitive) appear. |
| UI3 | Empty query | Clear the search box | List shows all indexed entries (or is capped sensibly — not empty, not frozen). |
| UI4 | Status bar count | Type a query with a known number of matches | Status bar shows a match count consistent with the list row count. |
| UI5 | Open via double-click | Double-click a result row | The file opens in its associated app, or the folder opens in Explorer (`ShellExecute`). No crash if the target was deleted since the last refresh. |
| UI6 | Open via Enter | Select a row, press Enter | Same behavior as UI5. |

## Live update — the actual point of USN monitoring

For each case: have the search box already showing results for a query
(e.g. `delta`), then perform the filesystem change from **outside** the app
(Explorer, `cmd`, PowerShell — not through the app itself) and observe the
already-open result list without touching the search box again.

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| UI7 | New matching file appears live | With `delta` query showing, create `C:\Temp\evtest\delta-report.txt` externally | New row appears in the result list within a few seconds, no manual refresh. |
| UI8 | New non-matching file does not appear | With `delta` query showing, create `C:\Temp\evtest\unrelated.txt` externally | No new row appears. |
| UI9 | Delete removes the row live | Delete a file that was showing as a match | Its row disappears from the list without restarting the app or re-typing the query. |
| UI10 | Rename into match | Rename a non-matching file so its new name contains `delta` | A row appears for the new name (no restart, no re-typed query). |
| UI11 | Rename out of match | Rename a currently-matching file so its new name no longer contains `delta` | Its row disappears from the list. |
| UI12 | Rename in place (still matches) | Rename `delta-a.txt` → `delta-b.txt` (still matches `delta`) | Row updates to the new name; does NOT show as a duplicate (no leftover row for the old name alongside a new one for the same file). |
| UI13 | Directory rename cascades | Rename a directory that contains a matching file, when query also matches path (e.g. `parent:` style) | Affected rows' displayed path updates to reflect the new parent name — verify no dangling/stale path in the list. |
| UI14 | Rapid create+delete (churn) | Script-create and immediately delete 20 files matching the query in a loop | List settles to the correct final state (no leaked rows, no crash, no duplicate rows) after churn stops. |

## Scale (virtual list, multi-volume load)

The result list MUST use ListView virtual mode (`LVS_OWNERDATA` +
`LVN_GETDISPINFO`), never per-row `InsertItem` — a real machine can have
several million to tens of millions of indexed files, and each fixed volume
loads on its own dedicated I/O thread (already implemented in
`CVolumeManager`/`CVolume` — the UI layer must not introduce a second,
UI-thread-serialized load path).

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| UI18 | Empty-query full listing at scale | On a volume with 1M+ entries, clear the search box | List reports the true count in the status bar and scrolls smoothly (no per-row allocation stall, no OOM) — confirms virtual mode, not `InsertItem` per row. |
| UI19 | Startup with multiple volumes | Machine has 2+ fixed NTFS volumes attached | Volumes load concurrently (overlapping progress, not one after another) — confirms the UI didn't collapse the existing one-thread-per-volume load into a single serialized path. |
| UI20 | Memory sanity at scale | Load a volume with several million entries | Process working set stays in the "compact node + string pool" ballpark described in README §Index Memory — not multiplied by a redundant UI-side copy of every path string. |

## Threading / stability

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| UI15 | No UI-thread violation | Run under a debugger or with `Application Verifier` while triggering UI7–UI11 rapidly | No cross-thread control access assert/crash. All list updates must arrive via a UI-thread message (e.g. `PostMessage` from the volume I/O thread), never a direct call into WTL/Win32 controls from that thread. |
| UI16 | Switch query while live results are streaming | Type `delta`, then immediately overtype with `omega` before the first query's initial scan finishes | Old query's in-flight results stop arriving; list ends up showing only `omega` matches (verifies `CancelSearch`/stale-request handling from the existing `volume.cpp` logic is wired to the UI's query changes). |
| UI17 | Close window mid-search | Close the app while a search is actively streaming live updates | Clean shutdown, no crash, no hung process left in Task Manager (`StopAllAndWait` path). |

---

## What "done" means for this doc

Each row above should end up with a ✅/❌ + one-line actual-result note before
the UI milestone is called complete. A case that can't be exercised (e.g. no
scratch NTFS volume in CI) should be marked with a reason, not silently
skipped.
