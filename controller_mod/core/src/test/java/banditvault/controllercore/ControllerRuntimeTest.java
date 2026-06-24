package banditvault.controllercore;

public final class ControllerRuntimeTest {
    public static void main(String[] args) {
        check(ControllerRuntime.shouldHoldKey(true, false, false));
        check(ControllerRuntime.shouldHoldKey(false, true, true));
        check(!ControllerRuntime.shouldHoldKey(false, true, false));
    }

    private static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError("controller key policy failed");
        }
    }
}
