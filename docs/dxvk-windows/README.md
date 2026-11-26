# Windows DXVK D3D9 build

The Windows `-vulkan` renderer loads `dxvk_d3d9.dll` beside `shaderapidx9.dll`.
The tested backend was built from [Star1xr/dxvk-native-android](https://github.com/Star1xr/dxvk-native-android), commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` (project version `v1.9.2`). The source is distributed under the included zlib license. This integration modifies that source with `msvc-inline.patch`, which supplies MSVC's spelling of the force-inline attribute while retaining the original attribute for other compilers.

The source archive used for the build came from `https://api.github.com/repos/Star1xr/dxvk-native-android/tarball/f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` and had SHA-256 `23606f947e81bd2319931c99e4a507083d9ef5d0d8ea08df91c1f7ed711ad4db`.

The tested x64 toolchain was MSVC 19.44.35228, Windows SDK 10.0.26100, Meson 1.12.1, Ninja 1.13.2 and glslangValidator 16.6.0. The glslang Windows x64 release archive at `https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip` had SHA-256 `82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b`.

Extract the source archive and apply `msvc-inline.patch`, `queue-diagnostics.patch`, and `qualcomm-clear-batching.patch` in its root with `git apply`. From an x64 MSVC developer environment, put `glslangValidator.exe` and Ninja on `PATH` and configure the build with the cross file in this directory. The cross file selects an x64 Windows target even when Python and Meson run on an ARM64 host.

```powershell
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
git -C $sourceDir apply $patchFile
git -C $sourceDir apply $queueDiagnosticsPatch
git -C $sourceDir apply $clearBatchingPatch
python -m mesonbuild.mesonmain setup $buildDir $sourceDir --cross-file $crossFile --buildtype=release -Ddebug=true -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false -Denable_tests=false
python -m mesonbuild.mesonmain compile -C $buildDir -j 12
```

Set `$sourceDir` and `$buildDir` to separate source and build directories, and the patch and cross-file variables to the absolute paths of the files in this directory. Meson and Ninja can be installed with `python -m pip install meson==1.12.1 ninja==1.13.2`. The outputs are `$buildDir\src\d3d9\dxvk_d3d9.dll` and its PDB. The tested binary loaded in an isolated x64 process, and both `Direct3DCreate9` and `Direct3DCreate9Ex` enumerated one Vulkan adapter. Its SHA-256 was `e1191e8802ba62794c0a8ef304b7786af3ddcff7a5f9ba163de734509e1671c5`; the matching PDB's SHA-256 was `011b21d7f3b25eefd2a30b429d9a962b921350b7fe6f111da5a6d8c0b0ef8d2d`. Build paths and toolchain differences can change these hashes.

The clear batching patch applies only to native Windows Vulkan on vendor `0x5143`, device `0x36334330`, Qualcomm proprietary driver ID `8`, and Vulkan driver version `0x8037d000` (the tested Adreno X1-85 driver 31.0.178.0). After 128 emitted image clear commands it flushes the existing ordered command stream. The batch size is an experimentally selected workaround, not a documented hardware limit. It can increase the number of submissions and needs performance testing. A driver update or another device does not use this workaround without further verification.

The isolated Viewfinder VHS workload reproduced a graphics queue submission failure without batching. With batching, the unchanged addon loaded, its camera effect was visible, and the isolated map ran for 60 seconds without that failure. The queue diagnostics patch records whether a failed command submission was on the transfer or graphics queue. It does not fix the older DXVK submission error-handling deadlock or establish the underlying Vulkan driver failure.

The user also confirmed that the deployed Windows x64 build loaded without hanging and displayed the other Viewfinder camera presets correctly on the tested Adreno device. Other Windows GPU and driver combinations and Android remain unverified for this workload.
