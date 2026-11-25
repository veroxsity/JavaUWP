# Java 17 UWP runtime adapter

This adapter describes the Java runtime expected by the Beta 1.7.3 profile.

## Contract

- Profile ID: `b1.7.3`
- Java major: `17` (exact match; do not silently fall back to Java 21 or 25)
- Runtime folder: `legacy-lwjgl2/runtime/jre17`
- Runtime type: JVM built for the app's Xbox UWP architecture
- JVM entry point: load the packaged JVM and resolve `JNI_CreateJavaVM`

## Implementation checklist

- [ ] Add exact Java 17 UWP runtime discovery to the native launcher.
- [ ] Validate the JVM binary and architecture before loading.
- [ ] Validate all dependent DLLs are packaged and loadable under UWP.
- [ ] Start the JVM with the Beta 1.7.3 classpath and isolated game directory.
- [ ] Log a clear error when Java 17 UWP is missing; do not silently choose another Java major.
- [ ] Verify JVM startup and shutdown on Xbox Series S/X.

This is a design note, not an implemented runtime loader. No JVM binaries are included.
