# Staráč — Beta 1.7.3 / Java 21 (Xbox UWP preparation)

This folder documents and builds the upstream Staráč mod for the Bandit Launcher
Beta 1.7.3 Babric target. The upstream project is maintained at
https://github.com/matthewperiut/starac and is licensed with MIT/LGPL-2.1
components; its own license files remain authoritative.

## Build on GitHub

Run **Actions → Build Starač for Beta 1.7.3 → Run workflow**. The workflow
checks out the upstream source, uses Temurin Java 21, runs the upstream Gradle
build, and publishes the resulting JAR files plus SHA-256 checksums as the
`starac-beta-1.7.3-java21` artifact.

The workflow intentionally builds the upstream project rather than copying or
forking its implementation into this repository.

## Important Xbox/UWP integration boundary

A successful upstream Gradle build is **not** a successful Xbox port.

Staráč replaces the legacy LWJGL 2 API with an LWJGL 3.4.0/GLFW compatibility
layer. Its upstream build includes desktop native classifiers (Linux, Windows,
macOS and ARM variants). Those desktop native binaries are not interchangeable
with Xbox UWP binaries. The Bandit Launcher currently has a separate UWP GLFW
CoreWindow shim; Staráč's GLFW calls, native loading, OpenAL, OpenGL and window
lifecycle still need to be adapted and validated against that shim before its
JAR can be enabled in an Xbox package.

Do not copy the upstream desktop `natives-windows` DLLs into the APPX. They
are not a UWP build.

## Controller support

Staráč modernizes windowing and input APIs (Display, Mouse and Keyboard) using
GLFW. It is **not a dedicated Xbox gamepad/controller mod**. The gamepad mapping
and Xbox button handling must remain a separate controller-provider task.
This workflow does not claim to add native gamepad support.

## Current status

- Beta 1.7.3 catalog entry: Babric, Java 21, experimental.
- Upstream Staráč Java sources: built by GitHub Actions when the workflow is run.
- Xbox UWP native integration: pending.
- Xbox Series S|X launch and controller validation: pending.

Keep the upstream license notices with any redistributed build. Review the
upstream source and release changes before shipping a JAR in a public APPX.
