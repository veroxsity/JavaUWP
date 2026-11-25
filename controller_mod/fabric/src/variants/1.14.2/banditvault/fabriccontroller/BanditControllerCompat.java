package banditvault.fabriccontroller;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.util.HashMap;
import java.util.Map;
import org.lwjgl.glfw.GLFW;
import net.minecraft.class_310;

/**
 * Legacy 1.14.2 controller adapter.
 *
 * LWJGL 3.2.2 predates GLFW's standardized gamepad-state API, so this adapter
 * reads the raw joystick axes/buttons exposed by the UWP GLFW shim.
 */
public final class BanditControllerCompat {
    private static final int JOYSTICK = GLFW.GLFW_JOYSTICK_1;
    private static final Map<String, Object> KEY_BINDINGS = new HashMap<String, Object>();
    private static boolean initialized;
    private static boolean wasConnected;
    private static boolean loggedMissing;
    private static long lastLog;

    private BanditControllerCompat() {
    }

    public static void tick(class_310 client) {
        if (client == null) return;
        try {
            if (!GLFW.glfwJoystickPresent(JOYSTICK)) {
                releaseAll();
                wasConnected = false;
                return;
            }

            FloatBuffer axes = GLFW.glfwGetJoystickAxes(JOYSTICK);
            ByteBuffer buttons = GLFW.glfwGetJoystickButtons(JOYSTICK);
            if (axes == null || buttons == null || axes.remaining() < 4 || buttons.remaining() < 10) {
                releaseAll();
                return;
            }

            if (!initialized) {
                discoverKeyBindings(client);
                initialized = true;
            }
            wasConnected = true;

            float lx = axis(axes, 0);
            float ly = axis(axes, 1);
            setKey("key.forward", ly < -0.25f);
            setKey("key.back", ly > 0.25f);
            setKey("key.left", lx < -0.25f);
            setKey("key.right", lx > 0.25f);

            // Standard GLFW gamepad button order: A, B, X, Y, LB, RB, Back, Start...
            setKey("key.jump", button(buttons, 0));
            setKey("key.sneak", button(buttons, 1));
            setKey("key.drop", button(buttons, 2));
            setKey("key.inventory", button(buttons, 3));
            setKey("key.attack", trigger(axes, 5) || button(buttons, 4));
            setKey("key.use", trigger(axes, 4) || button(buttons, 5));
            setKey("key.sprint", button(buttons, 9));

            if (button(buttons, 6)) pulse("key.inventory");
            if (button(buttons, 7)) pulse("key.chat");
            if (button(buttons, 10)) pulse("key.hotbar.1");
            if (button(buttons, 11)) pulse("key.hotbar.2");
            if (button(buttons, 12)) pulse("key.hotbar.3");
            if (button(buttons, 13)) pulse("key.hotbar.4");
            if (button(buttons, 14)) pulse("key.hotbar.5");
            if (button(buttons, 15)) pulse("key.hotbar.6");
        } catch (Throwable error) {
            releaseAll();
            long now = System.currentTimeMillis();
            if (!loggedMissing || now - lastLog > 10000L) {
                loggedMissing = true;
                lastLog = now;
                FabricControllerLog.logException("1.14.2 controller polling failed; check UWP GLFW joystick support", error);
            }
        }
    }

    private static float axis(FloatBuffer values, int index) {
        return index < values.limit() ? values.get(index) : 0.0f;
    }

    private static boolean button(ByteBuffer values, int index) {
        return index < values.limit() && values.get(index) == GLFW.GLFW_PRESS;
    }

    private static boolean trigger(FloatBuffer values, int index) {
        return index < values.limit() && values.get(index) > 0.25f;
    }

    private static void discoverKeyBindings(class_310 client) throws Exception {
        Object options = null;
        for (Field field : allFields(client.getClass())) {
            if (field.getType().getName().equals("net.minecraft.class_315")) {
                field.setAccessible(true);
                options = field.get(client);
                break;
            }
        }
        if (options == null) throw new IllegalStateException("Minecraft options object not found");

        Class<?> keyClass = Class.forName("net.minecraft.class_304");
        Method idMethod = keyClass.getDeclaredMethod("method_1431");
        idMethod.setAccessible(true);
        for (Field field : allFields(options.getClass())) {
            field.setAccessible(true);
            Object value = field.get(options);
            if (value != null && keyClass.isInstance(value)) {
                register(idMethod, value);
            } else if (value instanceof Object[]) {
                Object[] array = (Object[]) value;
                for (Object item : array) {
                    if (item != null && keyClass.isInstance(item)) register(idMethod, item);
                }
            }
        }
        if (KEY_BINDINGS.isEmpty()) throw new IllegalStateException("No Minecraft key bindings found");
    }

    private static void register(Method idMethod, Object key) throws Exception {
        String id = String.valueOf(idMethod.invoke(key));
        KEY_BINDINGS.put(id, key);
    }

    private static Field[] allFields(Class<?> type) {
        java.util.ArrayList<Field> result = new java.util.ArrayList<Field>();
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            Field[] fields = current.getDeclaredFields();
            for (Field field : fields) result.add(field);
        }
        return result.toArray(new Field[result.size()]);
    }

    private static void setKey(String id, boolean down) {
        Object key = KEY_BINDINGS.get(id);
        if (key == null) return;
        try {
            Method method = key.getClass().getDeclaredMethod("method_23481", boolean.class);
            method.setAccessible(true);
            method.invoke(key, down);
        } catch (Throwable error) {
            FabricControllerLog.logException("Could not update key binding " + id, error);
        }
    }

    private static void pulse(String id) {
        setKey(id, true);
        // The normal keybinding tick consumes the press; release is handled next client tick.
    }

    private static void releaseAll() {
        if (!wasConnected && KEY_BINDINGS.isEmpty()) return;
        for (String id : KEY_BINDINGS.keySet()) setKey(id, false);
    }
}
