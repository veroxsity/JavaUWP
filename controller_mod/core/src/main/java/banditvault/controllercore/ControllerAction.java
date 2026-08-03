package banditvault.controllercore;

public enum ControllerAction {
    ATTACK("attack", "Attack"),
    USE("use", "Use"),
    JUMP("jump", "Jump"),
    SNEAK("sneak", "Sneak"),
    SPRINT("sprint", "Sprint"),
    INVENTORY("inventory", "Inventory"),
    DROP("drop", "Drop"),
    SWAP_HANDS("swapHands", "Swap Hands"),
    PICK_BLOCK("pickBlock", "Pick Block"),
    PAUSE("pause", "Pause"),
    HOTBAR_PREVIOUS("hotbarPrevious", "Hotbar Prev"),
    HOTBAR_NEXT("hotbarNext", "Hotbar Next"),
    MENU_ACCEPT("menuAccept", "Menu Accept"),
    MENU_CANCEL("menuCancel", "Menu Cancel"),
    SNAP_FREE_TOGGLE("snapFreeToggle", "Snap/Free"),
    QUICK_MOVE("quickMove", "Quick Move"),
    MENU_SECONDARY("menuSecondary", "Secondary"),
    RADIAL_MENU("radialMenu", "Radial Menu");

    public final String id;
    public final String label;

    ControllerAction(String id, String label) {
        this.id = id;
        this.label = label;
    }
}
