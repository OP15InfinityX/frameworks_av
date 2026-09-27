# Camera-only fmt 11 ABI compatibility

The unchanged fmt 11.0.2 sources are from the local Infinity-X 3.12 tree,
`external/fmtlib` commit `d50695f04854e474766da7d713bf8242f4ceb4f8`.
Only `src/format.cc`, its three required headers and the license are included.

OOS 16.0.8 `libcsextimpl.so` imports nine `fmt::v11` functions formerly
provided by Android 16's `libbase.so`. Android 17 moved its fmtlib to v12.
Renaming symbols would not preserve the ABI of fmt's parameter types.

This module is whole-linked into `libcameraservice.so`, which the camera
extension already lists in `DT_NEEDED`. The linker can therefore resolve
the original v11 functions without modifying the blob or the platform's
v12 fmtlib. No old fmt headers are exported to camera or framework code.
