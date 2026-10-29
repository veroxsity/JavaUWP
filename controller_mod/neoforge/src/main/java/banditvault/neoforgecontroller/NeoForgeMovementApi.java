package banditvault.neoforgecontroller;

final class NeoForgeMovementApi {
    private NeoForgeMovementApi() {
    }

    // true on versions that ship the keyboard input mixin, the rest keep holding the movement keys
    static boolean analogSupported() {
        return false;
    }
}
