# 0004: Transactional scene files and edit history

Status: implemented for Issue #6; UI wiring is Issue #9.

Native files are schema-1 JSON, capped at 4 MiB and 32 nesting levels. IDs and
64-bit seeds are decimal strings so JS tooling cannot round values above 2^53.
Every Scene field is round-tripped; unknown members, missing members, invalid
types, unknown versions and invalid scene values fail before publication.
JSON parsing uses pinned nlohmann/json 3.12.0 (MIT). No GPU cache is serialized.

Save writes an exclusively created temporary sibling file, checks every write,
flushes and closes it, then publishes with same-filesystem replacement: POSIX
rename or Windows MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH). A failed write or
publish removes only this operation's temp file and preserves the old target.
Parent directories are not silently created. This is atomic file publication,
not a claim of universal crash durability on all/network filesystems; POSIX
parent-directory fsync and concurrent external-writer conflict resolution are
not provided. Deliberate save replaces the selected target on success.

EditorSession owns Document. Every accepted parameter change/add/delete passes
validation and uses Document::replace. Undo and Redo apply snapshots through the
same path, so revisions always advance. A drag publishes preview revisions but
records one history entry on completion; cancellation restores its baseline with
a newer revision. Undo/Redo/save/load are unavailable during a drag. Histories
are bounded to 128 entries, and branch edits clear Redo.

Modified state compares against the last successfully saved/loaded Scene. Failed
save never marks clean. Load parses/validates a temporary Scene first and refuses
unsaved changes unless its caller explicitly chooses discard. UI confirmation is
left to Issue #9; the core API cannot accidentally discard by calling load(path).
Failed parsing leaves Scene, revision and history unchanged.

Tests include exact round-trip, 64-bit boundaries, corrupt/future/deep/oversized
input, real destination failures, injected failure before writing and before
publication, temp cleanup, Undo/Redo branch/drag/cancel, dirty state and history cap.
