package banditvault.controllercore;

public final class ControllerRuntime {
    public static float shapedLookAxis(float value, float deadzone) {
        value = ControllerState.clampAxis(value);
        float abs = Math.abs(value);
        if (abs < deadzone) {
            return 0.0f;
        }
        float shaped = (abs - deadzone) / (1.0f - deadzone);
        shaped = shaped * shaped;
        return Math.copySign(shaped, value);
    }

    public static double shapedCursorAxis(float value, float deadzone) {
        value = ControllerState.clampAxis(value);
        float abs = Math.abs(value);
        if (abs < deadzone) {
            return 0.0;
        }
        return Math.copySign((abs - deadzone) / (1.0f - deadzone), value);
    }

    public static float[] shapedMovement(float x, float y, float deadzone) {
        x = ControllerState.clampAxis(x);
        y = ControllerState.clampAxis(y);
        float magnitude = (float)Math.sqrt(x * x + y * y);
        if (magnitude <= deadzone) {
            return new float[] { 0.0f, 0.0f };
        }
        float scaledMagnitude = (Math.min(magnitude, 1.0f) - deadzone) / (1.0f - deadzone);
        float scale = scaledMagnitude / magnitude;
        return new float[] { x * scale, y * scale };
    }

    public static float[] keyboardMovement(float x, float y, float deadzone) {
        x = ControllerState.clampAxis(x);
        y = ControllerState.clampAxis(y);
        return new float[] {
            x > deadzone ? 1.0f : x < -deadzone ? -1.0f : 0.0f,
            y > deadzone ? 1.0f : y < -deadzone ? -1.0f : 0.0f
        };
    }

    public static int radialSlot(float x, float y, float deadzone) {
        if (x * x + y * y <= deadzone * deadzone) {
            return -1;
        }
        double turn = Math.PI * 2.0;
        double angle = (Math.atan2(x, -y) + turn + Math.PI / 8.0) % turn;
        return (int)(angle / (Math.PI / 4.0));
    }

    public static boolean shouldHoldKey(boolean controllerHeld, boolean physicalHeld, boolean preservePhysicalInput) {
        return controllerHeld || (preservePhysicalInput && physicalHeld);
    }

    public static double clamp(double value, double min, double max) {
        if (value < min) {
            return min;
        }
        if (value > max) {
            return max;
        }
        return value;
    }
}
