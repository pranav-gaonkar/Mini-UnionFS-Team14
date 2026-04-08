# Mini-UnionFS Design Document

## 1. Goals & Constraints
Mini-UnionFS emulates the layered filesystem semantics used by container runtimes: a writable "upper" layer sits above one or more read-only "lower" layers. The filesystem must:

- Present a merged directory tree when mounted via FUSE 3.
- Prefer upper-layer entries when a path exists in both branches.
- Copy lower-layer files into the upper layer lazily (Copy-on-Write) before the first modification.
- Hide deleted lower-layer entries by creating whiteout markers in the upper layer.
- Support the POSIX operations listed in the project brief (`getattr`, `readdir`, `read`, `write`, `create`, `unlink`, `mkdir`, `rmdir`) plus the helpers (`open`, `release`, `truncate`, `utimens`, `access`) required for practical workloads.
- Remain entirely in user space, touching lower directories read-only and upper directories read-write.

The implementation targets Linux with libfuse3. All paths in global state are stored as canonical absolute paths to avoid ambiguity when forming child paths.

## 2. Architecture Overview
```
+---------------------------+
|        Mini-UnionFS       |
|                           |
|  FUSE callbacks --------- | ----> libfuse kernel interface
|    |                      |
|    v                      |
|  Path Resolver + Policy   | ----> decides upper/lower/whiteout
|    |                      |
|    v                      |
|  Branch Adapters          | ----> POSIX syscalls (open, read, ...)
+---------------------------+
```

### 2.1 Global State
`MiniUnionFSState` holds two absolute directories:
- `lower_dir`: read-only branch containing the base image.
- `upper_dir`: writable branch storing Copy-on-Write results and whiteouts.

The pointer to this struct is passed to `fuse_main` as `private_data`, making it accessible inside every callback through `fuse_get_context()`.

### 2.2 Path Handling Helpers
- `rel_from_fuse(path)` strips the leading `/` from a FUSE path and yields an `std::filesystem::path` that can be appended to either branch root.
- `make_branch_path(base, rel)` constructs the absolute path to a file in a specific branch.
- `make_whiteout_path(state, rel)` produces the `.wh.<name>` path inside the upper branch that shadows a lower entry.
- `resolve_path(path)` yields a `(origin, absolute_path)` tuple after checking, in order, for an upper whiteout, an upper real entry, and a lower entry.

All helpers lean on `std::filesystem` for path concatenation and canonicalization but fall back to raw POSIX syscalls for metadata (`lstat`, `access`, etc.) to respect symlinks.

## 3. Core Mechanisms

### 3.1 Whiteout Management
Deleting an entry that originates from a lower layer creates a zero-byte `.wh.<name>` file in the equivalent upper directory. When the resolver spots such a whiteout it returns `EntryOrigin::Whiteout`, causing subsequent operations to report `ENOENT`. Whiteouts are removed whenever a new file or directory with the same name is created in the upper layer.

### 3.2 Copy-on-Write (CoW)
`ensure_cow(path, for_write)` guards any write-like operation:
1. Skip if the call is read-only.
2. Compute the upper and lower absolute paths.
3. If the upper copy already exists, nothing more is required.
4. If the lower entry exists, replicate it into the upper branch:
   - Regular files are streamed with `read`/`write` loops.
   - Symlinks capture their target with `readlink`/`symlink` without resolution.
   - Directories are recreated with matching permissions.
5. Upon success, remove any lingering whiteout so future lookups pick the new upper entry.

Any attempt to modify a path that exists only in the lower branch will therefore materialize a private copy before the user-visible change takes place.

### 3.3 Directory Merging (`readdir`)
`readdir` scans the upper branch first, recording every emitted name in an `unordered_set`. Upper whiteouts (`.wh.*`) populate a `whiteouts` set. The lower directory is subsequently scanned, but entries that appear in either `whiteouts` or `emitted` are skipped. This guarantees:
- Upper entries override lower ones.
- Lower entries hidden by whiteouts never surface.
- Duplicate names are emitted exactly once.

### 3.4 File Descriptor Lifetime
`open` stores the real POSIX file descriptor in `fi->fh`. `read`, `write`, and `release` operate directly on that descriptor, avoiding redundant string-based opens per call. All writes use `pwrite`/`pread` to stay thread-safe and offset-aware.

## 4. Operation Mapping
| FUSE Operation | Responsibilities |
| --- | --- |
| `getattr` | Resolve path; honor whiteouts; fall back to synthetic attributes for `/`. Uses `lstat` to preserve symlink metadata.
| `access` | Shortcut for permission checks via `::access` after resolution.
| `readdir` | Merge directory listings while filtering whiteouts.
| `open` | Trigger CoW on the first write; open backing file with original flags and store descriptor.
| `read` / `write` | Use `pread`/`pwrite` against stored descriptor.
| `release` | Close the descriptor; no extra flushing required.
| `truncate` / `utimens` | Treat as write operations so they force CoW beforehand.
| `create` | Allocate the file directly in the upper layer, ensuring parent directories exist and clearing conflicts with prior whiteouts.
| `unlink` | If an upper copy exists, remove it and optionally create a whiteout when a lower copy also exists; otherwise drop a whiteout that hides the lower file.
| `mkdir` | Build directories in the upper branch and remove conflicting whiteouts.
| `rmdir` | Remove an existing upper directory and, when a lower directory remains, add a whiteout so it stays hidden. If only a lower directory exists, create the whiteout immediately.

## 5. Edge Cases & Guarantees
- **Root Directory:** `/` never maps to a physical path; `getattr` synthesizes attributes while `readdir` joins `upper_dir` and `lower_dir` roots.
- **Missing Parents:** `ensure_parent_dirs` calls `std::filesystem::create_directories` before any creation, so nested files can appear even if intermediate directories only existed in the lower branch.
- **Whiteout Cleanup:** Creating or copying up an entry automatically removes a stale `.wh.*` file in the same directory; deleting an upper entry adds a fresh whiteout when a lower entry should remain hidden.
- **Symlinks:** CoW reads the symlink target without dereferencing it, recreating the link verbatim in the upper layer.
- **Concurrency:** libfuse may issue parallel requests; the implementation relies on kernel-level serialization for conflicting syscalls on the same path. All helper functions avoid shared global structures beyond the immutable state pointer, so no extra locking is required.
- **Error Propagation:** All syscalls return `-errno` on failure so user space sees standard POSIX error codes.

## 6. Testing Strategy
`scripts/test_unionfs.sh` is a reproducible smoke suite mirroring Appendix B:
1. **Layer Visibility:** ensure lower-only files appear through the mount.
2. **Copy-on-Write:** append to a lower file and verify that only the upper copy changes.
3. **Whiteout:** delete a lower-only file and confirm the appearance of a `.wh.*` marker alongside the continued presence of the base file in `lower_dir`.

The script builds a temporary environment, mounts the filesystem in foreground mode, executes each check, and cleanly unmounts the mountpoint even on failure. Additional manual testing is recommended for nested directories, symlink CoW, and concurrent file writers.

## 7. Future Enhancements
- Support multiple stacked lower layers by iterating through an ordered list instead of a single directory.
- Persist extended attributes and finer-grained metadata (timestamps, ownership) during CoW with `futimens`/`fchown`.
- Implement more FUSE callbacks (`rename`, `link`, `statfs`, `chmod`, etc.) for broader compatibility with general-purpose workloads.
- Introduce per-file locks or reference counting to better coordinate concurrent writes from multiple processes.
