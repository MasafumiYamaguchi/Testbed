# Frozen contract 1 compatibility fixture

`calm-front.white.json` is an unmodified file emitted by `fixtures()` in
`tests/frozen_tests.cpp` at commit
`99b585adb1bf84b8f6f27b818895570e5a54f1e0` (schema 10, frozen contract 1).

It was generated after rebuilding the Release target with:

```sh
build/cpu-release/white_frozen_tests /workspace/scratch/59c2b798b94d/frozen-v1-fixtures
```

The original `calm-front-frozen.white.json` was copied without reserializing it.
The persistence test verifies its original hashes before migration and checks
that only contract metadata and hashes change. Variants also exercise unknown
generation versions and absent provenance without a generation job.
