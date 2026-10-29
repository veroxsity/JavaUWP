package banditvault.controllercore;

import java.util.Properties;

public final class ControllerRuntimeTest {
    public static void main(String[] args) {
        check(ControllerRuntime.shouldHoldKey(true, false, false));
        check(ControllerRuntime.shouldHoldKey(false, true, true));
        check(!ControllerRuntime.shouldHoldKey(false, true, false));

        float[] idle = ControllerRuntime.shapedMovement(0.2f, -0.2f, 0.35f);
        close(idle[0], 0.0f);
        close(idle[1], 0.0f);

        float[] full = ControllerRuntime.shapedMovement(-1.0f, 0.0f, 0.35f);
        close(full[0], -1.0f);
        close(full[1], 0.0f);

        float[] diagonal = ControllerRuntime.shapedMovement(1.0f, 1.0f, 0.35f);
        close((float)Math.sqrt(diagonal[0] * diagonal[0] + diagonal[1] * diagonal[1]), 1.0f);

        float[] keyboardIdle = ControllerRuntime.keyboardMovement(0.2f, -0.2f, 0.35f);
        close(keyboardIdle[0], 0.0f);
        close(keyboardIdle[1], 0.0f);
        float[] keyboardDiagonal = ControllerRuntime.keyboardMovement(0.7f, -0.8f, 0.35f);
        close(keyboardDiagonal[0], 1.0f);
        close(keyboardDiagonal[1], -1.0f);
        float[] keyboardAxis = ControllerRuntime.keyboardMovement(-0.5f, 0.3f, 0.35f);
        close(keyboardAxis[0], -1.0f);
        close(keyboardAxis[1], 0.0f);

        ControllerSettingsStore saved = new ControllerSettingsStore() { };
        saved.keyboardMovement = true;
        saved.showGuides = false;
        Properties properties = new Properties();
        saved.writeTo(properties);
        ControllerSettingsStore loaded = new ControllerSettingsStore() { };
        loaded.readFrom(properties);
        check(loaded.keyboardMovement);
        check(!loaded.showGuides);
        check(!new ControllerSettingsStore() { }.keyboardMovement);
        check(new ControllerSettingsStore() { }.showGuides);

        check(ControllerRuntime.radialSlot(0.0f, 0.1f, 0.2f) == -1);
        check(ControllerRuntime.radialSlot(0.0f, -1.0f, 0.2f) == 0);
        check(ControllerRuntime.radialSlot(1.0f, -1.0f, 0.2f) == 1);
        check(ControllerRuntime.radialSlot(1.0f, 0.0f, 0.2f) == 2);
        check(ControllerRuntime.radialSlot(1.0f, 1.0f, 0.2f) == 3);
        check(ControllerRuntime.radialSlot(0.0f, 1.0f, 0.2f) == 4);
        check(ControllerRuntime.radialSlot(-1.0f, 1.0f, 0.2f) == 5);
        check(ControllerRuntime.radialSlot(-1.0f, 0.0f, 0.2f) == 6);
        check(ControllerRuntime.radialSlot(-1.0f, -1.0f, 0.2f) == 7);
    }

    private static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError("controller key policy failed");
        }
    }

    private static void close(float actual, float expected) {
        if (Math.abs(actual - expected) > 0.0001f) {
            throw new AssertionError("expected " + expected + ", got " + actual);
        }
    }
}
