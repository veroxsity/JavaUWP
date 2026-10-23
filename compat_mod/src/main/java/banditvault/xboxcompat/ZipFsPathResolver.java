package banditvault.xboxcompat;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URI;
import java.nio.file.FileSystem;
import java.nio.file.FileSystemAlreadyExistsException;
import java.nio.file.FileSystemNotFoundException;
import java.nio.file.FileSystems;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

public final class ZipFsPathResolver {
    private static final ConcurrentMap<Path, FileSystem> ZIP_FILE_SYSTEMS = new ConcurrentHashMap<Path, FileSystem>();
    private static final Method NEW_FILE_SYSTEM_PATH_METHOD = findPathNewFileSystemMethod();

    private ZipFsPathResolver() {
    }

    public static Path resolve(URI uri) throws IOException {
        try {
            return Paths.get(uri);
        } catch (FileSystemNotFoundException ignored) {
            // Fall through to the path-based zipfs path below.
        } catch (Throwable ignored) {
            // Preserve vanilla behavior of tolerating odd URI providers and retrying.
        }

        String spec = uri.getRawSchemeSpecificPart();
        int sep = spec.indexOf("!/");
        if (sep == -1) {
            return Paths.get(uri);
        }

        Path jarPath = Paths.get(URI.create(spec.substring(0, sep))).toAbsolutePath().normalize();
        FileSystem fileSystem = ZIP_FILE_SYSTEMS.get(jarPath);

        if (fileSystem == null || !fileSystem.isOpen()) {
            try {
                fileSystem = newFileSystem(jarPath);
            } catch (FileSystemAlreadyExistsException ignored) {
                fileSystem = ZIP_FILE_SYSTEMS.get(jarPath);
            }

            if (fileSystem == null || !fileSystem.isOpen()) {
                fileSystem = newFileSystem(jarPath);
            }

            ZIP_FILE_SYSTEMS.put(jarPath, fileSystem);
        }

        return fileSystem.getPath(URI.create("file:" + spec.substring(sep + 1)).getPath());
    }

    private static Method findPathNewFileSystemMethod() {
        try {
            return FileSystems.class.getMethod("newFileSystem", Path.class, java.util.Map.class);
        } catch (NoSuchMethodException ignored) {
            return null;
        }
    }

    private static FileSystem newFileSystem(Path jarPath) throws IOException {
        if (NEW_FILE_SYSTEM_PATH_METHOD != null) {
            try {
                return (FileSystem) NEW_FILE_SYSTEM_PATH_METHOD.invoke(null, jarPath, Collections.emptyMap());
            } catch (IllegalAccessException e) {
                throw new IOException("Could not access path-based zipfs constructor", e);
            } catch (InvocationTargetException e) {
                Throwable cause = e.getCause();
                if (cause instanceof FileSystemAlreadyExistsException) {
                    throw (FileSystemAlreadyExistsException) cause;
                }
                if (cause instanceof IOException) {
                    throw (IOException) cause;
                }
                if (cause instanceof RuntimeException) {
                    throw (RuntimeException) cause;
                }
                throw new IOException("Could not open zipfs", cause);
            }
        }

        return FileSystems.newFileSystem(URI.create("jar:" + jarPath.toUri().toString()), Collections.emptyMap());
    }
}
