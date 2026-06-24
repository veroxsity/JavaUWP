package banditvault.controllercore;

public final class ControllerRuntime {
    private boolean active;
    private boolean crouchToggled;
    private boolean sprintToggled;
    private boolean followingCompanion;
    private double cursorX = -1.0;
    private double cursorY = -1.0;
    private long lastLookNanos;
    private int scrollCooldown;

    public boolean active() {
        return active;
    }

    public void setActive(boolean active) {
        this.active = active;
    }

    public void deactivate() {
        active = false;
        crouchToggled = false;
        sprintToggled = false;
        followingCompanion = false;
        lastLookNanos = 0L;
    }

    public boolean crouchToggled() {
        return crouchToggled;
    }

    public void toggleCrouch() {
        crouchToggled = !crouchToggled;
    }

    public void resetCrouchToggle() {
        crouchToggled = false;
    }

    public boolean sprintToggled() {
        return sprintToggled;
    }

    public void toggleSprint() {
        sprintToggled = !sprintToggled;
    }

    public void resetSprintToggle() {
        sprintToggled = false;
    }

    public boolean followingCompanion() {
        return followingCompanion;
    }

    public void setFollowingCompanion(boolean followingCompanion) {
        this.followingCompanion = followingCompanion;
    }

    public double cursorX() {
        return cursorX;
    }

    public double cursorY() {
        return cursorY;
    }

    public boolean hasCursor() {
        return cursorX >= 0.0 && cursorY >= 0.0;
    }

    public void ensureCursor(double x, double y) {
        if (!hasCursor()) {
            cursorX = Math.max(1.0, x);
            cursorY = Math.max(1.0, y);
        }
    }

    public void setCursor(double x, double y, double maxX, double maxY) {
        cursorX = clamp(x, 0.0, Math.max(1.0, maxX));
        cursorY = clamp(y, 0.0, Math.max(1.0, maxY));
    }

    public void moveCursor(double dx, double dy, double maxX, double maxY) {
        setCursor(cursorX + dx, cursorY + dy, maxX, maxY);
    }

    public int scrollCooldown() {
        return scrollCooldown;
    }

    public void tickScrollCooldown() {
        if (scrollCooldown > 0) {
            scrollCooldown--;
        }
    }

    public void setScrollCooldown(int scrollCooldown) {
        this.scrollCooldown = scrollCooldown;
    }

    public void resetLookClock() {
        lastLookNanos = 0L;
    }

    public float nextLookSeconds() {
        long now = System.nanoTime();
        float seconds = 1.0f / 60.0f;
        if (lastLookNanos != 0L) {
            seconds = (now - lastLookNanos) / 1000000000.0f;
            if (seconds < 1.0f / 240.0f) {
                seconds = 1.0f / 240.0f;
            } else if (seconds > 1.0f / 20.0f) {
                seconds = 1.0f / 20.0f;
            }
        }
        lastLookNanos = now;
        return seconds;
    }

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
