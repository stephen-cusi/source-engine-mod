# Servo C API probe

This standalone probe exercises the official Servo C API at commit
`facef05f121a7ac657050eb8d5e450188efcb69f`. It does not integrate with the
game or implement GMod DHTML. It dynamically loads a separately built matching
`servo_capi` shared library and its runtime dependencies.

Build with CMake:

```
cmake -S tools/servo_probe -B build/servo_probe
cmake --build build/servo_probe --config Release
```

The probe and Servo library must have the same platform and architecture.
On Windows, Servo's ANGLE runtime libraries must also be discoverable by the
loader. Their availability depends on how `servo-capi` was built.

Run with a red test page and verify all pixels:

```
servo_probe path/to/servo_capi.dll
```

An optional second argument loads a different URL, and an optional third
argument chooses the PPM output path. For a custom URL, success means a
screenshot was captured; the page's content is not automatically verified.
The test must be able to create Servo's software rendering context and may
not work on all graphics drivers. A missing library, dependency, symbol,
failed context, timeout, or incorrect red pixels causes a nonzero exit code.

Servo and its webviews are created, pumped, and destroyed on the same thread.
The screenshot data is copied to a PPM file while the callback owns it. This
probe does not test input, JavaScript bridge callbacks, asset URLs, Android,
or VGUI texture upload.
