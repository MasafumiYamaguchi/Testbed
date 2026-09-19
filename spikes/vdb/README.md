# Optional OpenVDB feasibility spike (Issue #13)

This standalone executable proves a Windows C++ build and a single density-grid
write/reopen path. Normal preview builds leave WHITE_BUILD_VDB_SPIKE=OFF and do
not resolve or link OpenVDB. It is not the production export feature.

The vcpkg source and manifest baseline are pinned to
`e6f9e70a29a3e80a1fc510d8503304315447112f`, selecting OpenVDB 12.0.1.
No NanoVDB, CUDA, AX, Python or viewer feature is requested. The port still needs
Blosc, Boost, Imath/OpenEXR and TBB; CI bundles installed package versions and
individual copyright notices with the executable and app-local DLLs.
OpenVDB is Apache-2.0; transitive licenses are recorded from the actual installation.

Configure independently with vcpkg's toolchain and x64-windows triplet:

```powershell
cmake -S spikes/vdb -B build/vdb -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build/vdb --config Release --parallel 2
ctest --test-dir build/vdb -C Release --verbose
```

Alternatively enable the root CMake option with the same toolchain and
`VCPKG_MANIFEST_DIR` pointing to this directory. The separate CI job exercises
the standalone entrypoint; ordinary native CI verifies the dependency stays optional.

The test creates a 32³ asymmetric nonnegative FloatGrid named `density`, fog
class, zero background, 0.75 m voxel spacing and nonzero world origin
(11.25, -3.5, 5.75) m. After closing the writer it reopens all data without delayed
loading and checks all 34³ samples including an exterior border, exact values,
active count and index-to-world mapping. Asymmetry detects axis swaps. The version
and ABI are printed by the linked library headers and the executable runs against
the bundled DLLs. The VDB file is an inspectable CI artifact.

Writing reserves an owned sibling temporary directory. Only a fully closed,
reopened and verified file is published by same-filesystem hard-link creation,
which refuses an existing destination atomically. NTFS supports this operation;
unsupported filesystems fail without overwriting the destination. Tests cover an
existing destination, injected pre-publication failure, invalid parent path and
cleanup of owned temporary files. This is no-overwrite publication, not a claim
of power-loss durability. Unicode Windows paths and external DCC import remain
unverified and are not production-export acceptance evidence.

Sources: [pinned vcpkg port](https://github.com/microsoft/vcpkg/tree/e6f9e70a29a3e80a1fc510d8503304315447112f/ports/openvdb),
[OpenVDB file examples](https://www.openvdb.org/documentation/doxygen/codeExamples.html).
