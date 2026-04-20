package net.fabricmc.loader.impl.util;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.stream.Collectors;

/**
 * Patched LoaderUtil for Xbox UWP compatibility.
 * normalizeExistingPath0 patched to use toAbsolutePath() instead of toRealPath()
 * because GetFinalPathNameByHandle is blocked in the Xbox Dev Mode sandbox.
 */
public final class LoaderUtil {
    private static final ConcurrentMap<Path, Path> pathNormalizationCache = new ConcurrentHashMap<>();
    private static final String FABRIC_LOADER_CLASS = "net/fabricmc/loader/api/FabricLoader.class";
    private static final String ASM_CLASS = "org/objectweb/asm/ClassReader.class";

    public static String getClassFileName(String className) {
        return className.replace('.', '/').concat(".class");
    }

    public static Path normalizePath(Path path) {
        if (Files.exists(path)) {
            return normalizeExistingPath(path);
        } else {
            return path.toAbsolutePath().normalize();
        }
    }

    public static Path normalizeExistingPath(Path path) {
        return pathNormalizationCache.computeIfAbsent(path, LoaderUtil::normalizeExistingPath0);
    }

    private static Path normalizeExistingPath0(Path path) {
        // Xbox UWP patch: toRealPath() calls GetFinalPathNameByHandle which is
        // blocked in the Xbox Dev Mode sandbox. Use toAbsolutePath().normalize()
        // instead — equivalent for our purposes since there are no symlinks.
        try {
            return path.toAbsolutePath().normalize();
        } catch (Exception e) {
            throw new UncheckedIOException(new IOException(e.getMessage(), e));
        }
    }

    public static void verifyNotInTargetCl(Class<?> clazz) {
        if (clazz.getClassLoader().getClass().getName()
                .equals("net.fabricmc.loader.impl.launch.knot.KnotClassLoader")) {
            throw new IllegalStateException("trying to load " + clazz.getName() + " from target class loader");
        }
    }

    public static void verifyClasspath() {
        try {
            List<URL> loaderUrls = Collections.list(
                LoaderUtil.class.getClassLoader().getResources(FABRIC_LOADER_CLASS));
            if (loaderUrls.size() > 1) {
                throw new IllegalStateException("duplicate fabric loader classes found on classpath:" +
                    loaderUrls.stream().map(URL::toString).collect(Collectors.joining(",")));
            } else if (loaderUrls.size() < 1) {
                throw new AssertionError("net/fabricmc/loader/api/FabricLoader.class not detected on the classpath?! (perhaps it was renamed?)");
            }

            List<URL> asmUrls = Collections.list(
                LoaderUtil.class.getClassLoader().getResources(ASM_CLASS));
            if (asmUrls.size() > 1) {
                throw new IllegalStateException("duplicate ASM classes found on classpath:" +
                    asmUrls.stream().map(URL::toString).collect(Collectors.joining(",")));
            } else if (asmUrls.size() < 1) {
                throw new IllegalStateException("ASM not detected on the classpath (or perhaps org/objectweb/asm/ClassReader.class was renamed?)");
            }
        } catch (IOException e) {
            throw new UncheckedIOException("Failed to get resources", e);
        }
    }

    public static boolean hasMacOs() {
        return System.getProperty("os.name").toLowerCase(Locale.ENGLISH).contains("mac");
    }

    public static boolean hasAwtSupport() {
        if (hasMacOs()) {
            for (String key : System.getenv().keySet()) {
                if (key.startsWith("JAVA_STARTED_ON_FIRST_THREAD_")) return false;
            }
        }
        return true;
    }
}
