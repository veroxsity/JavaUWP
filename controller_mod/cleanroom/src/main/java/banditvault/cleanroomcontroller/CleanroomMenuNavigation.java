package banditvault.cleanroomcontroller;

import banditvault.controllercore.GridNavigation;

import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiSlot;
import net.minecraft.client.gui.GuiTextField;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.Slot;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// 1.12 has no focus navigation, targets are read from the screen on every move so rebuilt screens stay correct
final class CleanroomMenuNavigation {
    static final class Position {
        final double x;
        final double y;

        Position(double x, double y) {
            this.x = x;
            this.y = y;
        }
    }

    private static final class Target {
        final double x;
        final double y;
        final Object ref;
        final int row;

        Target(double x, double y, Object ref, int row) {
            this.x = x;
            this.y = y;
            this.ref = ref;
            this.row = row;
        }

        boolean same(Object otherRef, int otherRow) {
            return ref == otherRef && row == otherRow;
        }
    }

    private static final Map<Class<?>, List<Field>> TEXT_FIELDS = new HashMap<Class<?>, List<Field>>();
    private static final Map<Class<?>, List<Field>> LIST_FIELDS = new HashMap<Class<?>, List<Field>>();

    private final Set<String> loggedEmpty = new HashSet<String>();
    private GuiScreen screen;
    private Object selectedRef;
    private int selectedRow = -1;

    void reset(GuiScreen next) {
        screen = next;
        selectedRef = null;
        selectedRow = -1;
    }

    Slot selectedSlot(GuiScreen current) {
        return current == screen && selectedRef instanceof Slot ? (Slot) selectedRef : null;
    }

    Position discover(GuiScreen current, double fromX, double fromY) {
        if (current != screen) reset(current);
        List<Target> targets = targets(current);
        Target best = null;
        double bestDistance = Double.MAX_VALUE;
        for (Target target : targets) {
            double dx = target.x - fromX;
            double dy = target.y - fromY;
            double distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                best = target;
                bestDistance = distance;
            }
        }
        if (best == null) {
            if (loggedEmpty.add(current.getClass().getName())) {
                CleanroomControllerLog.log("Snap navigation found no targets on " + current.getClass().getName());
            }
            return null;
        }
        return select(best);
    }

    Position move(GuiScreen current, GridNavigation.Direction direction, double fromX, double fromY) {
        if (current != screen || selectedRef == null) {
            return discover(current, fromX, fromY);
        }
        List<Target> targets = targets(current);
        int currentId = indexOf(targets, selectedRef, selectedRow);
        if (currentId < 0) {
            return discover(current, fromX, fromY);
        }
        if (selectedRef instanceof GuiSlot && (direction == GridNavigation.Direction.UP || direction == GridNavigation.Direction.DOWN)) {
            Position scrolled = stepList((GuiSlot) selectedRef, selectedRow, direction == GridNavigation.Direction.DOWN ? 1 : -1, current);
            if (scrolled != null) return scrolled;
        }
        List<GridNavigation.Point> points = new ArrayList<GridNavigation.Point>(targets.size());
        for (int i = 0; i < targets.size(); i++) {
            points.add(new GridNavigation.Point(i, (int) Math.round(targets.get(i).x), (int) Math.round(targets.get(i).y)));
        }
        int next = GridNavigation.next(points, currentId, direction);
        return next < 0 ? null : select(targets.get(next));
    }

    // the selected target can vanish when a screen rebuilds itself or a list scrolls under it
    Position synchronize(GuiScreen current, double fromX, double fromY) {
        if (current != screen || selectedRef == null) {
            return discover(current, fromX, fromY);
        }
        List<Target> targets = targets(current);
        int index = indexOf(targets, selectedRef, selectedRow);
        if (index < 0) return discover(current, fromX, fromY);
        Target target = targets.get(index);
        return Math.abs(target.x - fromX) > 0.5 || Math.abs(target.y - fromY) > 0.5 ? new Position(target.x, target.y) : null;
    }

    // a list row past the visible area is brought into view and selected
    private Position stepList(GuiSlot list, int row, int delta, GuiScreen current) {
        int size = listSize(list);
        int next = row + delta;
        if (next < 0 || next >= size) return null;
        Target nextTarget = listRow(list, next);
        if (nextTarget == null) {
            int slotHeight = CleanroomReflect.getInt(list, GuiSlot.class, "field_148149_f", 0); // slotHeight
            list.func_148145_f(delta * slotHeight); // scrollBy
            nextTarget = listRow(list, next);
        }
        return nextTarget == null ? null : select(nextTarget);
    }

    private Position select(Target target) {
        selectedRef = target.ref;
        selectedRow = target.row;
        return new Position(target.x, target.y);
    }

    private static int indexOf(List<Target> targets, Object ref, int row) {
        for (int i = 0; i < targets.size(); i++) {
            if (targets.get(i).same(ref, row)) return i;
        }
        return -1;
    }

    private static List<Target> targets(GuiScreen screen) {
        List<Target> targets = new ArrayList<Target>();
        if (screen instanceof GuiContainer) {
            GuiContainer container = (GuiContainer) screen;
            int left = CleanroomReflect.getInt(container, GuiContainer.class, "field_147003_i", 0); // guiLeft
            int top = CleanroomReflect.getInt(container, GuiContainer.class, "field_147009_r", 0); // guiTop
            Container menu = container.field_147002_h; // inventorySlots
            if (menu != null) {
                for (Slot slot : menu.field_75151_b) { // inventorySlots
                    if (slot.func_111238_b()) { // isEnabled
                        targets.add(new Target(left + slot.field_75223_e + 8, top + slot.field_75221_f + 8, slot, -1)); // xPos, yPos
                    }
                }
            }
        }

        Object buttons = CleanroomReflect.get(screen, GuiScreen.class, "field_146292_n"); // buttonList
        if (buttons instanceof List) {
            for (Object entry : (List<?>) buttons) {
                if (!(entry instanceof GuiButton)) continue;
                GuiButton button = (GuiButton) entry;
                if (!button.field_146125_m || !button.field_146124_l) continue; // visible, enabled
                int height = CleanroomReflect.getInt(button, GuiButton.class, "field_146121_g", 20); // height
                targets.add(new Target(button.field_146128_h + button.func_146117_b() / 2.0, button.field_146129_i + height / 2.0, button, -1)); // x, getButtonWidth, y
            }
        }

        for (GuiTextField field : textFields(screen)) {
            if (!field.func_146176_q()) continue; // getVisible
            int height = CleanroomReflect.getInt(field, GuiTextField.class, "field_146219_i", 20); // height
            targets.add(new Target(field.field_146209_f + field.func_146200_o() / 2.0, field.field_146210_g + height / 2.0, field, -1)); // x, getWidth, y
        }

        for (GuiSlot list : lists(screen)) {
            if (!CleanroomReflect.getBoolean(list, GuiSlot.class, "field_178041_q", true)) continue; // visible
            int size = listSize(list);
            for (int row = 0; row < size; row++) {
                Target target = listRow(list, row);
                if (target != null) targets.add(target);
            }
        }
        return targets;
    }

    static List<GuiTextField> textFields(GuiScreen screen) {
        List<GuiTextField> out = new ArrayList<GuiTextField>();
        for (Field field : fieldsOfType(screen.getClass(), GuiTextField.class, TEXT_FIELDS)) {
            try {
                Object value = field.get(screen);
                if (value instanceof GuiTextField) out.add((GuiTextField) value);
            } catch (IllegalAccessException ignored) {
                // fieldsOfType already made it accessible
            }
        }
        return out;
    }

    private static List<GuiSlot> lists(GuiScreen screen) {
        List<GuiSlot> out = new ArrayList<GuiSlot>();
        for (Field field : fieldsOfType(screen.getClass(), GuiSlot.class, LIST_FIELDS)) {
            try {
                Object value = field.get(screen);
                if (value instanceof GuiSlot) out.add((GuiSlot) value);
            } catch (IllegalAccessException ignored) {
                // fieldsOfType already made it accessible
            }
        }
        return out;
    }

    private static int listSize(GuiSlot list) {
        Object size = CleanroomReflect.call(list, GuiSlot.class, "func_148127_b", new Class<?>[0]); // getSize
        return size instanceof Integer ? (Integer) size : 0;
    }

    // vanilla's own row layout from GuiSlot.drawSelectionBox, null while the row is scrolled off
    private static Target listRow(GuiSlot list, int row) {
        int top = CleanroomReflect.getInt(list, GuiSlot.class, "field_148153_b", 0); // top
        int bottom = CleanroomReflect.getInt(list, GuiSlot.class, "field_148154_c", 0); // bottom
        int left = CleanroomReflect.getInt(list, GuiSlot.class, "field_148152_e", 0); // left
        int right = CleanroomReflect.getInt(list, GuiSlot.class, "field_148151_d", 0); // right
        int slotHeight = CleanroomReflect.getInt(list, GuiSlot.class, "field_148149_f", 0); // slotHeight
        int headerPadding = CleanroomReflect.getInt(list, GuiSlot.class, "field_148160_j", 0); // headerPadding
        float scrolled = CleanroomReflect.getFloat(list, GuiSlot.class, "field_148169_q", 0.0f); // amountScrolled
        if (slotHeight <= 0) return null;
        int rowTop = top + 4 - (int) scrolled + row * slotHeight + headerPadding;
        int rowBottom = rowTop + slotHeight - 4;
        if (rowTop < top || rowBottom > bottom) return null;
        return new Target((left + right) / 2.0, (rowTop + rowBottom) / 2.0, list, row);
    }

    private static List<Field> fieldsOfType(Class<?> type, Class<?> wanted, Map<Class<?>, List<Field>> cache) {
        List<Field> cached = cache.get(type);
        if (cached != null) return cached;
        List<Field> found = new ArrayList<Field>();
        for (Class<?> c = type; c != null && c != GuiScreen.class && c != Object.class; c = c.getSuperclass()) {
            for (Field field : c.getDeclaredFields()) {
                if (wanted.isAssignableFrom(field.getType()) && !java.lang.reflect.Modifier.isStatic(field.getModifiers())) {
                    try {
                        field.setAccessible(true);
                        found.add(field);
                    } catch (RuntimeException ignored) {
                        // a field the module system will not open is left out
                    }
                }
            }
        }
        cache.put(type, found);
        return found;
    }
}
