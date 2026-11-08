# Third Party Components

Components in this repository that originate elsewhere, and the terms they carry.
The repository's own `LICENSE` covers original project code only. It does not
override anything listed here.

See `docs/LEGAL.md` for redistribution rules and `docs/PATCHING.md` for what each
patch does and how it is applied.

## Vendored sources under `patch/`

These are upstream source files copied into this repository and modified for the
Xbox UWP sandbox. Every file carries a header naming its upstream, its license,
and what changed. They are compiled at build time and overlaid into the matching
upstream jar, so no upstream jar is redistributed from this repository.

| Files | Upstream | Version | License |
|---|---|---|---|
| `FabricLauncherBase.java`, `FileSystemUtil.java`, `LoaderUtil.java` | [Fabric Loader](https://github.com/FabricMC/fabric-loader) | applied against 0.19.2 and 0.14.25 | Apache-2.0 |
| `FileSystemHandler.java`, `FileSystemReference.java`, `OutputConsumerPath.java` | [tiny-remapper](https://github.com/FabricMC/tiny-remapper) | 0.8.2 | Apache-2.0 |
| `securejarhandler/cpw/mods/**` | [securejarhandler](https://github.com/NeoForged/SecureJarHandler) | 3.0.8 | LGPL-3.0 |

The tiny-remapper files sit in `net.fabricmc.loader.impl.lib.tinyremapper` because
that is where Fabric Loader shades them. `build.ps1` rewrites the package back to
`net.fabricmc.tinyremapper` when it patches the standalone 0.8.2 jar for legacy
Fabric targets.

The securejarhandler patch targets 3.0.8, which is what NeoForge `21.1.233`
resolves. Forge `1.20.1` resolves 2.1.10 and is deliberately left unpatched.

**The Fabric Loader release these files were copied from is not recorded.** The
versions above are the releases the patch is applied against, which is not
necessarily the same thing. Worth pinning properly the next time these files are
refreshed from upstream.

## Prebuilt binaries under `mesa-runtime/`

Prebuilt Mesa UWP DLLs. Not built from source in this repository and not modified
here. Mesa is distributed under MIT-style terms, with some components under other
permissive licenses. `docs/LEGAL.md` records that these keep their own upstream
terms.

## Components resolved at build or run time

Not vendored, not committed, and not redistributed from this repository. They are
downloaded on the build machine or on the device and are listed here so the full
dependency surface is written down in one place.

- Minecraft, Mojang assets, asset indexes and client jars. No redistribution
  rights are granted by this repository, and the launcher verifies ownership
  before downloading any of it.
- Fabric Loader, Forge, NeoForge and their libraries.
- LWJGL and its native jars.
- The Java runtime, packaged from a locally installed JDK.
