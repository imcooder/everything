# Everything

A high-performance NTFS file search engine inspired by [Everything](https://www.voidtools.com/) by voidtools. This project aims to deliver instant filename search across very large volumes by reading the NTFS Master File Table (MFT) directly and maintaining an in-memory tree index updated incrementally through the USN Change Journal.

Everything (the original product) is proprietary. This repository is an independent reimplementation focused on the same core problem: sub-millisecond search responsiveness on systems with tens of millions of files.

---

## Project Goals

### 1. Fast Cold Start via Direct NTFS Indexing

On startup, the engine loads file metadata from NTFS volumes by parsing the MFT rather than walking the directory tree through the Win32 API.

- Read MFT records directly from each NTFS volume (requires elevated privileges).
- Load volumes **in parallel** so cold-start time scales with the slowest disk, not the sum of all disks.
- Prefer a **persistent on-disk index** where practical, so subsequent launches replay only the delta since the last session instead of performing a full MFT scan every time.

### 2. Incremental Updates via the USN Change Journal

A static MFT snapshot is insufficient for a usable search tool. The engine must stay current while running.

- Monitor the NTFS **Update Sequence Number (USN) Change Journal** to observe create, delete, rename, and attribute changes in near real time.
- Apply journal records incrementally to the in-memory index without rebuilding from scratch.
- On restart, load the persisted index and **replay journal entries** from the last known USN to catch up quickly.

Static MFT-only tools that lack journal monitoring are insufficient; **live USN-driven updates** are a first-class requirement here.

### 3. Tree-Structured Index (Not Flat Full Paths)

The index mirrors the filesystem hierarchy. Each entry stores a **local name** and a **parent reference**, not a duplicated full path string.

- Internal representation: `(filename, parent_id, flags, …)` arranged as a tree, analogous to how NTFS stores `$FILE_NAME` with a directory file reference.
- When a directory is renamed or moved, child nodes remain in place; only the affected subtree’s resolved path changes at query time.
- Full paths are **materialized on demand** (for display or path-prefix filtering), not stored redundantly for every file.

This design keeps memory and on-disk index size bounded at scale. Storing a complete path per file would multiply storage cost and update work unnecessarily on large trees.

### 4. Instant Search

Search is optimized for interactive use: results must update as the user types, with no perceptible lag. Query behavior follows [Everything search syntax](https://www.voidtools.com/support/everything/searching/) where practical.

The engine uses three search tiers — **Fast**, **Path**, and **Slow** — over a tree-structured index and UTF-8 string pool. See [Index Memory & Search Architecture](#index-memory--search-architecture) for the full design.

| Tier | Examples | Target latency |
|------|----------|----------------|
| **Fast** | `report`, `*.pdf`, `*ss*.md` | Keystroke-responsive on 30M+ entries |
| **Path** | `d:\proj\`, `parent:c:\windows`, Match Path + `xxx\333` | Fast after subtree pruning |
| **Slow** | `regex:…` | Acceptable; not the default hot path |

**Design constraint:** search must **not** rely on a traditional inverted index (token → posting lists). Performance comes from a compact tree/array layout plus sequential or branch-predictable scans, not from building and maintaining inverted postings at index time.

### 5. Scale and Performance Targets

Modern workstations commonly host **30 million or more** file entries across one or more large NTFS volumes. The architecture must be designed for that order of magnitude from the outset.

| Dimension | Target |
|-----------|--------|
| Index size | Millions to tens of millions of entries per machine |
| Cold start | Complete or resume index within seconds, not minutes |
| Search latency | Visible update on every keystroke; interactive feel |
| Memory | Bounded footprint via compact node encoding and string pooling |
| On-disk index | Compact serialized form; optional compression |

Performance and memory characteristics are **first-class requirements**, not post-hoc optimizations.

**Means to meet these targets** (applied during implementation, not separate product features):

- Direct MFT access instead of recursive directory enumeration.
- **Parallel processing** across volumes and CPU cores for MFT load, parsing, USN replay, index persistence, and search — keeping the UI thread responsive.
- Compact tree index and string pooling to bound memory and disk use.
- Incremental USN journal updates instead of full index rebuilds.
- Sequential scan over compact filename storage rather than an inverted index.

### 6. User Interface — Match Everything (Initial Milestone)

The first UI milestone should **look and behave like [Everything](https://www.voidtools.com/)** so users can switch without relearning workflows. Visual redesign and experimental layouts are out of scope until core search and indexing are stable.

**Primary window**

- Single main **search window**: search box at the top, **result list** below, **status bar** at the bottom.
- **Live search**: the result list updates on every keystroke with no explicit “Search” button required.
- **Sortable columns** in the result list (at minimum **Name** and **Path**; additional columns such as size and date may follow Everything’s default set).
- **Status bar** shows match count; when a row is selected, show file details (size, dates, attributes) consistent with Everything’s behavior.

**Interaction**

- **Double-click** or Enter to open a file or folder.
- **Right-click context menu** on results: open, open path, copy, cut, delete, rename, properties, and related shell actions via the Windows shell (`IContextMenu`, `ShellExecute`, etc.).
- **Keyboard navigation** in the result list (arrow keys, type-to-jump within results).
- **Drag and drop** from the result list where Everything supports it.

**Application chrome**

- Standard menu bar: **File**, **Edit**, **View**, **Search**, **Bookmarks**, **Tools**, **Help** — same top-level structure as Everything.
- **View** menu: Details view (default), optional thumbnail/preview modes in later iterations if needed for parity.
- **Tools → Options** dialog with tabs aligned to Everything’s organization (UI, Results, Search, Indexes, etc.) for settings users expect.

**System integration**

- **System tray** icon; closing the search window **minimizes to tray** rather than exiting (configurable, matching Everything’s default behavior).
- **Global hotkey** to show or focus the search window.
- Optional **run on startup** and **single-instance** behavior consistent with Everything.

**Implementation notes**

- Native **Win32 UI** (dialog, ListView, common controls) — same technology class as Everything. No cross-platform UI framework in the first milestone.
- The UI layer is a thin shell over the index/query engine; indexing, USN replay, and search logic must not depend on UI code.
- Pixel-perfect cloning is not required; **layout, menus, shortcuts, and interaction patterns** should be familiar to an Everything user.

**Deferred UI-related features** (after core parity): HTTP/ETP server UI, tabbed windows (Everything 1.5+), custom themes beyond a simple light/dark match, third-party toolbar integrations.

---

## Non-Goals (Initial Scope)

- Support for non-NTFS filesystems (FAT, exFAT, ext4, etc.) in the first milestone.
- Full-text or content indexing inside files.
- Network drive indexing beyond what NTFS USN and MFT access allow on locally attached volumes.
- A novel or experimental UI — the **first UI must follow Everything** (see [§6 User Interface](#6-user-interface--match-everything-initial-milestone)).
- An inverted (token) index for filename search.
- Feature parity with **advanced** Everything options (HTTP server, ETP, folder indexing beyond NTFS volumes, etc.) before core indexing, search, and main-window UI are proven.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│              Search UI (Everything-compatible)               │
└─────────────────────────────┬───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│                     Query Engine                             │
│         parse → prune → Fast / Path / Slow match             │
└─────────────────────────────┬───────────────────────────────┘
                              │ scan
┌─────────────────────────────▼───────────────────────────────┐
│  Search view: ENTRY[]  +  UTF-8 string pool                  │
│  Index tree: node { parent_id, name_ref, flags, frn }        │
└──────────────▲──────────────────────────────▲───────────────┘
               │                              │
    ┌──────────┴──────────┐        ┌──────────┴──────────┐
    │  MFT Initial Load   │        │  USN Journal Reader │
    └──────────▲──────────┘        └──────────▲──────────┘
               │                              │
    ┌──────────┴──────────────────────────────┴──────────┐
    │              NTFS Volume(s)                         │
    └─────────────────────────────────────────────────────┘
```

---

## Index Memory & Search Architecture

This section defines how file metadata is stored in memory and how queries are executed. It complements [§3 Tree-Structured Index](#3-tree-structured-index-not-flat-full-paths) and [§4 Instant Search](#4-instant-search).

### In-Memory Index Layout

The index separates **tree structure**, **filename storage**, and **search iteration**:

| Component | Role |
|-----------|------|
| **Index nodes** | `{ parent_id, name_offset, name_length, flags, frn }` — tree mirrors NTFS; paths are not stored per file |
| **UTF-8 string pool** | All filenames in a contiguous allocator-backed blob; nodes hold offset + length |
| **Search entries** | Flat `ENTRY[]` of indexable items (files and folders); search scans this array, not the tree |
| **Path cache** (optional) | LRU `node_id → path prefix` for display and repeated path queries |

**String pool allocator:** Phase 1 uses an append-only bump allocator. The backend exposes a stable interface (`Alloc`, `GetPtr`, `Reset`) with **reserved hooks** for future compaction, hole reuse, and on-pool compression — without changing node or search entry layout.

**Parent references** use stable IDs (FRN or permanent node ID), not array indices, so pool compaction and node-table maintenance do not break the tree.

**On disk:** filenames are stored as **UTF-8** (same encoding as memory). NTFS `$FILE_NAME` (UTF-16) is converted once at index time.

### Search Architecture Overview

```
User query (UTF-16 from UI)
        │
        ▼
  Parse query ──► modifiers: path: regex: parent: ext: …
        │
        ├──► Subtree prune (path prefix / parent: / drive:)
        │
        ▼
  Parallel scan ENTRY[] in shards
        │
        ├── Fast tier  ──► UTF-8 filename: substring / glob / prefix
        ├── Path tier  ──► materialized path or per-segment tree filter
        └── Slow tier  ──► compiled regex on filename or full path
        │
        ▼
  Merge results → UI (materialize path for display only on hits)
```

Search never requires an inverted index. Optional auxiliary structures (path prefix cache, child lists for folder functions) may be added later; they are not token posting lists.

---

### Fast Tier (default hot path)

Used when **Match Path** is off and the query is not in `regex:` mode.

| Query | Behavior |
|-------|----------|
| `report` | Substring match on **filename** (UTF-8 bytes) |
| `*.pdf` | Glob on whole filename (Everything default wildcard semantics) |
| `*ss*.md` | Glob on filename; extension split at last `.` when needed |
| `case:ABC` | ASCII or Unicode case rules per Everything modifiers |

**Matching rules:**

- **ASCII query on UTF-8 names:** byte-level comparison is safe (ASCII bytes never appear inside multibyte UTF-8 sequences).
- **Non-ASCII query:** match UTF-8 byte sequences or decode to code points as required.
- **Do not** store a second lowercase copy of every name; apply case folding during the scan or on the query.

**Execution:** convert the query to UTF-8 once, partition `ENTRY[]` across worker threads, match against the string pool, merge result IDs. Apply a result cap (e.g. stop after N matches) when the UI does not need unbounded rows.

**Implementation priority:** Phase 1.

---

### Path Tier

Used when the query restricts **location** or matches against **path segments**, aligned with Everything **Match Path** and [searching syntax](https://www.voidtools.com/support/everything/searching/).

| Query | Behavior |
|-------|----------|
| `d:\downloads\` | Path prefix — restrict to subtree under that folder |
| `parent:c:\windows` | Files and folders under that directory (including subfolders) |
| `parent:…` + `nosubfolders:` | Single directory level only |
| `\work report` | Path segment ending with `work` AND filename containing `report` (space = AND) |
| `documents\` | Partial path segment (folder name ending with `documents`) |
| `xxx\333` (Match Path on) | Substring on **full path string**: parent segment ends with `xxx`, next segment starts with `333`, separated by `\` |

**How the tree index supports this without storing full paths:**

1. **Subtree prune (preferred):** resolve `d:\proj\` or `parent:` to a root `node_id`, collect descendant entry IDs, run Fast-tier matching only within that set — avoids scanning all 30M entries.
2. **Path materialization:** walk `parent_id` links upward, append UTF-8 segment names from the pool, match against the full path string (Everything Match Path behavior).
3. **Per-segment filter (optimization):** for patterns like `xxx\333`, filter directory nodes whose name ends with `xxx`, then children whose name starts with `333`, without building the full path for every file.

Path materialization runs on **candidates after pruning** or on **result rows for display**, not on every indexed file for every keystroke when a subtree prune is available.

**Implementation priority:** Phase 2 (path prefix / `parent:`), Phase 3 (Match Path / segment patterns).

---

### Slow Tier (regex)

Used when **Enable Regex** is on or the query is prefixed with `regex:`.

| Rule | Detail |
|------|--------|
| Syntax isolation | In regex mode, wildcards, functions, and other operators do not apply (same as Everything) |
| Default target | Filename only |
| Full path | Requires Match Path or `\` in the pattern; match against materialized UTF-8 path |
| Performance | Regex is **not** the default hot path; prune by drive/path/filename first when possible |

Compile the pattern once per query, then run against the candidate set (or full `ENTRY[]` if unavoidable). Use a UTF-8-aware regex engine.

**Implementation priority:** Phase 4.

---

### Query Examples (Everything alignment)

| User input | Tier | Notes |
|------------|------|-------|
| `report` | Fast | Filename substring |
| `*.mp3` | Fast | Whole-filename glob |
| `*ss*.md` | Fast | Disable “match whole filename” or use leading/trailing `*` per Everything |
| `d:\music\ *.flac` | Path + Fast | Subtree prune, then glob |
| `parent:c:\windows` | Path | Subtree under `c:\windows` |
| `xxx\333` | Path | Match Path: path string contains `xxx\333` at a `\` boundary |
| `regex:^[A-Z].*\.log$` | Slow | Filename regex |
| `regex:.*\\Windows\\` | Slow | Full-path regex with Match Path |

Advanced Everything functions (`child:`, `ext:`, size/date filters) may be added incrementally; they compose with the tiers above and do not require an inverted filename index.

---

### Search Implementation Phases

| Phase | Deliverable |
|-------|-------------|
| **1** | UTF-8 string pool (bump allocator) + `ENTRY[]` + Fast tier (substring, glob, `*ss*.md`) |
| **2** | Path tier: drive prefix, `parent:`, subtree pruning |
| **3** | Match Path, segment patterns (`xxx\333`), path LRU cache |
| **4** | Slow tier: `regex:` for filename and path |
| **5** | Additional Everything modifiers and search functions |

---

### Character Encoding Summary

| Layer | Encoding |
|-------|----------|
| NTFS `$FILE_NAME` | UTF-16 (load-time input) |
| Index / DB string pool | **UTF-8** |
| Search (internal) | **UTF-8** query string |
| UI / Shell | UTF-16 at boundaries (`CreateWindow`, `ShellExecute`, etc.) |

Convert UTF-16 ↔ UTF-8 at the UI and shell boundaries, not per file during every search.

### Per-Volume Threading (no locks inside a disk)

Each fixed NTFS volume is owned by one `CVolume` with **one dedicated I/O thread** (Boost.Asio `io_context`). All work for that disk runs serially on that thread:

| On the volume thread | No cross-thread access to this data |
|----------------------|-------------------------------------|
| Open `\\.\X:` | Volume handle |
| `FSCTL_ENUM_USN_DATA` (initial load) | |
| `FSCTL_READ_USN_JOURNAL` (monitor) | |
| Apply USN records to the in-memory index | String pool, nodes, `ENTRY[]` |
| Per-volume compact / flush to disk cache | |

**Why this fits the product**

- Volumes are independent roots (`C:\` vs `D:\`); parallelizing **one thread per disk** matches the hardware and avoids lock contention on 30M-entry structures.
- USN callbacks, pool append, and node updates never race within a volume — **no `mutex` on the index**.
- Different disks still load and monitor **in parallel** (N volumes → N threads).

**What may still use locks (minimal)**

| Scope | Lock |
|-------|------|
| `CVolumeManager` volume map | `std::mutex` when adding/removing drives or `RefreshVolumes()` |
| UI thread | Win32 message queue only |
| Global search (optional) | Either post a search job **to each volume thread** and merge result lists, or publish **read-only snapshots** — do not mutate the index from the UI thread |

**Anti-patterns to avoid**

- A single global index mutated by multiple volume threads.
- Calling USN or index update APIs directly from the UI thread.
- Sharing one `io_context` across all volumes (reintroduces locking or careful strand discipline).

The console harness (`Everything.Core`) currently uses a global callback for logging; production code should keep **index mutation inside `CVolume`** and pass only completed result sets outward.

---

## Testing Strategy

Correctness and performance must be validated under realistic load, not only on small developer machines.

### Synthetic Corpus Generation

- Build tooling to create **large synthetic directory trees** (millions of files) on NTFS test volumes or VHDs.
- Cover varied depth, fan-out, name patterns, and rename/move scenarios to stress the tree index and journal replay logic.

### Benchmark Suite

Run repeatable benchmarks against generated corpora and record:

- **Cold-start time** — full MFT load vs. resume from persisted index + journal replay.
- **Incremental update throughput** — apply USN records under sustained filesystem churn.
- **Search latency** — p50 / p95 / p99 for Fast-tier (substring, glob), Path-tier (prefix, `parent:`), and Slow-tier (regex) at 1M, 10M, and 30M+ entry counts.
- **Memory footprint** — resident set size and index structure overhead per million entries.
- **On-disk index size** — raw and compressed serialized form.

### Regression Gates

- Unit tests for MFT record parsing, tree mutations, and query matching.
- Integration tests that perform filesystem operations and assert index consistency against ground truth.
- Performance regressions blocked by CI thresholds once baseline numbers are established.

---

## Reference Material

| Resource | Relevance |
|----------|-----------|
| [Everything (voidtools)](https://www.voidtools.com/) | Product reference for **UI layout**, UX, menus, and performance expectations. |
| [Everything — Searching](https://www.voidtools.com/support/everything/searching/) | Search syntax, Match Path, wildcards, and regex behavior. |
| NTFS documentation (MFT, `$FILE_NAME`, USN Change Journal) | Required reading for index load and incremental update design. |

Additional open-source NTFS/MFT projects may be referenced during development; contributions and pointers are welcome.

---

## Platform

- **Primary target:** Windows 10 / 11, NTFS fixed disks.
- **Compiler:** MSVC (Visual Studio 2022 or compatible).
- **Privileges:** Administrator or equivalent rights for raw volume access and USN journal reads during development; a production deployment should document the minimum privilege model.

### Building (Windows)

Requirements: **Node.js 18+**, Visual Studio 2022, CMake 3.20+. Dependencies (Boost via [vcpkg manifest](vcpkg.json)) are installed automatically during `npm run setup`.

All repo commands are defined in [`package.json`](package.json) and implemented under `scripts/` (Node.js, cross-platform entrypoints).

| Command | Purpose |
|---------|---------|
| `npm run setup` | Clone/bootstrap vcpkg, CMake configure (`build/`) |
| `npm run build` | Debug build → `build/src/Debug/Everything.Core.exe` |
| `npm run build:release` | Release build → `build/src/Release/Everything.Core.exe` |
| `npm run test` | Build and run unit tests (GoogleTest via CTest) |
| `npm run dev` | Build Debug if needed, then run the console harness |
| `npm run format` | Apply `.clang-format` to `src/` |
| `npm run format:check` | Verify formatting (same rules as CI) |

Typical workflow:

```bat
git clone <repo>
cd everything
npm run setup
npm run dev
```

Run as **Administrator** for USN volume access. CI uses `npm run setup` + `npm run build:release`.

### Version numbering

The product version is **`major.minor.build`**:

| Segment | Source |
|---------|--------|
| `major.minor` | [`package.json`](package.json) `"version"` — edit manually (e.g. `0.1.0` → use `0.1`) |
| `build` (patch) | `BUILD_NUMBER` env var; **GitHub Actions** sets this to [`github.run_number`](https://docs.github.com/en/actions/learn-github-actions/contexts#github-context) |

Examples: `package.json` `"0.1.0"` + CI run `#42` → **`0.1.42`**. Local builds without `BUILD_NUMBER` use the patch digit from `package.json` (`0.1.0`).

```bash
npm run version                  # show resolved version
BUILD_NUMBER=42 npm run version  # simulate CI
```

CMake receives `-DEVERYTHING_VERSION=…` at configure time; the binary exposes it as `EVERYTHING_VERSION` (see startup banner in `main.cpp`).

### Continuous integration

GitHub Actions workflow [`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push and pull request to `main` / `master`:

| Job | Runner | Command |
|-----|--------|---------|
| **format** | `ubuntu-latest` | `npm run ci:format` |
| **build** | `windows-latest` | `npm run ci:build` |
| **test** | `windows-latest` | `npm run ci:test` |

Each `ci:*` script orchestrates the underlying `npm run setup`, `format:check`, `build:release`, `test` steps. GitHub Actions only handles checkout, Node setup, and caches.

Local equivalents:

```bash
npm run ci:format       # same as CI format job (installs clang-format on Linux CI)
npm run ci:build        # setup + Release build (Windows)
npm run ci:test         # setup + tests (Windows)
npm run format:check    # formatting only
npm run format          # auto-fix formatting
npm run setup           # init vcpkg + CMake (Windows)
npm run build:release   # Release build
npm run test            # unit tests
npm run dev             # Debug build + run
```

The **test** job runs after **build** and reuses vcpkg / CMake caches when possible.

### Merge policy (CI must pass)

Code may merge into `master` / `main` **only after all CI jobs pass** (`format`, `build`, `test`).

**One-time setup (repo admin, after the first CI run on GitHub):**

```bash
gh auth login
npm run protect:branch
PROTECT_BRANCH=main npm run protect:branch   # if default branch is main
```

Manual fallback: **Settings → Branches** → require status checks `format`, `build`, `test`.

Requirements: Node.js 18+; LLVM `clang-format` for format scripts; Windows + CMake + MSVC for setup/build/dev.

Source layout:

| Path | Role |
|------|------|
| `src/volume/volume` | One NTFS volume: handle, index, load, monitor, dedicated I/O thread (`CVolume`) |
| `src/volume/volume_manager` | Discovers drives; one `CVolume` per letter; parallel load/monitor |
| `src/index/` | `index_store`, `bump_string_pool`, `query_matcher` (`CIndexStore`, …) |
| `src/ntfs/` | `ntfs_volume_handle`, `usn_enumerator`, `usn_journal_monitor` |
| `src/io/io_service` | Boost.Asio `io_context` on a worker thread |
| `package.json` | Cross-platform repo scripts (`format`, `build`, `test`, …) |
| `tests/` | GoogleTest unit tests (index matchers, string pool, …) |
| `docs/index-design.md` | In-memory index design notes |

---

## Coding Style

All native code in this repository follows **Microsoft Visual C++ (VC++)** conventions, consistent with classic Win32 systems programming. The goal is a uniform, readable codebase that any Windows systems programmer can navigate without style surprises.

### Language and Toolchain

- **Language:** C++ (VC++ / MSVC), targeting Windows APIs (`windows.h`, `winioctl.h`, etc.).
- **Character type:** `WCHAR` / wide strings (`L"..."`) for paths and filenames; `TCHAR` only where dual ANSI/Unicode support is explicitly required.
- **Types:** Prefer Windows typedefs where they match platform data (`DWORD`, `ULONGLONG`, `BOOL`, `HANDLE`, `USN`) and fixed-width types (`UINT32`, `UINT64`) elsewhere.
- **Build:** Visual Studio solution (`.sln` / `.vcxproj`) is the primary build system.
- **UI:** Win32 common controls and dialogs for the main search window, consistent with Everything.

### Naming — Hungarian Notation

Identifiers use **Hungarian notation**: a type or role prefix, then a descriptive name in PascalCase.

**Member variables** (class and struct fields) **must begin with `m_`** — the letter `m`, then an underscore — followed by the usual Hungarian type prefix and name. Do not use bare `m` without the underscore (wrong: `mCount`; correct: `m_dwCount`).

| Prefix | Meaning | Example |
|--------|---------|---------|
| `m_` | **Required** prefix for every member variable | `m_parentId`, `m_dwEntryCount`, `m_hVolume` |
| `p` | Pointer | `pNode`, `pBuffer` |
| `pp` | Pointer to pointer | `ppResults` |
| `n` / `dw` / `ul` | Count or integer (`DWORD`, `ULONG`, …) | `nEntryCount`, `dwBytesRead` |
| `b` / `f` | Boolean | `bIsDirectory`, `fComplete` |
| `h` | Handle | `hVolume` |
| `str` / `wsz` / `psz` | String (`CString`, `WCHAR*`, `LPWSTR`) | `wszPattern`, `pszPath` |
| `cb` / `cch` | Size in bytes / characters | `cbBuffer`, `cchFileName` |
| `rg` | Array | `rgNodes` |
| `idx` | Index | `idxParent` |

Member naming pattern: **`m_` + Hungarian type + PascalCase name**

```cpp
// Correct
DWORD   m_dwEntryCount;
HANDLE  m_hVolumeFile;
BOOL    m_bIsDirectory;
PINDEXNODE m_pNodes;

// Wrong — missing m_ prefix on members
DWORD   dwEntryCount;   // local/param only
BOOL    mIsDirectory;   // no underscore after m
```

Non-member local variables and parameters use Hungarian prefixes but **must not** use `m_`:

```cpp
DWORD dwEntryCount = 0;
PINDEXNODE pNode = nullptr;
```

```cpp
class CIndexNode {
public:
    UINT32 GetParentId() const { return m_parentId; }

private:
    UINT32  m_parentId;
    UINT32  m_nameOffset;
    USHORT  m_flags;
    BOOL    m_bIsDirectory;
};
```

### Types, Functions, and Constants

| Element | Convention | Example |
|---------|------------|---------|
| Classes / structs | `C` prefix + PascalCase, or ALL_CAPS for plain POD layouts | `CVolumeIndex`, `FILE_RECORD_HEADER` |
| Free functions | PascalCase | `LoadMft`, `ApplyUsnRecord` |
| Member functions | PascalCase | `ParseMft`, `SearchFiles` |
| Macros / compile-time constants | ALL_CAPS | `MAX_PATH_DEPTH`, `CLUSTERS_PER_READ` |
| Enumerators | PascalCase or ALL_CAPS with common prefix | `AttributeFileName`, `AttributeData` |
| Namespaces | lowercase, short | `index` |

Typedef pointer aliases use a leading `P` where appropriate (`PFILE_RECORD_HEADER`, `PDISKHANDLE`).

### Formatting

- **Indentation:** 4 spaces (no tabs).
- **Braces (K&R / Attach):** The opening `{` stays on the **same line** as the statement or declaration — not on its own line. Applies to `if` / `else` / `for` / `while` / `switch` / `do`, function definitions (free and member), and `class` / `struct` / `namespace`:

```cpp
void LoadIndex() {
    if (bSuccess) {
        DoWork();
    } else {
        return FALSE;
    }

    for (DWORD i = 0; i < dwCount; ++i) {
        Process(i);
    }
}

class CVolumeIndex {
public:
    BOOL LoadFromMft(HANDLE hVolumeFile);

private:
    DWORD m_dwEntryCount;
};
```

- **Line length:** Prefer ≤ 120 columns; break long parameter lists one per line.
- **Includes:** `#include "ProjectHeader.h"` first, then C++ standard headers (`#include <vector>`), then Windows headers (`#include "windows.h"`). Use `#pragma once` in headers.
- **Packing:** NTFS on-disk structures use `#pragma pack(push, 1)` / `#pragma pack(pop)` with a comment naming the layout source.

#### Format with clang-format

Machine-readable rules live in [`.clang-format`](.clang-format) at the repo root (LLVM base + K&R braces + 4-space indent + 120-column limit). Editors and CI can read it directly.

| Setting | Value |
|---------|-------|
| `BreakBeforeBraces` | `Attach` — `{` on same line as `if` / `for` / functions |
| `IndentWidth` | 4 spaces |
| `ColumnLimit` | 120 |
| `AllowShortIfStatementsOnASingleLine` | `Never` — always use `{ }` for `if` |
| `InsertBraces` | `true` — add braces when missing |
| `SortIncludes` | `false` — keep include order manual |

**Format entire tree** (requires [LLVM clang-format](https://releases.llvm.org/) and Node.js 18+):

```bash
npm run format          # apply fixes
npm run format:check    # verify only (same as CI)
```

Implementation: [`scripts/format.mjs`](scripts/format.mjs) / [`scripts/format-check.mjs`](scripts/format-check.mjs).

**Format one file or a selection:**

```bash
clang-format -i src/volume/volume_manager.cpp
clang-format -i src/volume/volume_manager.h
```

**IDE integration:**

- **Visual Studio:** install the *LLVM* workload or extension; *Tools → Options → Text Editor → C/C++ → Formatting* can point at `.clang-format`. *Edit → Advanced → Format Document* (`Ctrl+K, Ctrl+D`).
- **VS Code / Cursor:** install the *Clang-Format* extension; it picks up `.clang-format` automatically on save if enabled.
- **CLion:** enable *ClangFormat* under *Settings → Editor → Code Style*.

Run `npm run format:check` before opening a PR. See [Continuous integration](#continuous-integration) for the full CI pipeline (format + Windows build).

### Classes and Modules

- One primary class or cohesive module per `.h` / `.cpp` pair.
- **File names:** lowercase **snake_case** (e.g. class `CVolume` → `volume.h` / `volume.cpp`, `CVolumeManager` → `volume_manager.h`). **Class names** keep the `C` prefix per VC++ convention.
- Keep headers minimal; move implementation details to `.cpp` files.
- Prefer `explicit` constructors; mark methods `const` when they do not mutate state.
- Raw `new` / `delete` may appear in low-level disk code; higher layers should use RAII wrappers (`std::unique_ptr`, custom handle guards) where practical without sacrificing hot-path performance.

### Error Handling and Win32

- **`bool` vs `BOOL`:** Use standard C++ **`bool`** (`true` / `false`) in index, volume orchestration, I/O service, and other logic that is not a direct Win32 API surface. Reserve **`BOOL`** for the `ntfs/` layer and code that mirrors Win32 signatures (`CreateFile`, `DeviceIoControl`, …).
- Win32 API failures: check return values; use `GetLastError()` when applicable.
- Low-level Win32 wrappers may return `BOOL`; higher layers should expose `bool` where practical.
- Avoid exceptions across Win32 callback boundaries; use them sparingly in pure C++ layers if at all.

### Comments

- Use `//` for single-line comments; `/* */` only for brief block comments on structures.
- Comment **why**, not what, for non-obvious logic (USN replay rules, MFT fix-up, index invariants).
- Public headers: one-line `///` or brief `//` summary above non-obvious functions is sufficient.

### Example

```cpp
class CVolumeIndex {
public:
    BOOL LoadFromMft(HANDLE hVolumeFile);
    BOOL ApplyUsnRecord(const USN_RECORD* pRecord);
    DWORD SearchByPattern(LPCWSTR wszPattern, PSEARCHRESULT* ppResults) const;

private:
    HANDLE      m_hVolumeFile;
    DWORD       m_dwEntryCount;
    PINDEXNODE  m_pNodes;
    CNamePool   m_namePool;
};
```

New code and refactors must conform to this guide. Pull requests that introduce inconsistent naming or non-VC++ layout will be asked to revise before merge.

---

## License

To be determined.

---

## Status

**Early design phase.** No production-ready implementation yet. This document defines the type-level specification for what the project intends to build.
