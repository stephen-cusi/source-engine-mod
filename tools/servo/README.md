# Servo HTML backend

The Linux desktop client loads Servo at runtime. Its source and binaries are
maintained separately from this repository.

The integration branch is `hl2sb-servo-integration` in the
`stephen-cusi/servo` GitHub repository. The build script accepts the pinned
fork commit `43eae6443b8742331bc2d08a27c8ec11001046eb`, or the upstream
commit `facef05f121a7ac657050eb8d5e450188efcb69f` with
`servo-capi.patch` applied. The patch contains the same changes as the fork
commit and should not be applied again to that commit.

On a Linux host with the Servo toolchain and build dependencies installed, run
this command from the separately checked out Servo source:

```sh
cargo build --locked -j 12 --release -p servo-capi
```

Place the matching `libservo_capi.so` at `bin/libservo_capi.so` under the game
directory. The Lua main menu attempts this backend by default. To use Servo
for new in-game Lua HTML panels, set `hl2sb_html_servo 1`.
External navigation, Lua callbacks, IME placement, and other browser APIs
are not complete.
