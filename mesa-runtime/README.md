# Mesa Runtime

Bundled Mesa UWP runtime files for local builds live in this folder.

Required. `Test-MesaRuntimeDir` rejects any folder missing one of these, so `build.ps1` cannot resolve a Mesa runtime without them:

- `opengl32.dll`
- `libgallium_wgl.dll`
- `dxil.dll`
- `spirv_to_dxil.dll`
- `z-1.dll`

Copied when present (optional Vulkan support):

- `vulkan-1.dll` — Vulkan loader. Put the UWP-compatible x64 loader in this directory; the build stages it beside the app executable and in `natives\\` so it can be found through the process DLL search path.
- `vulkan_dzn.dll` — Mesa Dozen, a Vulkan ICD implemented on top of Microsoft Direct3D 12.
- `dzn_icd.x86_64.json` — 64-bit Vulkan ICD manifest for the bundled Dozen DLL.

The build copies each available runtime file, including `vulkan-1.dll` when supplied, to the package root, `natives\\`, and `graphics\\mesa\\`. The Minecraft launch setup points `VK_DRIVER_FILES` at the packaged Dozen manifest when it exists.

## Vulkan / D3D12 status

Dozen translates Vulkan calls to D3D12; it is separate from Mesa's existing OpenGL-on-D3D12 driver. The ICD manifest advertises Vulkan API 1.2 for the Java 26.2 target. This must match the actual `vulkan_dzn.dll`: use a Mesa 26.2 Dozen build (or another build verified to expose Vulkan 1.2). Do not raise this value just to label an older driver as 1.2.

This is an experimental integration, not a claim that Vulkan is already working on Xbox. The Vulkan loader (`vulkan-1.dll`) must be supplied in this directory for the build to package it, and the D3D12 device/runtime must be accessible from the app sandbox. Those conditions and actual Minecraft/LWJGL Vulkan startup still need to be verified on Xbox Series S|X. Vulkan 1.2 availability also depends on the packaged Dozen driver and the Xbox D3D12 feature support; the manifest value alone cannot add missing driver features. The launcher keeps the existing OpenGL/GLFW path as the default; selecting an ICD does not switch Minecraft's renderer to Vulkan by itself.

Use another Mesa runtime with:

```powershell
.\\build.ps1 -MesaRuntimeDir "C:\\path\\to\\mesa-runtime"
```

Or:

```powershell
$env:MESA_UWP_DIR = "C:\\path\\to\\mesa-runtime"
```
