/*
 * Vendored from securejarhandler 3.0.8
 * (https://github.com/NeoForged/SecureJarHandler), the version NeoForge 21.1.233
 * resolves. Forge 1.20.1 pulls 2.1.10 and is deliberately not patched with this,
 * see forge.cpp PrepareClientArtifacts.
 * Licensed under the GNU Lesser General Public License, version 3.
 *
 * Modified for Xbox UWP: package scanning tolerates the sandbox filesystem and
 * falls back to the backing path when the union filesystem cannot walk a jar.
 */
package cpw.mods.jarhandling.impl;

import cpw.mods.jarhandling.JarContents;
import cpw.mods.jarhandling.SecureJar;
import cpw.mods.niofs.union.UnionFileSystem;
import cpw.mods.niofs.union.UnionFileSystemProvider;
import cpw.mods.niofs.union.UnionPathFilter;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.URI;
import java.nio.file.FileSystems;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.spi.FileSystemProvider;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Supplier;
import java.util.jar.JarFile;
import java.util.jar.JarInputStream;
import java.util.jar.Manifest;

public class JarContentsImpl implements JarContents {
    private static final UnionFileSystemProvider UFSP = (UnionFileSystemProvider) FileSystemProvider.installedProviders()
            .stream()
            .filter(fsp -> fsp.getScheme().equals("union"))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("Couldn't find UnionFileSystemProvider"));
    private static final Set<String> NAUGHTY_SERVICE_FILES = Set.of("org.codehaus.groovy.runtime.ExtensionModule");
    private static final Set<String> REPORTED_LAUNCHER_OVERRIDES = java.util.Collections.synchronizedSet(new HashSet<>());

    final UnionFileSystem filesystem;
    final JarSigningData signingData = new JarSigningData();
    private final Manifest manifest;
    private final Map<Path, Integer> nameOverrides;
    private final UnionPathFilter pathFilter;
    private final Map<Path, UnionFileSystem> backingFileSystems = new HashMap<>();

    private Set<String> packages;
    private List<SecureJar.Provider> providers;

    public JarContentsImpl(Path[] paths, Supplier<Manifest> defaultManifest, UnionPathFilter pathFilter) {
        var validPaths = Arrays.stream(paths).filter(Files::exists).toArray(Path[]::new);
        if (validPaths.length == 0) {
            throw new UncheckedIOException(new IOException("Invalid paths argument, contained no existing paths: " + Arrays.toString(paths)));
        }
        this.pathFilter = pathFilter;
        this.filesystem = UFSP.newFileSystem(pathFilter, validPaths);
        this.manifest = readManifestAndSigningData(defaultManifest, validPaths);
        this.nameOverrides = readMultiReleaseInfo();
    }

    private Manifest readManifestAndSigningData(Supplier<Manifest> defaultManifest, Path[] validPaths) {
        try {
            for (int x = validPaths.length - 1; x >= 0; x--) {
                var path = validPaths[x];
                if (Files.isDirectory(path)) {
                    var manfile = path.resolve(JarFile.MANIFEST_NAME);
                    if (Files.exists(manfile)) {
                        try (var is = Files.newInputStream(manfile)) {
                            return new Manifest(is);
                        }
                    }
                } else {
                    try (var jis = new JarInputStream(Files.newInputStream(path))) {
                        signingData.readJarSigningData(jis);

                        if (jis.getManifest() != null) {
                            return new Manifest(jis.getManifest());
                        }
                    }
                }
            }
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return defaultManifest.get();
    }

    private Map<Path, Integer> readMultiReleaseInfo() {
        boolean isMultiRelease = Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"));
        if (!isMultiRelease) {
            return Map.of();
        }

        var vers = filesystem.getRoot().resolve("META-INF/versions");
        if (!Files.isDirectory(vers)) {
            return Map.of();
        }

        try (var walk = Files.walk(vers)) {
            Map<Path, Integer> pathToJavaVersion = new HashMap<>();
            walk
                    .filter(p -> !Files.isDirectory(p))
                    .forEach(p -> {
                        int javaVersion = Integer.parseInt(p.getName(2).toString());
                        Path remainder = p.subpath(3, p.getNameCount());
                        if (javaVersion <= Runtime.version().feature()) {
                            pathToJavaVersion.merge(remainder, javaVersion, Integer::max);
                        }
                    });
            return pathToJavaVersion;
        } catch (IOException ioe) {
            throw new UncheckedIOException(ioe);
        }
    }

    @Override
    public Path getPrimaryPath() {
        return filesystem.getPrimaryPath();
    }

    @Override
    public Optional<URI> findFile(String name) {
        var launcherOverride = findLauncherOverrideFile(name);
        if (launcherOverride.isPresent()) {
            return launcherOverride;
        }

        var rel = filesystem.getPath(name);
        if (this.nameOverrides.containsKey(rel)) return findBackingFileUri(name);
        var unionPath = this.filesystem.getRoot().resolve(rel);
        if (Files.exists(unionPath)) {
            return Optional.of(unionPath.toUri());
        }

        return findBackingFileUri(name);
    }

    private Optional<URI> findLauncherOverrideFile(String name) {
        var overrideDir = System.getProperty("banditvault.launcherOverrideDir", "");
        if (overrideDir.isBlank() || name.isBlank() || name.contains("..") || name.startsWith("/") || name.startsWith("\\") || name.endsWith(".class")) {
            return Optional.empty();
        }

        try {
            var base = Path.of(overrideDir).toAbsolutePath().normalize();
            var candidate = base.resolve(name.replace('/', base.getFileSystem().getSeparator().charAt(0))).normalize();
            if (candidate.startsWith(base) && Files.isRegularFile(candidate)) {
                var reportKey = name + " -> " + candidate;
                if (Boolean.getBoolean("banditvault.securejarhandler.debug") && REPORTED_LAUNCHER_OVERRIDES.add(reportKey)) {
                    System.err.println("[banditvault] JarContentsImpl launcher override resource "
                            + name + " -> " + candidate);
                }
                return Optional.of(candidate.toUri());
            }
        } catch (RuntimeException ignored) {
        }
        return Optional.empty();
    }

    @Override
    public Manifest getManifest() {
        return manifest;
    }

    @Override
    public Set<String> getPackagesExcluding(String... excludedRootPackages) {
        Set<String> ignoredRootPackages = new HashSet<>(excludedRootPackages.length + 1);
        ignoredRootPackages.add("META-INF");
        ignoredRootPackages.addAll(List.of(excludedRootPackages));

        Set<String> packages = new HashSet<>();
        final Path root = this.filesystem.getRoot();
        IOException unionFailure = null;
        try {
            Files.walkFileTree(root, new SimpleFileVisitor<>() {
                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) {
                    if (file.getFileName().toString().endsWith(".class")) {
                        var pkg = packageNameFor(root, file);
                        if (!pkg.isEmpty()) {
                            packages.add(pkg);
                        }
                    }
                    return FileVisitResult.CONTINUE;
                }

                @Override
                public FileVisitResult preVisitDirectory(Path path, BasicFileAttributes attrs) {
                    if (path.getNameCount() > 0 && ignoredRootPackages.contains(path.getName(0).toString())) {
                        return FileVisitResult.SKIP_SUBTREE;
                    }
                    return FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException e) {
            unionFailure = e;
            System.err.println("[banditvault] Union package scan failed " + e.getMessage());
        }
        boolean recovered = addPackagesFromBackingJars(packages, ignoredRootPackages);
        if (unionFailure != null && !recovered) throw new UncheckedIOException(unionFailure);
        logPackageScan(root, packages);
        return Set.copyOf(packages);
    }

    private void logPackageScan(Path root, Set<String> packages) {
        if (!Boolean.getBoolean("banditvault.securejarhandler.debug")) {
            return;
        }

        System.err.println("[banditvault] JarContentsImpl UWP package scan primary=" + getPrimaryPath()
                + " root=" + root
                + " packages=" + packages.size()
                + " net.minecraft.core=" + packages.contains("net.minecraft.core")
                + " net.minecraft.core.registries=" + packages.contains("net.minecraft.core.registries"));
        for (Path path : getBackingPaths()) {
            System.err.println("[banditvault] JarContentsImpl backing path=" + path);
        }
    }

    private static String packageNameFor(Path root, Path file) {
        Path parent = file.getParent();
        if (parent == null) {
            return "";
        }

        String pathText;
        try {
            pathText = root.relativize(parent).toString();
        } catch (IllegalArgumentException ex) {
            pathText = parent.toString();
        }

        pathText = pathText.replace('\\', '/');
        while (pathText.startsWith("/")) {
            pathText = pathText.substring(1);
        }
        if (pathText.isEmpty() || ".".equals(pathText)) {
            return "";
        }
        return pathText.replace('/', '.');
    }

    private boolean addPackagesFromBackingJars(Set<String> packages, Set<String> ignoredRootPackages) {
        boolean recovered = true;
        for (Path basePath : getBackingPaths()) {
            boolean read = Files.isDirectory(basePath)
                    ? addPackagesFromDirectory(packages, ignoredRootPackages, basePath)
                    : addPackagesFromJar(packages, ignoredRootPackages, basePath);
            recovered &= read;
        }
        return recovered;
    }

    @SuppressWarnings("unchecked")
    public List<Path> getBackingPaths() {
        try {
            var method = UnionFileSystem.class.getDeclaredMethod("getBasePaths");
            method.setAccessible(true);
            return (List<Path>) method.invoke(this.filesystem);
        } catch (ReflectiveOperationException | RuntimeException ex) {
            return List.of(this.filesystem.getPrimaryPath());
        }
    }

    private boolean allowed(String name, Path base) {
        return pathFilter == null || pathFilter.test(name, base);
    }

    public byte[] readBackingFileBytes(String name) {
        for (Path basePath : getBackingPaths()) {
            if (!allowed(name, basePath)) continue;
            try {
                if (Files.isDirectory(basePath)) {
                    Path file = findDirectoryFile(basePath, name);
                    if (Files.exists(file)) {
                        return Files.readAllBytes(file);
                    }
                } else if (isOnDefaultFileSystem(basePath)) {
                    try (var jar = new JarFile(basePath.toFile(), true, JarFile.OPEN_READ, JarFile.baseVersion())) {
                        var entry = findJarEntry(jar, name, basePath);
                        if (entry != null && !entry.isDirectory() && allowed(entry.getName(), basePath)) {
                            try (var in = jar.getInputStream(entry)) {
                                return in.readAllBytes();
                            }
                        }
                    }
                } else {
                    var bytes = readEntryByStreaming(basePath, name);
                    if (bytes != null) {
                        return bytes;
                    }
                }
            } catch (IOException | RuntimeException ignored) {
            }
        }
        return new byte[0];
    }

    private byte[] readEntryByStreaming(Path jarPath, String name) throws IOException {
        byte[] selected = null;
        int selectedVersion = -1;
        try (var in = Files.newInputStream(jarPath); var jar = new JarInputStream(in)) {
            boolean multiRelease = Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"));
            for (var entry = jar.getNextJarEntry(); entry != null; entry = jar.getNextJarEntry()) {
                String logical = logicalEntryName(entry.getName(), multiRelease);
                if (!entry.isDirectory() && name.equals(logical) && allowed(logical, jarPath) && allowed(entry.getName(), jarPath)) {
                    int version = entry.getName().equals(name) ? 0 : Integer.parseInt(entry.getName().split("/", 4)[2]);
                    if (version > selectedVersion) {
                        selected = jar.readAllBytes();
                        selectedVersion = version;
                    }
                }
            }
        }
        return selected;
    }

    private Path findDirectoryFile(Path base, String name) {
        if (Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"))) {
            for (int version = Runtime.version().feature(); version >= 9; --version) {
                String versioned = "META-INF/versions/" + version + "/" + name;
                Path file = base.resolve(versioned);
                if (Files.isRegularFile(file) && allowed(versioned, base)) return file;
            }
        }
        return base.resolve(name.replace('/', base.getFileSystem().getSeparator().charAt(0)));
    }

    private java.util.jar.JarEntry findJarEntry(JarFile jar, String name, Path base) throws IOException {
        if (Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"))) {
            for (int version = Runtime.version().feature(); version >= 9; --version) {
                String versioned = "META-INF/versions/" + version + "/" + name;
                var entry = jar.getJarEntry(versioned);
                if (entry != null && allowed(versioned, base)) return entry;
            }
        }
        return jar.getJarEntry(name);
    }

    private static String logicalEntryName(String name, boolean multiRelease) {
        if (!name.startsWith("META-INF/versions/")) return name;
        if (!multiRelease) return null;
        String[] parts = name.split("/", 4);
        if (parts.length != 4) return null;
        try {
            int version = Integer.parseInt(parts[2]);
            return version >= 9 && version <= Runtime.version().feature() ? parts[3] : null;
        } catch (NumberFormatException ex) {
            return null;
        }
    }

    private Optional<URI> findBackingFileUri(String name) {
        for (Path basePath : getBackingPaths()) {
            if (!allowed(name, basePath)) continue;
            try {
                if (Files.isDirectory(basePath)) {
                    Path file = findDirectoryFile(basePath, name);
                    if (Files.exists(file)) {
                        return Optional.of(file.toUri());
                    }
                } else if (!isOnDefaultFileSystem(basePath)) {
                    String selected = null;
                    int selectedVersion = -1;
                    try (var in = Files.newInputStream(basePath); var jar = new JarInputStream(in)) {
                        boolean multiRelease = Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"));
                        for (var entry = jar.getNextJarEntry(); entry != null; entry = jar.getNextJarEntry()) {
                            if (!entry.isDirectory() && name.equals(logicalEntryName(entry.getName(), multiRelease)) && allowed(entry.getName(), basePath)) {
                                int version = entry.getName().equals(name) ? 0 : Integer.parseInt(entry.getName().split("/", 4)[2]);
                                if (version > selectedVersion) { selected = entry.getName(); selectedVersion = version; }
                            }
                        }
                    }
                    if (selected != null) {
                        return Optional.of(backingFileUri(basePath, selected));
                    }
                } else {
                    try (var jar = new JarFile(basePath.toFile(), true, JarFile.OPEN_READ, JarFile.baseVersion())) {
                        var entry = findJarEntry(jar, name, basePath);
                        if (entry != null && !entry.isDirectory() && allowed(entry.getName(), basePath)) {
                            if (Boolean.getBoolean("banditvault.securejarhandler.debug")
                                    && (name.endsWith("mods.toml") || name.startsWith("META-INF/services/"))) {
                                System.err.println("[banditvault] JarContentsImpl UWP backing resource "
                                        + name + " -> " + basePath);
                            }
                            return Optional.of(backingFileUri(basePath, entry.getName()));
                        }
                    }
                }
            } catch (IOException | RuntimeException ignored) {
            }
        }
        return Optional.empty();
    }

    private static boolean isOnDefaultFileSystem(Path path) {
        return path.getFileSystem() == FileSystems.getDefault();
    }

    private synchronized URI backingFileUri(Path basePath, String entry) {
        var backing = backingFileSystems.computeIfAbsent(basePath, path -> UFSP.newFileSystem(pathFilter, path));
        return backing.getRoot().resolve(entry).toUri();
    }

    private boolean addPackagesFromJar(Set<String> packages, Set<String> ignoredRootPackages, Path jarPath) {
        // a jar-in-jar base path lives in the parent's zipfs, so toFile throws UnsupportedOperationException
        if (!isOnDefaultFileSystem(jarPath)) {
            return addPackagesByStreaming(packages, ignoredRootPackages, jarPath);
        }

        try (var jar = new JarFile(jarPath.toFile())) {
            boolean multiRelease = Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"));
            var entries = jar.entries();
            while (entries.hasMoreElements()) {
                var entry = entries.nextElement();
                if (entry.isDirectory()) {
                    continue;
                }
                String logical = logicalEntryName(entry.getName(), multiRelease);
                if (logical != null && allowed(logical, jarPath) && allowed(entry.getName(), jarPath)) addPackageFromEntryName(packages, ignoredRootPackages, logical);
            }
            return true;
        } catch (IOException | RuntimeException ex) {
            System.err.println("[banditvault] Backing package scan failed " + jarPath + " " + ex);
            return false;
        }
    }

    private boolean addPackagesByStreaming(Set<String> packages, Set<String> ignoredRootPackages, Path jarPath) {
        try (var in = Files.newInputStream(jarPath); var jar = new JarInputStream(in)) {
            boolean multiRelease = Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"));
            for (var entry = jar.getNextJarEntry(); entry != null; entry = jar.getNextJarEntry()) {
                if (entry.isDirectory()) {
                    continue;
                }
                String logical = logicalEntryName(entry.getName(), multiRelease);
                if (logical != null && allowed(logical, jarPath) && allowed(entry.getName(), jarPath)) addPackageFromEntryName(packages, ignoredRootPackages, logical);
            }
            return true;
        } catch (IOException | RuntimeException ex) {
            System.err.println("[banditvault] Backing package scan failed " + jarPath + " " + ex);
            return false;
        }
    }

    private boolean addPackagesFromDirectory(Set<String> packages, Set<String> ignoredRootPackages, Path directory) {
        try (var walk = Files.walk(directory)) {
            boolean multiRelease = Boolean.parseBoolean(getManifest().getMainAttributes().getValue("Multi-Release"));
            walk
                    .filter(path -> !Files.isDirectory(path))
                    .forEach(path -> {
                        String name = directory.relativize(path).toString().replace('\\', '/');
                        String logical = logicalEntryName(name, multiRelease);
                        if (logical != null && allowed(logical, directory) && allowed(name, directory)) {
                            addPackageFromEntryName(packages, ignoredRootPackages, logical);
                        }
                    });
            return true;
        } catch (IOException | UncheckedIOException ex) {
            System.err.println("[banditvault] Backing package scan failed " + directory + " " + ex);
            return false;
        }
    }

    private static void addPackageFromEntryName(Set<String> packages, Set<String> ignoredRootPackages, String name) {
        if (!name.endsWith(".class")) {
            return;
        }

        while (name.startsWith("/")) {
            name = name.substring(1);
        }

        int slash = name.indexOf('/');
        if (slash < 0) {
            return;
        }
        if (ignoredRootPackages.contains(name.substring(0, slash))) {
            return;
        }

        int lastSlash = name.lastIndexOf('/');
        if (lastSlash <= 0) {
            return;
        }
        packages.add(name.substring(0, lastSlash).replace('/', '.'));
    }

    @Override
    public Set<String> getPackages() {
        if (this.packages == null) {
            this.packages = getPackagesExcluding();
        }
        return this.packages;
    }

    @Override
    public List<SecureJar.Provider> getMetaInfServices() {
        if (this.providers == null) {
            final var services = this.filesystem.getRoot().resolve("META-INF/services/");
            if (Files.exists(services)) {
                try (var walk = Files.walk(services, 1)) {
                    this.providers = walk.filter(path -> !Files.isDirectory(path))
                            .filter(path -> !NAUGHTY_SERVICE_FILES.contains(path.getFileName().toString()))
                            .map((Path path1) -> SecureJar.Provider.fromPath(path1, filesystem.getFilesystemFilter()))
                            .toList();
                } catch (IOException e) {
                    throw new UncheckedIOException(e);
                }
            } else {
                this.providers = List.of();
            }
        }
        return this.providers;
    }

    @Override
    public synchronized void close() throws IOException {
        IOException failure = null;
        var filesystems = new java.util.ArrayList<>(backingFileSystems.values());
        filesystems.add(filesystem);
        for (var current : filesystems) {
            try { current.close(); }
            catch (IOException error) {
                if (failure == null) failure = error;
                else failure.addSuppressed(error);
            }
        }
        backingFileSystems.clear();
        if (failure != null) throw failure;
    }
}
