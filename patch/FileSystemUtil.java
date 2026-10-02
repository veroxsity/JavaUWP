/*
 * Vendored from Fabric Loader (https://github.com/FabricMC/fabric-loader).
 * Applied against Fabric Loader 0.19.3 and 0.14.25; the source release it was
 * copied from is not recorded.
 * Licensed under the Apache License, Version 2.0.
 *
 * Modified for Xbox UWP: avoids the ZipFS open route that calls toRealPath(),
 * which fails inside the Dev Mode sandbox.
 */
package net.fabricmc.loader.impl.util;

import java.io.IOException;
import java.net.URI;
import java.nio.file.FileSystem;
import java.nio.file.FileSystemAlreadyExistsException;
import java.nio.file.FileSystems;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.Map;

/**
 * Patched FileSystemUtil for Xbox UWP compatibility.
 *
 * The stock URI-based jar filesystem path calls ZipFileSystemProvider.newFileSystem(URI),
 * which calls Path.toRealPath() and fails inside the Xbox Dev Mode sandbox.
 */
public final class FileSystemUtil {
    private static final Map<String, String> jfsArgsCreate = Collections.singletonMap("create", "true");
    private static final Map<String, String> jfsArgsEmpty = Collections.emptyMap();

    private FileSystemUtil() {
    }

    public static FileSystemDelegate getJarFileSystem(Path path, boolean create) throws IOException {
        try {
            FileSystem fs = FileSystems.newFileSystem(path, create ? jfsArgsCreate : jfsArgsEmpty);
            return new FileSystemDelegate(fs, true);
        } catch (FileSystemAlreadyExistsException ignored) {
            URI uri = URI.create("jar:" + path.toUri());
            return new FileSystemDelegate(FileSystems.getFileSystem(uri), false);
        } catch (IOException | RuntimeException ex) {
            throw new IOException("Error accessing " + path + ": " + ex, ex);
        }
    }

    public static FileSystemDelegate getJarFileSystem(URI uri, boolean create) throws IOException {
        try {
            String raw = uri.getRawSchemeSpecificPart();
            if ("jar".equalsIgnoreCase(uri.getScheme()) && raw != null) {
                int sep = raw.indexOf("!/");
                if (sep >= 0) {
                    raw = raw.substring(0, sep);
                }
                return getJarFileSystem(Paths.get(URI.create(raw)), create);
            }

            return getJarFileSystem(Paths.get(uri), create);
        } catch (IllegalArgumentException ex) {
            throw new IOException("Error accessing " + uri + ": " + ex, ex);
        }
    }

    public static class FileSystemDelegate implements AutoCloseable {
        private final FileSystem fileSystem;
        private final boolean owner;

        public FileSystemDelegate(FileSystem fileSystem, boolean owner) {
            this.fileSystem = fileSystem;
            this.owner = owner;
        }

        public FileSystem get() {
            return fileSystem;
        }

        @Override
        public void close() throws IOException {
            if (owner) {
                fileSystem.close();
            }
        }
    }
}
