package banditvault.xboxcompat.mixin;

import java.io.IOException;
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
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.Unique;

@Mixin(net.minecraft.class_10619.class)
public abstract class ZipFsBypassMixin {
    @Unique
    private static final ConcurrentMap<Path, FileSystem> banditvault$zipFileSystems = new ConcurrentHashMap<>();

    /**
     * @author Codex
     * @reason ZipFileSystemProvider's URI path calls toRealPath(), which fails in Xbox Dev Mode.
     */
    @Overwrite
    public static Path method_66590(URI uri) throws IOException {
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
        FileSystem fileSystem = banditvault$zipFileSystems.get(jarPath);

        if (fileSystem == null || !fileSystem.isOpen()) {
            try {
                fileSystem = FileSystems.newFileSystem(jarPath, Collections.emptyMap());
            } catch (FileSystemAlreadyExistsException ignored) {
                fileSystem = banditvault$zipFileSystems.get(jarPath);
            }

            if (fileSystem == null || !fileSystem.isOpen()) {
                fileSystem = FileSystems.newFileSystem(jarPath, Collections.emptyMap());
            }

            banditvault$zipFileSystems.put(jarPath, fileSystem);
        }

        return fileSystem.getPath(spec.substring(sep + 1));
    }
}
