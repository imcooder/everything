# Index Persistence — Self-Test Cases

Covers on-disk persistence of the per-volume in-memory index (`CIndexStore` +
`CBumpStringPool` + the FRN→node map) so a restart replays only the USN delta
since the last session instead of a full `FSCTL_ENUM_USN_DATA` re-enumeration
(README §2, §5 — "Prefer a persistent on-disk index... subsequent launches
replay only the delta since the last session").

**Platform**: Windows, NTFS fixed volume, run as Administrator (USN access).

---

## Correctness — persisted state matches a fresh scan

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| P1 | Cold start writes an index | Run the app (harness or UI) against a volume with no existing persisted index file | After initial load completes, an index file exists on disk for that volume (path/naming your call — document it). |
| P2 | Warm start loads persisted state | Restart the app against the same volume/index file, with no filesystem changes in between | Cold-start-equivalent node count and search results (e.g. run the same query, same match count) — verify by comparing `INDEX_STATS` (`m_cNodes`, `m_cSearchEntries`) between a full re-scan and a persisted-load run, not just spot-checking one query. |
| P3 | Delta replay after offline changes | Stop the app, create N new files + delete M existing ones + rename K, then restart | Warm start reflects all of those changes without a full re-enumeration — verify via a timing/counter signal (see P7) that the delta path, not full-scan path, was taken, AND verify correctness (search finds the new files, doesn't find the deleted ones, sees renamed names). |
| P4 | Journal ID mismatch triggers full rescan | Persist an index, then simulate a journal reset (delete and recreate the USN journal via `fsutil usn deletejournal` + `fsutil usn createjournal`, or equivalent) before restart | App detects the journal id no longer matches the persisted checkpoint and falls back to a full `FSCTL_ENUM_USN_DATA` re-enumeration instead of attempting a delta replay against an invalid cursor. No crash, no silently-wrong index. |
| P5 | Corrupted/truncated index file | Truncate or corrupt bytes in a persisted index file, then start the app | Falls back to a full re-scan (does not crash, does not load garbage data) — log/report the fallback reason. |
| P6 | Index format/version mismatch | Bump whatever version tag the on-disk format carries, start against an old-version file | Falls back to full re-scan rather than misinterpreting old-format bytes as new-format. |

## Performance — the actual point

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| P7 | Warm start is faster than cold start | Time a cold start (no persisted index) vs. a warm start (persisted index + small delta) on the same volume | Warm start's time-to-`IsReadyForSearch()` is meaningfully lower than cold start's, scaling with delta size, not total volume entry count. Record actual numbers in the case result, not just pass/fail. |
| P8 | Load time scales with entry count reasonably | Test on at least two volumes of different sizes (e.g. a small one and your largest available) | Cold-start load time is roughly linear in entry count, not worse; warm-start load time is roughly independent of total entry count (dominated by delta size + file I/O for the persisted blob). |
| P9 | On-disk size is compact | Check the persisted file size relative to `INDEX_STATS.m_cbPoolUsed` / `m_cNodes` | Serialized size should track the in-memory compact representation (string pool + fixed-size node records), not balloon with redundant full-path strings per entry (README's stated anti-goal). |

## Safety / lifecycle

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| P10 | Crash/kill during a live session doesn't corrupt the persisted file | Kill the process (not clean shutdown) while it's actively monitoring USN changes, then restart | Next start either loads the last successfully-flushed persisted state (falls back that far, replays the rest via USN) or detects corruption and does a full rescan — never a hang or crash. |
| P11 | Multiple volumes persist independently | Machine with 2+ volumes | Corrupting/deleting one volume's persisted file doesn't affect another volume's ability to warm-start from its own file. |
| P12 | Disk full / write failure while persisting | Simulate a failed write (read-only target dir, or fill the disk) | App continues running with the in-memory index; persistence failure is non-fatal (logged, not a crash), and search still works for the current session. |

---

## What "done" means for this doc

Each row gets a ✅/❌ + one-line actual-result note (and for P7/P8, actual
numbers) before the persistence milestone is called complete. Cases needing
hardware/scale you don't have locally should say so explicitly rather than
being silently skipped.
