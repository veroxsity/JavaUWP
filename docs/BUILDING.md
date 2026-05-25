# Building

This repo ships the source and the Mesa UWP runtime needed for local builds.
Downloaded game files, temporary package assembly, local notes, and certificates
still live under ignored local directories.

## Requirements

- Windows with PowerShell 5+.
- Visual Studio or Visual Studio Build Tools with the MSVC x64 toolchain.
- Windows 10/11 SDK.
- JDK 21 or newer. Set `JAVA_HOME` if it is not in a common location.
- Python 3 with Pillow:

```powershell
py -m pip install pillow
```

- Fabric installer JAR placed at `staging\cache\tools\fabric-installer.jar`.
- A Mesa UWP runtime directory at `mesa-runtime\` containing:
  `libEGL.dll`, `libGLESv2.dll`, `opengl32.dll`, `libgallium_wgl.dll`,
  `libglapi.dll`, `glu32.dll`, `dxil.dll`, and `z-1.dll`.

If you need to test with a different Mesa build, point the build at another
directory with `-MesaRuntimeDir` or `MESA_UWP_DIR`.

The bundled DLLs live in the tracked runtime folder:

```text
mesa-runtime\
```

Alternatively, pass the path when building:

```powershell
.\build.ps1 -MesaRuntimeDir "C:\path\to\mesa-uwp-runtime"
```

Or set an environment variable:

```powershell
$env:MESA_UWP_DIR = "C:\path\to\mesa-uwp-runtime"
```

`RETROARCH_UWP_DIR` is still accepted as a backward-compatible fallback.

## Project Settings

Version-sensitive settings live in `scripts/config.ps1`.

Current defaults:

- Minecraft: `1.21.11`
- Fabric Loader: `0.19.2`
- Java release: `21`
- LWJGL GLFW natives: `3.3.3`
- JNA: `5.17.0`

If you change Minecraft or Fabric versions, also update runtime metadata in:

- `MC.Xbox/App.cpp`
- `MC.Xbox/launch.ps1`
- `compat_mod/src/main/resources/fabric.mod.json`

Those files are used inside the packaged app and cannot read the PowerShell
config at runtime.

## Fresh Setup

From the repo root:

```powershell
.\scripts\download-libs.ps1
.\scripts\download-assets.ps1
java -jar .\staging\cache\tools\fabric-installer.jar client -dir .\staging\cache\gameDir -mcversion 1.21.11 -loader 0.19.2 -launcher win32 -noprofile
```

Make sure these paths exist after setup:

```text
staging\cache\gameDir\libraries\net\fabricmc\fabric-loader\0.19.2\fabric-loader-0.19.2.jar
staging\cache\gameDir\.fabric\remappedJars\minecraft-1.21.11-0.19.2\client-intermediary.jar
staging\cache\assets\indexes\29.json
staging\cache\natives-1.21\
```

The `staging\cache\natives-1.21` directory is local-only. Put the native DLLs
needed by the Minecraft/LWJGL runtime there. It is ignored by git.

## Patch Fabric Loader

Run:

```powershell
.\scripts\patch-fabric.ps1
```

This injects `patch/LoaderUtil.java` into your local Fabric Loader JAR to avoid
the Xbox sandbox path canonicalization failure.

## Run Fabric environment to generate missing files for building

Run:

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
This generates the remaining missing files so the build script doesn't crash.

## Build Package

Run:

```powershell
.\build.ps1
```

The build script:

1. Builds `MC.Xbox.exe`.
2. Builds the `glfw.dll` CoreWindow shim.
3. Builds the compatibility Fabric mod.
4. Assembles `staging\package`.
5. Injects the GLFW shim into the LWJGL GLFW native JAR.
6. Copies Mesa runtime DLLs.
7. Copies assets, game files, natives, and the JRE.
8. Generates placeholder UWP tile assets.
9. Creates and signs `output\MC_Java_1.0.0.0.appx`.
10. Removes `staging\package` unless `-KeepStaging` is passed.

Generated outputs are ignored by git.

## Clean Local Outputs

Preview cleanup:

```powershell
.\scripts\clean.ps1
```

Apply cleanup:

```powershell
.\scripts\clean.ps1 -Apply
```

The default cleanup removes build outputs only: `staging\build`,
`staging\package`, `staging\certs`, `output`, and legacy root build artifacts.
It keeps the cache under `staging\cache`.

For a full ignored-file cleanup, including downloaded cache files:

```powershell
.\scripts\clean.ps1 -Scope AllIgnored
.\scripts\clean.ps1 -Scope AllIgnored -Apply
```

## Troubleshooting

- `No suitable Java installation found`: set `JAVA_HOME` to a JDK 21 or newer
  install.
- `vswhere.exe not found`: install Visual Studio Build Tools with C++ tools.
- `Mesa UWP runtime DLLs not found`: pass `-MesaRuntimeDir`, set
  `MESA_UWP_DIR`, or restore the bundled DLLs in `mesa-runtime\`.
- Missing `client-intermediary.jar`: rerun the Fabric install step and make sure
  the Minecraft/Fabric versions match `scripts/config.ps1`.
- Package signing failure: delete the ignored local `.pfx` and rerun
  `build.ps1`, or set `APPX_CERT_SUBJECT` before building.
