# In-Memory Index Design

## Ownership

All structures below live inside `CVolume` and are touched **only** on that volume's I/O thread. No mutex inside the index.

```
CVolume (one thread)
  ├── CNtfsVolumeHandle
  ├── CUsnEnumerator / CUsnJournalMonitor
  └── CIndexStore
        ├── CBumpStringPool     (UTF-8 filenames)
        ├── m_rgNodes[]         (tree nodes)
        ├── m_mapFrnToNodeId    (USN FRN → node id)
        └── m_rgSearchEntries[] (flat scan list)
```

## Node Model

| Field | Purpose |
|-------|---------|
| `m_ullFrn` | NTFS file reference (stable USN key) |
| `m_ullParentFrn` | Parent FRN until resolved |
| `m_parentNodeId` | Parent node index after `ResolveParents()` |
| `m_nameOffset` / `m_cbName` | UTF-8 name in string pool |
| `m_flags` | Directory, deleted, in-use |

Full paths are **not** stored. Materialize on demand by walking `m_parentNodeId`.

## String Pool

- Append-only bump allocator (`CBumpStringPool`)
- `IStringPoolBackend` reserves `Compact()`, `GetDeadRatio()` for later
- Names stored as **UTF-8** (converted once from USN UTF-16)

## USN → Index

| Phase | Action |
|-------|--------|
| Initial `FSCTL_ENUM_USN_DATA` | `UpsertRecord` per USN row; then `ResolveParents()` + `RebuildSearchEntries()` |
| Journal | Apply by reason: create/rename-new → upsert; delete/rename-old → `MarkDeleted` |

## Search (phase 1)

- `m_rgSearchEntries` lists node ids eligible for search (non-deleted, name present)
- Fast path: ASCII substring on UTF-8 name via parallel scan **posted on volume thread** (single-threaded for now)
- No inverted index

## Cross-Volume

`CVolumeManager` holds volumes; search merges per-volume result vectors on the caller thread after each volume returns a copy.
