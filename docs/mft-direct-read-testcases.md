# Direct MFT Read — Self-Test Cases

Covers reading the NTFS Master File Table directly (README §1: "Read MFT records
directly from each NTFS volume") instead of the initial-load path currently used
(`FSCTL_ENUM_USN_DATA` in `src/ntfs/usn_enumerator.cpp`). This is a read-only,
additive change to the **initial load** phase only — USN journal monitoring for
live updates (README §2) is unaffected and must keep working exactly as-is.

**Safety note**: this feature only ever *reads* `\\.\<drive>:` and the `$MFT`
metadata file. It must never issue a write, and a parsing bug must degrade to
"index is incomplete/wrong," never to a crash that takes down search entirely
or (worse) any accidental write path. Every case below assumes read-only intent.

---

## Correctness — parsed MFT data matches ground truth

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| M1 | Record count sanity | Parse a volume's MFT directly, compare total record count against `FSCTL_ENUM_USN_DATA`'s enumerated record count on the same volume | Counts match (or MFT count is the superset if USN excludes something specific — document the exact relationship, don't just assert equality blindly if there's a legitimate reason they'd differ). |
| M2 | Name matches for a known file | Pick a handful of files with a distinctive name (e.g. seed a scratch file with a unique name before the test), locate the corresponding node in both the MFT-parsed index and the USN-parsed index | Same file reference number, same parent FRN, same name bytes. |
| M3 | Directory flag correctness | Compare `INDEX_NODE_DIRECTORY` flag for a sample of known files/folders between the two load paths | Identical flag value for every sampled node. |
| M4 | Deleted/unused MFT records skipped | MFT contains records marked not-in-use (deleted files whose slot hasn't been reused) | These do not appear as live nodes in the parsed index — parser must check the in-use flag in the record header, not just walk every record slot blindly. |
| M5 | Fixup (update sequence array) applied correctly | Any MFT record spanning multiple sectors | Parser correctly restores the last two bytes of each sector by applying the update sequence array before interpreting record contents — a record with an incorrect/skipped fixup would show garbage bytes at each sector boundary; verify parsed names don't contain those garbage bytes. |
| M6 | Non-resident / long attribute lists | A file with enough attributes that they spill into an `$ATTRIBUTE_LIST` (rare but real — e.g. files with many alternate data streams or heavily fragmented metadata) | Parser either handles the attribute list correctly or explicitly documents this as a known unsupported edge case with a safe fallback (skip the record, don't crash, don't produce a wrong name) — must not be silently wrong. |
| M7 | Special/system MFT records (0-15) | The first 16 MFT records are reserved NTFS metadata files ($MFT, $MFTMirr, $LogFile, $Volume, etc.) | Parser's behavior toward these is a deliberate choice (include, exclude, or special-case) — document which, and confirm it doesn't produce nonsense root-level "files" named after internal NTFS metadata unless that's genuinely intended. |

## Performance — the actual point of doing this at all

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| M8 | Faster cold start than USN enumeration | Time direct-MFT initial load vs. the existing `FSCTL_ENUM_USN_DATA` path on the same volume | Record actual numbers. If direct MFT read is not measurably faster on this environment's disks, say so plainly — the entire justification for this feature is speed; don't ship it as a wash or a regression without flagging that clearly. |
| M9 | Scales with volume size, not record count alone | Compare load time trend across at least two differently-sized volumes | Confirms no pathological per-record overhead (e.g. an O(n²) parent-resolution pass) that would erase the benefit on very large volumes. |

## Safety / fallback

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| M10 | Falls back to USN enumeration on any parse failure | Simulate a read failure or corrupt-looking record (e.g. by testing against a volume/record the parser doesn't understand, or via a unit test with a hand-crafted malformed buffer) | Falls back to the existing `FSCTL_ENUM_USN_DATA` path rather than producing a partial/wrong index or crashing. This fallback must be automatic, not a manual toggle. |
| M11 | No write access ever attempted | Code review + a runtime assertion/guard if practical | Every `CreateFileW`/`DeviceIoControl` call in the new MFT-reading code path opens for read-only access; no `GENERIC_WRITE`, no `FSCTL_*` write operations. |
| M12 | Read-only on a volume this process can't get raw access to (no admin) | Run without Administrator (this dev environment's actual daily condition) | Fails to open the raw volume/MFT handle exactly like the existing USN path already does — same graceful `VOLUME_STATE_ERROR`, not a new/different crash. |

---

## What "done" means for this doc

Given this environment cannot elevate to Administrator (confirmed repeatedly this
project), M1-M3, M7-M9, M12 need either a hand-crafted synthetic MFT record buffer
unit-tested in isolation (parser correctness, independent of real disk access) or
a note that they require a machine with admin + a real NTFS volume to verify
end-to-end. Do not claim end-to-end verification that didn't happen — mark clearly
what's unit-tested-in-isolation vs. reasoned-through-code vs. a known gap, exactly
like the UI and persistence milestones' test docs already do.
