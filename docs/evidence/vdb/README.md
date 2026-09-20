# OpenVDB spike verified on Windows

[Actions run](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35420096687),
code commit `0b0a36094dabd2309ed50d89c5693cd72d4ce886`, Release x64.
OpenVDB 12.0.1 / ABI 12.0.1abi12. Test completed in 0.09 seconds.
The 37,327-byte `density.vdb` reopens as one FloatGrid, fog class, zero background,
32,768 active values. All 39,304 interior/border points and world coordinates
match exactly. Existing-file collision, pre-publication failure, invalid parent
and temporary cleanup checks pass. See `roundtrip.log`.

The pinned installation includes Boost 1.92.0, TBB 2023.1.0, Imath 3.2.2,
OpenEXR 3.4.15, Blosc 1.21.6, LZ4 1.10.0, Zstd 1.5.7, zlib 1.3.2 port revision 2,
and libdeflate 1.26. `installed-status.txt` records every package/feature/version.
The Actions artifact contains 77 dependency copyright notices plus the project
license. Runtime distribution contains `white_vdb_spike.exe`, `openvdb.dll`,
`Imath-3_2.dll`, `tbb12.dll`, `blosc.dll`, `lz4.dll`, `z.dll`, and `zstd.dll`.
Build-time dependencies need not each have a runtime DLL.

The optional target is viable for the small contract. Normal GPU builds passed
without it. External DCC import, Unicode paths, production export scale and
power-loss durability remain unverified; this result makes no claim for them.
