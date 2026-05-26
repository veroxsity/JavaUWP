package net.fabricmc.loader.impl.launch;

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.util.Map;

import net.fabricmc.loader.impl.FabricLoaderImpl;
import net.fabricmc.loader.impl.FormattedException;
import net.fabricmc.loader.impl.game.GameProvider;
import net.fabricmc.loader.impl.util.SystemProperties;
import net.fabricmc.loader.impl.util.log.Log;
import net.fabricmc.loader.impl.util.log.LogCategory;

public abstract class FabricLauncherBase implements FabricLauncher {
    protected static final boolean IS_DEVELOPMENT = SystemProperties.isSet(SystemProperties.DEVELOPMENT);

    private static boolean mixinReady;
    private static Map<String, Object> properties;
    private static FabricLauncher launcher;
    private static MappingConfiguration mappingConfiguration = new MappingConfiguration();

    protected FabricLauncherBase() {
        setLauncher(this);
    }

    public static Class<?> getClass(String className) throws ClassNotFoundException {
        return Class.forName(className, true, getLauncher().getTargetClassLoader());
    }

    @Override
    public final boolean isDevelopment() {
        return IS_DEVELOPMENT;
    }

    @Override
    public MappingConfiguration getMappingConfiguration() {
        return mappingConfiguration;
    }

    protected static void setProperties(Map<String, Object> propertiesA) {
        if (properties != null && properties != propertiesA) {
            throw new RuntimeException("Duplicate setProperties call!");
        }

        properties = propertiesA;
    }

    public static void setLauncher(FabricLauncher launcherA) {
        if (launcher != null && launcher != launcherA) {
            throw new RuntimeException("Duplicate setLauncher call!");
        }

        launcher = launcherA;
    }

    public static FabricLauncher getLauncher() {
        return launcher;
    }

    public static Map<String, Object> getProperties() {
        return properties;
    }

    protected static void handleFormattedException(FormattedException exc) {
        Throwable actualExc = exc.getMessage() != null ? exc : exc.getCause();
        Log.error(LogCategory.GENERAL, exc.getMainText(), actualExc);
        printFailure(exc.getDisplayedText(), actualExc);
        throw exc;
    }

    protected static void setupUncaughtExceptionHandler() {
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                try {
                    if (e instanceof FormattedException) {
                        handleFormattedException((FormattedException) e);
                    } else {
                        String mainText = String.format("Uncaught exception in thread \"%s\"", t.getName());
                        Log.error(LogCategory.GENERAL, mainText, e);
                        printFailure(mainText, e);
                    }
                } catch (Throwable e2) {
                    e.addSuppressed(e2);
                    printFailure("Exception while reporting Fabric failure", e);
                }
            }
        });
    }

    private static void printFailure(String text, Throwable throwable) {
        PrintWriter pw = new PrintWriter(new FileOutputStream(FileDescriptor.err));
        pw.println("Fabric Loader failure:");
        pw.println(text);
        if (throwable != null) {
            throwable.printStackTrace(pw);
        }
        pw.flush();
    }

    protected static void finishMixinBootstrapping() {
        if (mixinReady) {
            throw new RuntimeException("Must not call FabricLauncherBase.finishMixinBootstrapping() twice!");
        }

        try {
            Class<?> mixinEnvironmentClass = Class.forName("org.spongepowered.asm.mixin.MixinEnvironment");
            Class<?> phaseClass = Class.forName("org.spongepowered.asm.mixin.MixinEnvironment$Phase");
            Object initPhase = phaseClass.getField("INIT").get(null);
            Object defaultPhase = phaseClass.getField("DEFAULT").get(null);
            Method m = mixinEnvironmentClass.getDeclaredMethod("gotoPhase", phaseClass);
            m.setAccessible(true);
            m.invoke(null, initPhase);
            m.invoke(null, defaultPhase);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }

        mixinReady = true;
    }

    public static boolean isMixinReady() {
        return mixinReady;
    }
}
