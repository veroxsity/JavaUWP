# Legal Notes

This repository contains source code and build scripts for the UWP host, GLFW shim, compatibility mod, Fabric Loader patches, and related project tooling.

It does not grant rights to redistribute Minecraft, Mojang assets, Fabric, LWJGL, Java, Xbox platform files, or any other external component.

The Mesa UWP runtime DLLs in `mesa-runtime/` remain under their own upstream license terms.

`docs/THIRD-PARTY.md` lists every third party component, its upstream, its version and its license.

`docs/PRIVACY.md` covers what the launcher does with account data, what opt-in telemetry sends and where it goes, and what Remote Files exposes on a local network.

## Repository license

Original project code is covered by the custom license in `LICENSE`.

In short:

- Private forks are allowed for personal, educational, research, or internal use.
- Public forks, public mirrors, public source archives, and public modified copies are not permitted without prior written permission.
- Videos, streams, screenshots, reviews, benchmarks, tutorials, and other creator content are allowed when they follow the attribution, redistribution, endorsement, and authentication rules in `LICENSE`.
- Public creator content based on the project must include credit and a visible link back to veroxsity / BanditVault.
- Redistribution requires prior written permission from veroxsity / BanditVault.
- Removing, bypassing, disabling, stubbing, faking, or making optional Microsoft/Xbox authentication or Minecraft entitlement checks is not permitted.
- Patches, instructions, builds, or configuration intended to bypass authentication or ownership verification are not permitted.
- External components keep their own licenses and terms.

## Pre Release And Nightly Packages

Generated APPX packages, including nightly and pre release packages, may not be redistributed, mirrored, re uploaded, or otherwise shared without prior written permission from veroxsity / BanditVault.

Nightly and pre release packages are testing builds. They are not full releases, and support is not provided for them.

Public videos, streams, screenshots, reviews, benchmarks, tutorials, install guides, and similar public walkthroughs are allowed when they follow the creator content rules in `LICENSE`.

Unofficial builds that remove or bypass authentication, ownership checks, or entitlement enforcement are not permitted.

## Local files

The build creates or uses local files that should stay out of git:

- Minecraft game files.
- Mojang asset indexes and asset objects.
- Downloaded libraries.
- Fabric installer JAR.
- Java runtime images.
- Native DLLs.
- Mesa runtime DLLs from local test folders outside `mesa-runtime/`.
- Signed `.appx` packages.
- Development signing certificates.
- Saves, logs, config files, and local debug output.

These files are ignored under `staging` or `output`.
