package com.onyx.controller.babric;

import java.lang.reflect.Field;

/** Applies gamepad state to Beta 1.7.3's existing KeyBinding objects. */
public final class LegacyKeyBindings {
    private LegacyKeyBindings() {}

    public static void apply(Object minecraft) {
        if (minecraft == null) return;
        Object settings = findSettings(minecraft);
        if (settings == null) return;

        GamepadBridge.State pad = GamepadBridge.STATE;
        setPressed(settings, "keyBindForward", pad.leftY < 0.0f);
        setPressed(settings, "keyBindBack", pad.leftY > 0.0f);
        setPressed(settings, "keyBindLeft", pad.leftX < 0.0f);
        setPressed(settings, "keyBindRight", pad.leftX > 0.0f);
        setPressed(settings, "keyBindJump", pad.a);
        setPressed(settings, "keyBindSneak", pad.b || pad.leftStick);
        setPressed(settings, "keyBindDrop", pad.x);
        setPressed(settings, "keyBindInventory", pad.y);
        setPressed(settings, "keyBindAttack", pad.rightTrigger > 0.55f || pad.rightBumper);
        setPressed(settings, "keyBindUseItem", pad.leftTrigger > 0.55f || pad.leftBumper);
        setPressed(settings, "keyBindSprint", pad.rightStick);
    }

    private static Object findSettings(Object minecraft) {
        Class<?> type = minecraft.getClass();
        while (type != null) {
            Field[] fields = type.getDeclaredFields();
            for (int i = 0; i < fields.length; i++) {
                Field field = fields[i];
                String name = field.getName().toLowerCase();
                if (!name.contains("game") && !name.contains("option") && !name.contains("setting")) continue;
                try {
                    field.setAccessible(true);
                    Object value = field.get(minecraft);
                    if (value != null && hasField(value.getClass(), "keyBindForward")) return value;
                } catch (Throwable ignored) {}
            }
            type = type.getSuperclass();
        }
        return null;
    }

    private static boolean hasField(Class<?> type, String name) {
        try { type.getField(name); return true; } catch (Throwable ignored) {}
        try { type.getDeclaredField(name); return true; } catch (Throwable ignored) {}
        return false;
    }

    private static void setPressed(Object settings, String name, boolean pressed) {
        try {
            Field bindingField;
            try { bindingField = settings.getClass().getField(name); }
            catch (NoSuchFieldException ignored) { bindingField = settings.getClass().getDeclaredField(name); }
            bindingField.setAccessible(true);
            Object binding = bindingField.get(settings);
            if (binding == null) return;

            Field pressedField;
            try { pressedField = binding.getClass().getField("pressed"); }
            catch (NoSuchFieldException ignored) { pressedField = binding.getClass().getDeclaredField("pressed"); }
            pressedField.setAccessible(true);
            pressedField.setBoolean(binding, pressed);
        } catch (Throwable ignored) {
            // Mapping/field differences are reported by the runtime smoke test.
        }
    }
}
