# Building

This page covers a clean local build from the repo root.

The build produces a signed UWP package at:

```text
output\BanditLauncher_1.0.0.0.appx
```

## Requirements

- Windows with PowerShell 5.1 or newer.
- Visual Studio or Visual Studio Build Tools with the MSVC x64 C++ tools.
- Windows 10 or Windows 11 SDK.
- JDK 21 or newer. Set `JAVA_HOME` if auto detection does not find it.
- Python 3 with Pillow.
- Desktop install of Git `https://git-scm.com/install/windows`
- Fabric installer JAR at `staging\cache\tools\fabric-installer.jar`.
- Minecraft/LWJGL native DLLs in `staging\cache\natives-1.21`.
- Mesa UWP runtime DLLs in `mesa-runtime\`, or another folder passed to the build.

Install Pillow:

```powershell
python -m pip install pillow
```

## Versions

Default versions live in `scripts/config.ps1`.

Current defaults:

- Minecraft: `1.21.11`
- Asset index: `29`
- Fabric Loader: `0.19.2`
- Java release: `21`
- LWJGL GLFW natives: `3.3.3`
- JNA: `5.17.0`

The main build accepts temporary overrides:

```powershell
.\build.ps1 -McVersion 1.21.11 -FabricLoader 0.19.2 -AssetIndex 29
```

These values are passed into setup helpers and into the generated host header used by `MC.Xbox\App.cpp`.

For a real version bump, update:

- `scripts/config.ps1`
- `compat_mod/src/main/resources/fabric.mod.json`
- `MC.Xbox/launch.ps1` if you still use that legacy launch helper

Then recreate the local `gameDir`, natives, remapped jars, and patched Fabric Loader. The installed app downloads official runtime libraries, client jars, and assets into UWP `LocalState`; they are not bundled into the APPX.

## Mesa runtime

The repo includes the Mesa UWP runtime folder:

```text
mesa-runtime\
```

You can use a different runtime with:

```powershell
.\build.ps1 -MesaRuntimeDir "C:\path\to\mesa-runtime"
```

Or:

```powershell
$env:MESA_UWP_DIR = "C:\path\to\mesa-runtime"
```

`RETROARCH_UWP_DIR` is still accepted as a fallback search path. RetroArch is not required.

## Xbox One graphics runtime

Series consoles keep using the Mesa runtime above. Xbox One can use a separate
runtime packaged under `graphics\xboxone\`. The repo-level source folder is:

```text
xboxone-runtime\
```

You can use a different source folder with:

```powershell
.\build.ps1 -XboxOneGraphicsRuntimeDir "C:\path\to\xboxone-graphics-runtime"
```

Or:

```powershell
$env:XBOX_ONE_GRAPHICS_RUNTIME_DIR = "C:\path\to\xboxone-graphics-runtime"
```

That folder must contain at least:

```text
opengl32.dll
libEGL.dll
libGLESv2.dll
```

The RetroArch Xbox One package provides ANGLE `libEGL.dll` and `libGLESv2.dll`,
but not `opengl32.dll`. A UWP-built MobileGlues `opengl32.dll` is expected to
fill that role. If the Xbox One runtime is incomplete, the build still succeeds
and the app falls back to the Series/Mesa path at runtime.

## Fresh setup

Create the local cache folders and place the Fabric installer here:

```text
staging\cache\tools\fabric-installer.jar
```

Download the Minecraft client libraries for the local build cache:

```powershell
.\scripts\download-libs.ps1
```

If you need to regenerate Fabric remapped jars from scratch, also download the local asset cache used by the desktop Fabric launch helper:

```powershell
.\scripts\download-assets.ps1
```

Run the Fabric installer:

```powershell
java -jar .\staging\cache\tools\fabric-installer.jar client -dir .\staging\cache\gameDir -mcversion 1.21.11 -loader 0.19.2 -launcher win32 -noprofile
```

Put the needed Minecraft and LWJGL native DLLs here:

```text
staging\cache\natives-1.21\
```

That folder is local only and ignored by git.

To obtain those, use the official Minecraft launcher, create a 1.21.11 instance, launch it fully past the accessibility screen, close the game, then go to ".minecraft" in your appdata folder, search "*.dll" and grab these DLLs:

```text
glfw.dll
jemalloc.dll
lwjgl.dll
lwjgl_opengl.dll
lwjgl_stb.dll
OpenAL.dll
```

## Microsoft sign-in

The packaged app signs in dynamically. You no longer need to create or bundle a
local auth JSON file.

On first launch, the app shows a Microsoft device-code screen before Minecraft
starts. Go to `https://www.microsoft.com/link`, enter the displayed code, and
sign in with the Microsoft account that owns Minecraft Java Edition. The QR code
on that screen opens the same Microsoft link flow.

After sign-in, the app exchanges the Microsoft token through Xbox Live, XSTS,
and Minecraft Services, checks Java Edition ownership, and passes the resolved
Minecraft username, UUID, and access token into the embedded JVM.

## Runtime downloads

The APPX contains only launcher-owned runtime pieces and intentionally patched
files:

```text
MC.Xbox.exe
jre\
natives\
graphics\
runtime\libraries\...\fabric-loader-<version>.jar
runtime\bundled-mods\
runtime\log_configs\
download_manifest.tsv
```

It does not contain Mojang libraries, Minecraft client jars, Minecraft version
JSON files, asset indexes, asset objects, or Fabric remapped jars. During build,
`scripts\new-download-manifest.ps1` writes `download_manifest.tsv` from official
Mojang and Fabric metadata. During launch, after ownership verification,
`MC.Xbox.exe` verifies the manifest entries under `LocalState` and downloads any
missing or stale files.

Downloaded runtime files land under:

```text
LocalState\game\libraries
LocalState\game\versions
LocalState\assets
```

The next launch verifies hashes and skips files that are already present.

The launcher also writes a small manifest marker in `LocalState`. If the
packaged manifest changes, the app removes old downloaded official runtime
folders before verifying the new manifest. The signed-in menu includes
`Repair downloads`, which forces the same cleanup and re-download path for
corrupt or suspect local files.

Missing or stale files download with limited parallelism. The first stable
setting is six workers so the UI stays responsive and the Xbox storage path is
not flooded with thousands of simultaneous asset writes.

## Generate Fabric remapped jars

Fabric remapped jars are created by running the Fabric client once on the local desktop cache. This step is needed before the compatibility mod can compile.

Run this from the repo root:

```powershell
$gameDir   = (Resolve-Path .\staging\cache\gameDir).Path
$assetsDir = (Resolve-Path .\staging\cache\assets).Path
$clientJar = "$gameDir\versions\1.21.11\1.21.11.jar"
$jars = @()
Get-ChildItem -Recurse "$gameDir\libraries" -Filter "*.jar" | ForEach-Object { $jars += $_.FullName }
$jars += $clientJar
$cp = $jars -join ';'

$javaArgs = @(
    "-Dfabric.gameJarPath=$clientJar"
    "-Duser.dir=$gameDir"
    "-cp"; $cp
    "net.fabricmc.loader.impl.launch.knot.KnotClient"
    "--gameDir";     $gameDir
    "--assetsDir";   $assetsDir
    "--assetIndex";  "29"
    "--version";     "fabric-loader-0.19.2-1.21.11"
    "--username";    "DevPlayer"
    "--uuid";        "00000000-0000-0000-0000-000000000000"
    "--accessToken"; "0"
    "--versionType"; "release"
)
& "$env:JAVA_HOME\bin\java.exe" @javaArgs
```

After setup, these local build-cache paths should exist:

```text
staging\cache\gameDir\libraries\net\fabricmc\fabric-loader\0.19.2\fabric-loader-0.19.2.jar
staging\cache\gameDir\.fabric\remappedJars\minecraft-1.21.11-0.19.2\client-intermediary.jar
staging\cache\natives-1.21\
```

If you ran `download-assets.ps1`, `staging\cache\assets\indexes\29.json` should also exist. Runtime assets are still downloaded by the installed app, not copied into the APPX.

If all of those are there, put launcher-owned or explicitly allowed mod jars into `staging\cache\gameDir\mods`. The compatibility mod is generated there automatically by the build.

## Patch Fabric Loader

The top level build runs this step automatically. You can also run it directly:

```powershell
.\scripts\patch-fabric.ps1
```

The script overlays patched Fabric Loader classes into the local ignored loader JAR under `staging\cache\gameDir`.
The package step then copies only that patched loader jar into `runtime\libraries`.

## Build package

Run:

```powershell
.\build.ps1
```

Useful options:

- `-KeepStaging` keeps `staging\package` after packaging.
- `-SkipStopAppProcesses` skips the process cleanup that runs before packaging.
- `-StopFileLockers` asks Windows Restart Manager to find processes that lock the output appx.
- `-MesaRuntimeDir` points at another Mesa runtime folder.
- `-McVersion`, `-FabricLoader`, and `-AssetIndex` override version values for this build.

The build script:

1. Generates `runtime_config.h` for the selected versions.
2. Builds `MC.Xbox.exe`.
3. Builds the UWP GLFW shim.
4. Builds the Fabric compatibility mod.
5. Patches the local Fabric Loader JAR.
6. Assembles `staging\package`.
7. Copies the patched Fabric Loader, bundled mods, log config, natives, Mesa/MobileGlues graphics DLLs, and the JRE.
8. Generates `download_manifest.tsv` for official Minecraft/Fabric runtime downloads.
9. Generates UWP tile and splash assets from `MC.Xbox\Assets\Java_UWP_Icon.png`.
10. Creates and signs `output\BanditLauncher_1.0.0.0.appx`.
11. Deletes `staging\package` unless `-KeepStaging` is set.

## Clean outputs

Preview cleanup:

```powershell
.\scripts\clean.ps1
```

Apply cleanup:

```powershell
.\scripts\clean.ps1 -Apply
```

Default cleanup removes build outputs only:

```text
staging\build
staging\package
staging\certs
output
```

To include all ignored files, including downloaded cache files:

```powershell
.\scripts\clean.ps1 -Scope AllIgnored
.\scripts\clean.ps1 -Scope AllIgnored -Apply
```

## Troubleshooting

- `No suitable Java installation found`: set `JAVA_HOME` to a JDK 21 or newer install.
- `No suitable Python installation found`: install Python 3 and Pillow, or set `PYTHON`.
- `vswhere.exe not found`: install Visual Studio Build Tools with C++ tools.
- `Mesa UWP runtime DLLs not found`: pass `-MesaRuntimeDir`, set `MESA_UWP_DIR`, or restore `mesa-runtime\`.
- Missing `client-intermediary.jar`: run the Fabric client once from the local desktop cache as shown above.
- Missing native DLLs: fill `staging\cache\natives-1.21` with the native DLLs required by the Minecraft and LWJGL runtime.
- First launch downloads every required official file after sign-in. A later launch should verify and skip already-downloaded files.
- Runtime download failure: check `LocalState\mc_launch.log` for the manifest path, URL, HTTP status, or SHA1 mismatch.
- Package signing failure: delete the ignored local `.pfx` under `staging\certs` and rerun `build.ps1`, or set `APPX_CERT_SUBJECT`.
- If you can't find your appdata folder, type `%appdata%` into your address bar in your file explorer.
