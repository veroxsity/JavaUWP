/*
 * Vendored from securejarhandler 3.0.8
 * (https://github.com/NeoForged/SecureJarHandler), the version NeoForge 21.1.233
 * resolves. Forge 1.20.1 pulls 2.1.10 and is deliberately not patched with this,
 * see forge.cpp PrepareClientArtifacts.
 * Licensed under the GNU Lesser General Public License, version 3.
 *
 * Modified for Xbox UWP: adds module read edges the sandbox layer misses, plus a
 * resource and class lookup fallback for Minecraft classes the secure jar module
 * loader resolves differently under the embedded JVM.
 */
package cpw.mods.cl;

import cpw.mods.jarhandling.impl.JarContentsImpl;
import cpw.mods.util.LambdaExceptionUtils;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.module.*;
import java.net.MalformedURLException;
import java.net.URI;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.*;
import java.util.function.BiFunction;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.jar.JarFile;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public class ModuleClassLoader extends ClassLoader {
    static {
        ClassLoader.registerAsParallelCapable();
        URL.setURLStreamHandlerFactory(ModularURLHandler.INSTANCE);
        ModularURLHandler.initFrom(ModuleClassLoader.class.getModule().getLayer());
    }

    // Reflect into JVM internals to associate each ModuleClassLoader with all of its parent layers.
    // This is necessary to let ServiceProvider find service implementations in parent module layers.
    // At the moment, this does not work for providers in the bootstrap or platform class loaders,
    // but any other provider (defined by the application class loader or child layers) should work.
    //
    // The only mechanism the JVM has for this is to also look for layers defined by the parent class loader.
    // We don't want to set a parent because we explicitly do not want to delegate to a parent class loader,
    // and that wouldn't even handle the case of multiple parent layers anyway.
    private static final MethodHandle LAYER_BIND_TO_LOADER;
    private static final MethodHandle MODULES_ADD_READS;
    private static final MethodHandle MODULES_FIND_LOADED_MODULE;
    private static final Set<String> UWP_ADDED_READ_EDGES = Collections.synchronizedSet(new HashSet<>());
    private static final Set<String> UWP_REPORTED_READ_EDGE_MISSES = Collections.synchronizedSet(new HashSet<>());
    private final Set<String> UWP_RESOURCE_FALLBACK_MISSES = Collections.synchronizedSet(new HashSet<>());
    private static final ThreadLocal<Set<String>> UWP_RESOURCE_FALLBACK_IN_PROGRESS = ThreadLocal.withInitial(HashSet::new);
    private static final ThreadLocal<Set<String>> UWP_LOAD_CLASS_IN_PROGRESS = ThreadLocal.withInitial(HashSet::new);
    private static final Map<String, Module> UWP_SEEN_MODULES = Collections.synchronizedMap(new HashMap<>());

    static {
        try {
            var hackfield = MethodHandles.Lookup.class.getDeclaredField("IMPL_LOOKUP");
            hackfield.setAccessible(true);
            MethodHandles.Lookup hack = (MethodHandles.Lookup) hackfield.get(null);

            LAYER_BIND_TO_LOADER = hack.findSpecial(ModuleLayer.class, "bindToLoader", MethodType.methodType(void.class, ClassLoader.class), ModuleLayer.class);
            var modulesClass = Class.forName("jdk.internal.module.Modules");
            MODULES_ADD_READS = hack.findStatic(modulesClass, "addReads", MethodType.methodType(void.class, Module.class, Module.class));
            MODULES_FIND_LOADED_MODULE = hack.findStatic(modulesClass, "findLoadedModule", MethodType.methodType(Optional.class, String.class));
        } catch (NoSuchFieldException | IllegalAccessException | NoSuchMethodException | ClassNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * Invokes {@code ModuleLayer.bindToLoader(ClassLoader)}.
     */
    private static void bindToLayer(ModuleClassLoader classLoader, ModuleLayer layer) {
        try {
            LAYER_BIND_TO_LOADER.invokeExact(layer, (ClassLoader) classLoader);
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    private final Configuration configuration;
    private final Map<String, JarModuleFinder.JarModuleReference> resolvedRoots;
    private final Map<String, ResolvedModule> packageLookup;
    private final Map<String, ClassLoader> parentLoaders;
    private ClassLoader fallbackClassLoader;
    private static volatile Path minecraftSrgJarPath;

    public ModuleClassLoader(final String name, final Configuration configuration, final List<ModuleLayer> parentLayers) {
        this(name, configuration, parentLayers, null);
    }

    /**
     * This constructor allows setting the parent {@linkplain ClassLoader classloader}. Use this with caution since
     * it will allow loading of classes from the classpath directly if the {@linkplain ClassLoader#getSystemClassLoader() system classloader}
     * is reachable from the given parent classloader.
     * <p>
     * Generally classes that are in packages covered by reachable modules are preferably loaded from these modules.
     * If a class-path entry is not shadowed by a module, specifying a parent class-loader may lead to those
     * classes now being loadable instead of throwing a {@link ClassNotFoundException}.
     * <p>
     * This relaxed classloader isolation is used in unit-testing, where testing libraries are loaded on the
     * system class-loader outside our control (by the Gradle test runner). We must not reload these classes
     * inside the module layers again, otherwise tests throw incompatible exceptions or may not be found at all.
     */
    public ModuleClassLoader(final String name, final Configuration configuration, final List<ModuleLayer> parentLayers, ClassLoader parentLoader) {
        super(name, parentLoader);
        this.fallbackClassLoader = Objects.requireNonNullElse(parentLoader, ClassLoader.getPlatformClassLoader());
        this.configuration = configuration;
        this.packageLookup = new HashMap<>();
        this.resolvedRoots = configuration.modules().stream()
                .filter(m -> m.reference() instanceof JarModuleFinder.JarModuleReference)
                .peek(mod -> {
                    // Populate packageLookup at the same time, for speed
                    mod.reference().descriptor().packages().forEach(pk->this.packageLookup.put(pk, mod));
                })
                .collect(Collectors.toMap(mod -> mod.reference().descriptor().name(), mod -> (JarModuleFinder.JarModuleReference)mod.reference()));

        this.parentLoaders = new HashMap<>();
        Set<ModuleDescriptor> processedAutomaticDescriptors = new HashSet<>();
        Map<ResolvedModule, ClassLoader> classLoaderMap = new HashMap<>();
        Function<ResolvedModule, ClassLoader> findClassLoader = k -> {
            // Loading a class requires its module to be part of resolvedRoots
            // If it's not, we delegate loading to its module's classloader
            if (!this.resolvedRoots.containsKey(k.name())) {
                return parentLayers.stream()
                        .filter(l -> l.configuration() == k.configuration())
                        .flatMap(layer->Optional.ofNullable(layer.findLoader(k.name())).stream())
                        .findFirst()
                        .orElse(ClassLoader.getPlatformClassLoader());
            } else {
                return ModuleClassLoader.this;
            }
        };
        // This loop will be O(n^2) for the average set of mods, since they all read one another.
        // However, we amortize some of the cost by optimizing the common automatic module path.
        for (var rm : configuration.modules()) {
            for (var other : rm.reads()) {
                ClassLoader cl = classLoaderMap.computeIfAbsent(other, findClassLoader);
                final var descriptor = other.reference().descriptor();
                if (descriptor.isAutomatic()) {
                    // No need to run this logic more than once per automatic module
                    if (processedAutomaticDescriptors.add(descriptor)) {
                        descriptor.packages().forEach(pn->this.parentLoaders.put(pn, cl));
                    }
                } else {
                    // We actually use "rm" for this path, so we have to run it each time
                    descriptor.exports().stream()
                            .filter(e -> !e.isQualified() || (e.isQualified() && other.configuration() == configuration && e.targets().contains(rm.name())))
                            .map(ModuleDescriptor.Exports::source)
                            .forEach(pn->this.parentLoaders.put(pn, cl));
                }
            }
        }
        // Bind this classloader to all parent layers recursively,
        // to make sure ServiceLoader can find providers defined in parent layers
        Set<ModuleLayer> visitedLayers = new HashSet<>();
        parentLayers.forEach(p -> forLayerAndParents(p, visitedLayers, l -> bindToLayer(this, l)));
    }

    private static void forLayerAndParents(ModuleLayer layer, Set<ModuleLayer> visited, Consumer<ModuleLayer> operation) {
        if (visited.contains(layer)) return;
        visited.add(layer);
        operation.accept(layer);

        if (layer != ModuleLayer.boot()) {
            layer.parents().forEach(l -> forLayerAndParents(l, visited, operation));
        }
    }

    private URL readerToURL(final ModuleReader reader, final ModuleReference ref, final String name) {
        try {
            return ModuleClassLoader.toURL(reader.find(name));
        } catch (IOException e) {
            return null;
        }
    }

    @SuppressWarnings("OptionalUsedAsFieldOrParameterType")
    private static URL toURL(final Optional<URI> uri) {
        if (uri.isPresent()) {
            try {
                return uri.get().toURL();
            } catch (MalformedURLException e) {
                throw new IllegalArgumentException(e);
            }
        }
        return null;
    }

    @SuppressWarnings("OptionalUsedAsFieldOrParameterType")
    private static Stream<InputStream> closeHandler(Optional<InputStream> supplier) {
        final var is = supplier.orElse(null);
        return Optional.ofNullable(is).stream().onClose(() -> Optional.ofNullable(is).ifPresent(LambdaExceptionUtils.rethrowConsumer(InputStream::close)));
    }
    protected byte[] getClassBytes(final ModuleReader reader, final ModuleReference ref, final String name) {
        var cname = name.replace('.','/')+".class";

        try (var istream = closeHandler(Optional.of(reader).flatMap(LambdaExceptionUtils.rethrowFunction(r->r.open(cname))))) {
            var bytes = istream.map(LambdaExceptionUtils.rethrowFunction(InputStream::readAllBytes))
                    .findFirst()
                    .orElseGet(()->new byte[0]);
            if (bytes.length != 0) {
                return bytes;
            }
        }
        return getBackingClassBytes(ref, cname, name);
    }

    private byte[] getBackingClassBytes(final ModuleReference ref, final String classResourceName, final String className) {
        var modroot = this.resolvedRoots.get(ref.descriptor().name());

        var primaryPath = pathFromLocation(ref.location());
        Object jar = modroot != null ? modroot.jar() : null;
        if (jar == null && ref instanceof JarModuleFinder.JarModuleReference jarRef) {
            jar = jarRef.jar();
        }
        if (jar == null) {
            logBackingClassMiss(className, ref.descriptor().name(), "no backing jar object");
            return new byte[0];
        }

        try {
            var contents = getJarContents(jar);
            if (contents instanceof JarContentsImpl jarContents) {
                var bytes = jarContents.readBackingFileBytes(classResourceName);
                if (bytes.length != 0) {
                    logBackingClassRead("union", className, ref.descriptor().name(), primaryPath);
                } else {
                    logBackingClassMiss(className, ref.descriptor().name(), "not found in backing contents");
                }
                return bytes;
            }
            logBackingClassMiss(className, ref.descriptor().name(), "contents unavailable from " + jar.getClass().getName()
                    + " location=" + ref.location().map(Object::toString).orElse("<none>"));
        } catch (Throwable t) {
            logBackingClassMiss(className, ref.descriptor().name(), t.getClass().getName() + ": " + t.getMessage());
        }

        return new byte[0];
    }

    private static Object getJarContents(final Object moduleDataProvider) {
        var direct = getFieldValue(moduleDataProvider, "contents");
        if (direct != null) {
            return direct;
        }

        // SecureJar's JarModuleReference exposes a ModuleDataProvider. In 3.0.8 that is
        // Jar$JarModuleDataProvider, a record wrapper whose jar() accessor points back
        // to the outer Jar object that owns the real JarContentsImpl.
        var owner = invokeNoArg(moduleDataProvider, "jar");
        if (owner != null) {
            var contents = getFieldValue(owner, "contents");
            if (contents != null) {
                return contents;
            }
        }

        owner = getFieldValue(moduleDataProvider, "jar");
        if (owner != null) {
            var contents = getFieldValue(owner, "contents");
            if (contents != null) {
                return contents;
            }
        }

        owner = getFieldValue(moduleDataProvider, "this$0");
        if (owner != null) {
            var contents = getFieldValue(owner, "contents");
            if (contents != null) {
                return contents;
            }
        }

        return null;
    }

    private static Path pathFromLocation(final Optional<URI> location) {
        try {
            if (location.isPresent() && "file".equalsIgnoreCase(location.get().getScheme())) {
                return Path.of(location.get());
            }
        } catch (RuntimeException ignored) {
        }
        return null;
    }

    private static java.lang.reflect.Field findField(Class<?> type, String name) {
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            try {
                return current.getDeclaredField(name);
            } catch (NoSuchFieldException ignored) {
            }
        }
        return null;
    }

    private static Object getFieldValue(final Object target, final String name) {
        if (target == null) {
            return null;
        }
        try {
            var field = findField(target.getClass(), name);
            if (field == null) {
                return null;
            }
            field.setAccessible(true);
            return field.get(target);
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static Object invokeNoArg(final Object target, final String name) {
        if (target == null) {
            return null;
        }
        for (Class<?> current = target.getClass(); current != null; current = current.getSuperclass()) {
            try {
                var method = current.getDeclaredMethod(name);
                method.setAccessible(true);
                return method.invoke(target);
            } catch (NoSuchMethodException ignored) {
            } catch (Throwable ignored) {
                return null;
            }
        }
        return null;
    }

    private static byte[] readClassBytesFromPath(final Path basePath, final String classResourceName) {
        if (basePath == null) {
            return new byte[0];
        }
        try {
            if (Files.isDirectory(basePath)) {
                Path file = basePath.resolve(classResourceName.replace('/', basePath.getFileSystem().getSeparator().charAt(0)));
                if (Files.exists(file)) {
                    return Files.readAllBytes(file);
                }
            } else {
                try (var jar = new JarFile(basePath.toFile())) {
                    var entry = jar.getJarEntry(classResourceName);
                    if (entry != null && !entry.isDirectory()) {
                        try (var in = jar.getInputStream(entry)) {
                            return in.readAllBytes();
                        }
                    }
                }
            }
        } catch (IOException | RuntimeException ignored) {
        }
        return new byte[0];
    }

    private static void logBackingClassRead(final String source, final String className, final String moduleName, final Path path) {
        if (shouldLogBackingClass(className)) {
            System.err.println("[banditvault] ModuleClassLoader UWP backing " + source + " bytes " + className + " -> " + moduleName + " from " + path);
        }
    }

    private static void logBackingClassMiss(final String className, final String moduleName, final String reason) {
        if (shouldLogBackingClass(className) && "minecraft".equals(moduleName)) {
            System.err.println("[banditvault] ModuleClassLoader UWP backing miss " + className + ": " + reason + " for " + moduleName);
        }
    }

    private static boolean shouldLogBackingClass(final String className) {
        if (!Boolean.getBoolean("banditvault.securejarhandler.debug")) {
            return false;
        }
        return className.equals("net.minecraft.client.main.Main")
                || className.startsWith("net.minecraft.core.")
                || className.startsWith("net.minecraft.client.Minecraft")
                || className.startsWith("net.minecraft.resources.")
                || className.startsWith("net.minecraft.server.packs.")
                || className.startsWith("com.mojang.blaze3d.")
                || className.startsWith("com.mojang.math.");
    }

    private static boolean isMinecraftClass(final String className) {
        return className.startsWith("net.minecraft.")
                || className.startsWith("com.mojang.blaze3d.")
                || className.startsWith("com.mojang.math.");
    }

    private static boolean shouldTryResourceFallback(final String className) {
        if (isMinecraftClass(className)) {
            return true;
        }
        if (Boolean.getBoolean("banditvault.securejarhandler.resourceFallbackAll")) {
            return true;
        }

        final var prefixes = System.getProperty("banditvault.securejarhandler.resourceFallbackPrefixes", "");
        if (prefixes.isBlank()) {
            return false;
        }
        for (var prefix : prefixes.split(",")) {
            prefix = prefix.trim();
            if (!prefix.isEmpty() && className.startsWith(prefix)) {
                return true;
            }
        }
        return false;
    }

    private Class<?> readerToClass(final ModuleReader reader, final ModuleReference ref, final String name) {
        return defineClassBytes(ref, name, getClassBytes(reader, ref, name));
    }

    private Class<?> defineClassBytes(final ModuleReference ref, final String name, final byte[] rawBytes) {
        var bytes = maybeTransformClassBytes(rawBytes, name, null);
        if (bytes.length == 0) return null;
        var cname = name.replace('.','/')+".class";
        var modroot = ref == null ? null : this.resolvedRoots.get(ref.descriptor().name());
        if (modroot == null) {
            if (shouldLogBackingClass(name)) {
                System.err.println("[banditvault] ModuleClassLoader UWP defining descriptor-hidden class "
                        + name + " from " + (ref == null ? "direct-srg" : ref.descriptor().name()));
            }
            var cls = defineClass(name, bytes, 0, bytes.length);
            rememberModule(cls.getModule());
            return cls;
        }
        ensureNeoForgeCanReadMinecraft(ref.descriptor().name(), name);
        ProtectionDomainHelper.tryDefinePackage(this, name, modroot.jar().getManifest(), t->modroot.jar().getManifest().getAttributes(t), this::definePackage); // Packages are dirctories, and can't be signed, so use raw attributes instead of signed.
        var cs = ProtectionDomainHelper.createCodeSource(toURL(ref.location()), modroot.jar().verifyAndGetSigners(cname, bytes));
        var cls = defineClass(name, bytes, 0, bytes.length, ProtectionDomainHelper.createProtectionDomain(cs, this));
        rememberModule(cls.getModule());
        ensureNeoForgeCanReadMinecraft(ref.descriptor().name(), name);
        ProtectionDomainHelper.trySetPackageModule(cls.getPackage(), cls.getModule());
        return cls;
    }

    private static void ensureNeoForgeCanReadMinecraft(final String sourceModuleName, final String className) {
        if ("minecraft".equals(sourceModuleName)) {
            return;
        }
        if (!"neoforge".equals(sourceModuleName) && !className.startsWith("net.neoforged.neoforge.")) {
            return;
        }
        addModuleReadEdge(sourceModuleName, "minecraft", className);
    }

    private static void rememberModule(final Module module) {
        if (module != null && module.isNamed()) {
            UWP_SEEN_MODULES.put(module.getName(), module);
        }
    }

    @SuppressWarnings("unchecked")
    private static Optional<Module> findLoadedModule(final String moduleName) {
        final var seen = UWP_SEEN_MODULES.get(moduleName);
        if (seen != null) {
            return Optional.of(seen);
        }
        try {
            return (Optional<Module>) MODULES_FIND_LOADED_MODULE.invokeExact(moduleName);
        } catch (Throwable ignored) {
            return Optional.empty();
        }
    }

    private static void addModuleReadEdge(final String sourceModuleName, final String targetModuleName, final String className) {
        final var key = sourceModuleName + "->" + targetModuleName;
        if (UWP_ADDED_READ_EDGES.contains(key)) {
            return;
        }
        final var source = findLoadedModule(sourceModuleName);
        final var target = findLoadedModule(targetModuleName);
        if (source.isEmpty() || target.isEmpty()) {
            final var missKey = key + ":" + source.isPresent() + ":" + target.isPresent();
            if (UWP_REPORTED_READ_EDGE_MISSES.add(missKey)) {
                System.err.println("[banditvault] ModuleClassLoader UWP could not add module read edge "
                        + sourceModuleName + " -> " + targetModuleName
                        + " while defining " + className
                        + " sourceLoaded=" + source.isPresent()
                        + " targetLoaded=" + target.isPresent());
            }
            return;
        }
        if (source.get().canRead(target.get())) {
            UWP_ADDED_READ_EDGES.add(key);
            return;
        }
        try {
            MODULES_ADD_READS.invokeExact(source.get(), target.get());
            UWP_ADDED_READ_EDGES.add(key);
            System.err.println("[banditvault] ModuleClassLoader UWP added module read edge "
                    + sourceModuleName + " -> " + targetModuleName
                    + " while defining " + className);
        } catch (Throwable t) {
            final var missKey = key + ":error:" + t.getClass().getName() + ":" + String.valueOf(t.getMessage());
            if (UWP_REPORTED_READ_EDGE_MISSES.add(missKey)) {
                System.err.println("[banditvault] ModuleClassLoader UWP failed to add module read edge "
                        + sourceModuleName + " -> " + targetModuleName
                        + " while defining " + className
                        + ": " + t.getClass().getName() + ": " + String.valueOf(t.getMessage()));
            }
        }
    }

    protected byte[] maybeTransformClassBytes(final byte[] bytes, final String name, final String context) {
        return bytes;
    }

    @Override
    protected Class<?> loadClass(final String name, final boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            final var loadKey = System.identityHashCode(this) + ":" + name;
            final var inProgress = UWP_LOAD_CLASS_IN_PROGRESS.get();
            if (!inProgress.add(loadKey)) {
                var recursiveClass = findLoadedClass(name);
                if (recursiveClass == null) {
                    recursiveClass = findClassByResource(name, true);
                }
                if (recursiveClass == null) {
                    throw new ClassNotFoundException(name + " (recursive SecureJar delegation by " + getName() + ")");
                }
                if (resolve) resolveClass(recursiveClass);
                return recursiveClass;
            }
            try {
                var c = findLoadedClass(name);
                if (c == null) {
                    var index = name.lastIndexOf('.');
                    if (index >= 0) {
                        final var pname = name.substring(0, index);
                        if (isMinecraftClass(name) && !isTransformerLoader()) {
                            c = loadMinecraftClassFromContext(name);
                        }
                        if (c == null && this.packageLookup.containsKey(pname)) {
                            c = findClass(this.packageLookup.get(pname).name(), name);
                        }
                        if (c == null && shouldTryResourceFallback(name)) {
                            c = findClassByResource(name);
                        }
                        if (c == null) {
                            if (isMinecraftClass(name)) {
                                c = loadMinecraftClassFromContext(name);
                                if (c == null) {
                                    throw new ClassNotFoundException(name + " (not found in resolved SecureJar modules by " + getName() + ")");
                                }
                            } else {
                                var delegate = this.parentLoaders.getOrDefault(pname, fallbackClassLoader);
                                if (delegate == this) {
                                    c = findClassByResource(name, true);
                                    if (c == null && fallbackClassLoader != this) {
                                        c = fallbackClassLoader.loadClass(name);
                                    }
                                } else {
                                    c = delegate.loadClass(name);
                                }
                            }
                        }
                    }
                }
                if (c == null) throw new ClassNotFoundException(name);
                if (resolve) resolveClass(c);
                return c;
            } finally {
                inProgress.remove(loadKey);
            }
        }
    }

    private Class<?> findClassByResource(final String name) {
        return findClassByResource(name, false);
    }

    private Class<?> findClassByResource(final String name, final boolean force) {
        if ((!force && !shouldTryResourceFallback(name)) || UWP_RESOURCE_FALLBACK_MISSES.contains(name)) {
            return null;
        }
        final var inProgress = UWP_RESOURCE_FALLBACK_IN_PROGRESS.get();
        if (!inProgress.add(name)) {
            return null;
        }
        try {
            if (isMinecraftClass(name) && !isTransformerLoader()) {
                return loadMinecraftClassFromContext(name);
            }

            final var resourceName = name.replace('.', '/') + ".class";
            for (var root : this.resolvedRoots.entrySet()) {
                var jar = root.getValue().jar();
                byte[] backingBytes = getBackingClassBytes(root.getValue(), resourceName, name);
                if (backingBytes.length == 0 && jar.findFile(resourceName).isEmpty()) {
                    continue;
                }

                System.err.println("[banditvault] ModuleClassLoader UWP package fallback " + name + " -> " + root.getKey());
                var c = findClass(root.getKey(), name);
                if (c != null) {
                    return c;
                }
            }
            if (isMinecraftClass(name)) {
                var c = findMinecraftClassByReader(name, resourceName);
                if (c != null) {
                    return c;
                }
            }
            UWP_RESOURCE_FALLBACK_MISSES.add(name);
            return null;
        } finally {
            inProgress.remove(name);
        }
    }

    private Class<?> loadMinecraftClassFromContext(final String name) {
        var contextLoader = Thread.currentThread().getContextClassLoader();
        if (contextLoader == null || contextLoader == this) {
            return null;
        }
        try {
            var cls = contextLoader.loadClass(name);
            rememberModule(cls.getModule());
            if (shouldLogBackingClass(name)) {
                System.err.println("[banditvault] ModuleClassLoader UWP delegated Minecraft class "
                        + name + " from " + getName() + " to context " + contextLoader.getName());
            }
            return cls;
        } catch (ClassNotFoundException | LinkageError ignored) {
            return null;
        }
    }

    private Class<?> findMinecraftClassByReader(final String name, final String resourceName) {
        for (var module : this.configuration.modules()) {
            var ref = module.reference();
            try (var reader = ref.open()) {
                var bytes = getClassBytes(reader, ref, name);
                if (bytes.length == 0) {
                    continue;
                }
                if (shouldLogBackingClass(name)) {
                    System.err.println("[banditvault] ModuleClassLoader UWP descriptor-hidden package fallback "
                            + name + " -> " + ref.descriptor().name());
                }
                return defineClassBytes(ref, name, bytes);
            } catch (IOException ignored) {
            }
        }
        if (!isTransformerLoader()) {
            if (shouldLogBackingClass(name)) {
                System.err.println("[banditvault] ModuleClassLoader UWP skipped direct SRG fallback "
                        + name + " in " + getName());
            }
            return null;
        }
        var bytes = readMinecraftSrgClassBytes(resourceName);
        if (bytes.length != 0) {
            if (shouldLogBackingClass(name)) {
                System.err.println("[banditvault] ModuleClassLoader UWP direct SRG fallback "
                        + name + " in " + getName() + " from " + getMinecraftSrgJarPath());
            }
            return defineClassBytes(null, name, bytes);
        }
        return null;
    }

    private boolean isTransformerLoader() {
        return "TRANSFORMER".equals(getName());
    }

    private static byte[] readMinecraftSrgClassBytes(final String resourceName) {
        var path = getMinecraftSrgJarPath();
        if (path == null) {
            return new byte[0];
        }
        return readClassBytesFromPath(path, resourceName);
    }

    private static Path getMinecraftSrgJarPath() {
        var cached = minecraftSrgJarPath;
        if (cached != null) {
            return cached;
        }
        var value = System.getProperty("banditvault.neoforge.minecraftSrgJar", "");
        if (value == null || value.isBlank()) {
            return null;
        }
        try {
            cached = Path.of(value);
            minecraftSrgJarPath = cached;
            return cached;
        } catch (RuntimeException ignored) {
            return null;
        }
    }

    @Override
    protected Class<?> findClass(final String name) throws ClassNotFoundException {
        final String mname = classNameToModuleName(name);
        if (mname != null) {
            return findClass(mname, name);
        } else {
            return super.findClass(name);
        }
    }

    protected String classNameToModuleName(final String name) {
        final var pname = name.substring(0, name.lastIndexOf('.'));
        return Optional.ofNullable(this.packageLookup.get(pname)).map(ResolvedModule::name).orElse(null);
    }

    private Package definePackage(final String[] args) {
        return definePackage(args[0], args[1], args[2], args[3], args[4], args[5], args[6], null);
    }

    @Override
    public URL getResource(final String name) {
        try {
            var reslist = findResourceList(name);
            if (!reslist.isEmpty()) {
                return reslist.get(0);
            } else {
                return fallbackClassLoader.getResource(name);
            }
        } catch (IOException e) {
            return null;
        }
    }

    @Override
    protected URL findResource(final String moduleName, final String name) throws IOException {
        try {
            return loadFromModule(moduleName, (reader, ref) -> this.readerToURL(reader, ref, name));
        } catch (UncheckedIOException ioe) {
            throw ioe.getCause();
        }
    }

    @Override
    public Enumeration<URL> getResources(final String name) throws IOException {
        return Collections.enumeration(findResourceList(name));
    }

    private List<URL> findResourceList(final String name) throws IOException {
        var idx = name.lastIndexOf('/');
        var pkgname =  (idx == -1 || idx==name.length()-1) ? "" : name.substring(0,idx).replace('/','.');
        var module = packageLookup.get(pkgname);
        if (module != null) {
            var res = findResource(module.name(), name);
            return res != null ? List.of(res): List.of();
        } else {
            return resolvedRoots.values().stream()
                    .map(JarModuleFinder.JarModuleReference::jar)
                    .map(jar->jar.findFile(name))
                    .map(ModuleClassLoader::toURL)
                    .filter(Objects::nonNull)
                    .toList();
        }
    }

    @Override
    protected Enumeration<URL> findResources(final String name) throws IOException {
        return Collections.enumeration(findResourceList(name));
    }

    @Override
    protected Class<?> findClass(final String moduleName, final String name) {
        try {
            return loadFromModule(moduleName, (reader, ref) -> this.readerToClass(reader, ref, name));
        } catch (IOException e) {
            return null;
        }
    }

    protected <T> T loadFromModule(final String moduleName, BiFunction<ModuleReader, ModuleReference, T> lookup) throws IOException {
        var module = configuration.findModule(moduleName);
        if (module.isEmpty()) {
            throw new NoSuchFileException("module " + moduleName);
        }
        var ref = module.get().reference();
        try (var reader = ref.open()) {
            return lookup.apply(reader, ref);
        }
    }

    protected byte[] getMaybeTransformedClassBytes(final String name, final String context) throws ClassNotFoundException {
        byte[] bytes = new byte[0];
        Throwable suppressed = null;
        try {
            final var pname = name.substring(0, name.lastIndexOf('.'));
            if (this.packageLookup.containsKey(pname)) {
                bytes = loadFromModule(classNameToModuleName(name), (reader, ref)->this.getClassBytes(reader, ref, name));
            } else if (this.parentLoaders.containsKey(pname)) {
                var cname = name.replace('.','/')+".class";
                try (var is = this.parentLoaders.get(pname).getResourceAsStream(cname)) {
                    if (is != null)
                        bytes = is.readAllBytes();
                }
            }
        } catch (IOException e) {
            suppressed = e;
        }
        byte[] maybeTransformedBytes = maybeTransformClassBytes(bytes, name, context);
        if (maybeTransformedBytes.length == 0) {
            ClassNotFoundException e = new ClassNotFoundException(name);
            if (suppressed != null) e.addSuppressed(suppressed);
            throw e;
        }
        return maybeTransformedBytes;
    }

    public void setFallbackClassLoader(final ClassLoader fallbackClassLoader) {
        this.fallbackClassLoader = fallbackClassLoader;
    }
}
