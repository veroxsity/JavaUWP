package banditvault.cleanroomcontroller;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

// most of what menu navigation needs is protected or private in 1.12, members are looked up by srg name once
final class CleanroomReflect {
    private static final Map<String, Field> FIELDS = new HashMap<String, Field>();
    private static final Map<String, Method> METHODS = new HashMap<String, Method>();
    private static final Set<String> MISSING = new HashSet<String>();

    private CleanroomReflect() {
    }

    static Object get(Object target, Class<?> owner, String srg) {
        Field field = field(owner, srg);
        if (field == null || target == null) return null;
        try {
            return field.get(target);
        } catch (IllegalAccessException e) {
            return null;
        }
    }

    static int getInt(Object target, Class<?> owner, String srg, int fallback) {
        Object value = get(target, owner, srg);
        return value instanceof Number ? ((Number) value).intValue() : fallback;
    }

    static float getFloat(Object target, Class<?> owner, String srg, float fallback) {
        Object value = get(target, owner, srg);
        return value instanceof Number ? ((Number) value).floatValue() : fallback;
    }

    static boolean getBoolean(Object target, Class<?> owner, String srg, boolean fallback) {
        Object value = get(target, owner, srg);
        return value instanceof Boolean ? (Boolean) value : fallback;
    }

    static Object call(Object target, Class<?> owner, String srg, Class<?>[] types, Object... args) {
        Method method = method(owner, srg, types);
        if (method == null || target == null) return null;
        try {
            return method.invoke(target, args);
        } catch (ReflectiveOperationException | RuntimeException e) {
            CleanroomControllerLog.logException("Reflective call failed " + owner.getSimpleName() + "." + srg, e);
            return null;
        }
    }

    private static Field field(Class<?> owner, String srg) {
        String key = owner.getName() + "#" + srg;
        if (FIELDS.containsKey(key)) return FIELDS.get(key);
        Field found = null;
        for (Class<?> c = owner; c != null && found == null; c = c.getSuperclass()) {
            try {
                found = c.getDeclaredField(srg);
                found.setAccessible(true);
            } catch (NoSuchFieldException | RuntimeException ignored) {
                // not declared here, the loop tries the superclass
                found = null;
            }
        }
        FIELDS.put(key, found);
        if (found == null && MISSING.add(key)) CleanroomControllerLog.log("Field not found " + key);
        return found;
    }

    private static Method method(Class<?> owner, String srg, Class<?>[] types) {
        String key = owner.getName() + "#" + srg;
        if (METHODS.containsKey(key)) return METHODS.get(key);
        Method found = null;
        for (Class<?> c = owner; c != null && found == null; c = c.getSuperclass()) {
            try {
                found = c.getDeclaredMethod(srg, types);
                found.setAccessible(true);
            } catch (NoSuchMethodException | RuntimeException ignored) {
                // not declared here, the loop tries the superclass
                found = null;
            }
        }
        METHODS.put(key, found);
        if (found == null && MISSING.add(key)) CleanroomControllerLog.log("Method not found " + key);
        return found;
    }
}
