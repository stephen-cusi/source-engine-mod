# Servo HTML backend

Servo is built separately and loaded at runtime. Its source and binaries are
not vendored in this repository.

The integration branch is `hl2sb-servo-integration` in the
`stephen-cusi/servo` GitHub repository. The build script accepts the pinned
fork commit `43eae6443b8742331bc2d08a27c8ec11001046eb`, or the upstream
commit `facef05f121a7ac657050eb8d5e450188efcb69f` with
`servo-capi.patch` applied. The patch contains the same changes as the fork
commit and should not be applied again to that commit.

Build the separately checked out source with:

```powershell
./tools/build_servo_capi.ps1 -ServoRoot <servo-checkout> -Target x86_64-pc-windows-msvc
```

The Servo toolchain and target dependencies must be installed separately.
On Windows, deploy `servo_capi.dll` and the matching ANGLE `libEGL.dll` and
`libGLESv2.dll` alongside the game binaries. The experimental backend is
selected for newly created Lua HTML panels using `hl2sb_html_servo 1`.
External navigation, Lua callbacks, IME placement, and other browser APIs
are not complete. Other platforms require separate builds and validation.
