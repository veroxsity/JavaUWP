# Mesa Runtime

Bundled Mesa UWP runtime files for local builds live in this folder.

Required. `Test-MesaRuntimeDir` rejects any folder missing one of these, so `build.ps1` cannot resolve a Mesa runtime without them:

- `opengl32.dll`
- `libgallium_wgl.dll`
- `dxil.dll`
- `spirv_to_dxil.dll`
- `z-1.dll`

Copied when present:

- `vulkan_dzn.dll` — Mesa Dozen, a Vulkan ICD implemented on top of Microsoft Direct3D 12.
- `dzn_icd.x86_64.json` — 64-bit Vulkan ICD manifest for the bundled Dozen DLL.

The build copies each available runtime file to the package root, `natives\\`, and `graphics\\mesa\\`. The Minecraft launch setup points `VK_DRIVER_FILES` at the packaged Dozen manifest when it exists.

## Vulkan / D3D12 status

Dozen translates Vulkan calls to D3D12; it is separate from Mesa's existing OpenGL-on-D3D12 driver. The manifest currently advertises Vulkan API 1.1, matching the manifest generation option used by the upstream Windows Mesa build recipe.

This is an experimental integration, not a claim that Vulkan is already working on Xbox. The Vulkan loader (`vulkan-1.dll`) must also be available to the UWP process, and the D3D12 device/runtime must be accessible from the app sandbox. Those conditions and actual Minecraft/LWJGL Vulkan startup still need to be verified on Xbox Series S|X. The launcher keeps the existing OpenGL/GLFW path as the default; selecting an ICD does not switch Minecraft's renderer to Vulkan by itself.

Use another Mesa runtime with:

```powershell
.\\build.ps1 -MesaRuntimeDir "C:\\path\\to\\mesa-runtime"
```

Or:

```powershell
$env:MESA_UWP_DIR = "C:\\path\\to\\mesa-runtime"
```
