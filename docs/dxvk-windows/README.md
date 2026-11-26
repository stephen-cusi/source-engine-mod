# Windows DXVK D3D9 build

The Windows `-vulkan` renderer loads `dxvk_d3d9.dll` beside `shaderapidx9.dll`.
The tested backend was built from [Star1xr/dxvk-native-android](https://github.com/Star1xr/dxvk-native-android), commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` (project version `v1.9.2`). The source is distributed under the included zlib license. This integration modifies that source with `msvc-inline.patch`, which supplies MSVC's spelling of the force-inline attribute while retaining the original attribute for other compilers.

The source archive used for the build came from `https://api.github.com/repos/Star1xr/dxvk-native-android/tarball/f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` and had SHA-256 `23606f947e81bd2319931c99e4a507083d9ef5d0d8ea08df91c1f7ed711ad4db`.

The tested x64 toolchain was MSVC 19.44.35228, Windows SDK 10.0.26100, Meson 1.12.1, Ninja 1.13.2 and glslangValidator 16.6.0. The glslang Windows x64 release archive at `https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip` had SHA-256 `82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b`.

Extract the source archive and apply `msvc-inline.patch` in its root with `git apply`. From an x64 MSVC developer environment, put `glslangValidator.exe` and Ninja on `PATH` and configure the build with the cross file in this directory. The cross file selects an x64 Windows target even when Python and Meson run on an ARM64 host.

```powershell
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
git -C $sourceDir apply $patchFile
python -m mesonbuild.mesonmain setup $buildDir $sourceDir --cross-file $crossFile --buildtype=release -Ddebug=true -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false -Denable_tests=false
python -m mesonbuild.mesonmain compile -C $buildDir -j 12
```

Set `$sourceDir` and `$buildDir` to separate source and build directories, and `$patchFile` and `$crossFile` to the absolute paths of the files in this directory. Meson and Ninja can be installed with `python -m pip install meson==1.12.1 ninja==1.13.2`. The outputs are `$buildDir\src\d3d9\dxvk_d3d9.dll` and its PDB. The tested binary loaded in an isolated x64 process, and both `Direct3DCreate9` and `Direct3DCreate9Ex` enumerated one Vulkan adapter. Its SHA-256 was `37675cbde02e3b9562d2355e992c901274083c3aadcaf753d02c1b2949afc399`; the matching PDB's SHA-256 was `c1c46b0d1b8a8e3f71296ee7e331e5e4eb5693ea1abdda514533cb262297de43`. Build paths and toolchain differences can change these hashes.

This record covers the Windows x64 build. Runtime graphics and driver behavior still require testing in the game.
