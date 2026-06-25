package banditvault.fabriccontroller;

import banditvault.controllercore.GridNavigation;
import banditvault.fabriccontroller.mixin.BanditControllerRecipeBookAccessor;
import banditvault.fabriccontroller.mixin.BanditControllerRecipeBookPageAccessor;
import banditvault.fabriccontroller.mixin.BanditControllerRecipeOverlayAccessor;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.class_339;
import net.minecraft.class_4069;
import net.minecraft.class_364;
import net.minecraft.class_8030;
import net.minecraft.class_437;
import net.minecraft.class_465;
import net.minecraft.class_508;
import net.minecraft.class_507;
import net.minecraft.class_1735;
import org.lwjgl.glfw.GLFW;

final class Fabric12111MenuNavigation {
    enum Region {
        NONE,
        NATIVE_WIDGET,
        SLOT,
        RECIPE_BOOK,
        RECIPE_OVERLAY
    }

    static final class Position {
        final double x;
        final double y;

        Position(double x, double y) {
            this.x = x;
            this.y = y;
        }
    }

    private final Set<String> loggedUnavailableHooks = new HashSet<String>();
    private final Set<String> loggedDiscoveryFailures = new HashSet<String>();
    private class_437 screen;
    private Region region = Region.NONE;
    private class_1735 slot;
    private class_339 recipeWidget;

    void reset(class_437 nextclass_437) {
        screen = nextclass_437;
        region = Region.NONE;
        slot = null;
        recipeWidget = null;
    }

    Region region() {
        return region;
    }

    class_1735 selectedSlot(class_437 currentclass_437) {
        return currentclass_437 == screen && region == Region.SLOT ? slot : null;
    }

    boolean usesNativeActivation(class_437 currentclass_437) {
        return currentclass_437 == screen && region == Region.NATIVE_WIDGET && deepestFocused(currentclass_437) != null;
    }

    Position discover(class_437 currentclass_437, double fromX, double fromY) {
        if (screen != currentclass_437) {
            reset(currentclass_437);
        }

        if (currentclass_437 instanceof class_465) {
            class_465<?> container = (class_465<?>) currentclass_437;
            class_507 recipeBook = recipeBook(currentclass_437);
            class_508 overlay = overlay(recipeBook);
            if (overlay != null && overlay.method_2616()) {
                return selectRecipeTarget(currentclass_437, overlayTargets(overlay), fromX, fromY, Region.RECIPE_OVERLAY);
            }
            if (recipeBook != null && recipeBook.method_2605() && isNarrow(currentclass_437)) {
                return selectRecipeTarget(currentclass_437, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
            }

            List<SlotTarget> slots = slotTargets(container);
            if (recipeBook != null && recipeBook.method_2605() && fromX < ((banditvault.fabriccontroller.mixin.BanditControllerContainerAccessor) container).banditvault$leftPos()) {
                Position recipe = selectRecipeTarget(currentclass_437, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
                if (recipe != null) {
                    return recipe;
                }
            }
            SlotTarget target = nearestSlot(slots, fromX, fromY);
            if (target != null) {
                return selectSlot(currentclass_437, target);
            }
            return discoverNative(currentclass_437, true);
        }
        return discoverNative(currentclass_437, true);
    }

    Position move(class_437 currentclass_437, GridNavigation.Direction direction, double fromX, double fromY) {
        if (screen != currentclass_437 || region == Region.NONE) {
            Position discovered = discover(currentclass_437, fromX, fromY);
            if (discovered == null) {
                return null;
            }
        }

        class_507 recipeBook = recipeBook(currentclass_437);
        class_508 overlay = overlay(recipeBook);
        if (overlay != null && overlay.method_2616() && region != Region.RECIPE_OVERLAY) {
            return selectRecipeTarget(currentclass_437, overlayTargets(overlay), fromX, fromY, Region.RECIPE_OVERLAY);
        }
        if (region == Region.RECIPE_OVERLAY) {
            if (overlay == null || !overlay.method_2616()) {
                return discover(currentclass_437, fromX, fromY);
            }
            return moveRecipe(currentclass_437, overlayTargets(overlay), direction, Region.RECIPE_OVERLAY);
        }

        if (currentclass_437 instanceof class_465) {
            class_465<?> container = (class_465<?>) currentclass_437;
            if (recipeBook != null && recipeBook.method_2605() && isNarrow(currentclass_437) && region != Region.RECIPE_BOOK) {
                return selectRecipeTarget(currentclass_437, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
            }
            if (region == Region.SLOT) {
                Position moved = moveSlot(container, direction);
                if (moved != null) {
                    return moved;
                }
                if (recipeBook != null && recipeBook.method_2605() && direction == GridNavigation.Direction.LEFT) {
                    Position recipe = selectRecipeBoundary(currentclass_437, recipeTargets(recipeBook), fromY, false, Region.RECIPE_BOOK);
                    if (recipe != null) {
                        return recipe;
                    }
                }
                return moveNative(currentclass_437, direction, false);
            }
            if (region == Region.RECIPE_BOOK) {
                Position moved = moveRecipe(currentclass_437, recipeTargets(recipeBook), direction, Region.RECIPE_BOOK);
                if (moved != null) {
                    return moved;
                }
                if (!isNarrow(currentclass_437) && direction == GridNavigation.Direction.RIGHT) {
                    SlotTarget boundary = nearestSlotOnBoundary(slotTargets(container), fromY, true);
                    if (boundary != null) {
                        return selectSlot(currentclass_437, boundary);
                    }
                }
                return null;
            }
            if (region == Region.NATIVE_WIDGET) {
                if (recipeBook != null && recipeBook.method_2605() && !isNarrow(currentclass_437) &&
                    direction == GridNavigation.Direction.LEFT) {
                    Position recipe = selectRecipeBoundary(
                        currentclass_437,
                        recipeTargets(recipeBook),
                        fromY,
                        false,
                        Region.RECIPE_BOOK);
                    if (recipe != null) {
                        return recipe;
                    }
                }
                Position nativePosition = moveNative(currentclass_437, direction, false);
                if (nativePosition != null) {
                    return nativePosition;
                }
                SlotTarget boundary = nearestSlotInDirection(slotTargets(container), fromX, fromY, direction);
                return boundary == null ? null : selectSlot(currentclass_437, boundary);
            }
        }
        return moveNative(currentclass_437, direction, true);
    }

    Position synchronize(class_437 currentclass_437, double fromX, double fromY) {
        if (screen != currentclass_437 || region == Region.NONE) {
            return discover(currentclass_437, fromX, fromY);
        }
        class_507 recipeBook = recipeBook(currentclass_437);
        class_508 overlay = overlay(recipeBook);
        if (overlay != null && overlay.method_2616()) {
            List<class_339> targets = overlayTargets(overlay);
            if (region != Region.RECIPE_OVERLAY || !targets.contains(recipeWidget)) {
                return selectRecipeTarget(currentclass_437, targets, fromX, fromY, Region.RECIPE_OVERLAY);
            }
            return null;
        }
        if (region == Region.RECIPE_OVERLAY ||
            (region == Region.RECIPE_BOOK && (recipeBook == null || !recipeBook.method_2605()))) {
            region = Region.NONE;
            return discover(currentclass_437, fromX, fromY);
        }
        if (recipeBook != null && recipeBook.method_2605() && isNarrow(currentclass_437)) {
            List<class_339> targets = recipeTargets(recipeBook);
            if (region != Region.RECIPE_BOOK || !targets.contains(recipeWidget)) {
                return selectRecipeTarget(currentclass_437, targets, fromX, fromY, Region.RECIPE_BOOK);
            }
        }
        if (region == Region.SLOT && currentclass_437 instanceof class_465 &&
            !containsSlot(slotTargets((class_465<?>) currentclass_437), slot)) {
            region = Region.NONE;
            return discover(currentclass_437, fromX, fromY);
        }
        return null;
    }

    boolean handleBack(class_437 currentclass_437) {
        class_507 recipeBook = recipeBook(currentclass_437);
        class_508 overlay = overlay(recipeBook);
        if (overlay != null && overlay.method_2616()) {
            overlay.method_2613(false);
            region = Region.NONE;
            recipeWidget = null;
            return true;
        }
        if (recipeBook != null && recipeBook.method_2605() && isNarrow(currentclass_437)) {
            recipeBook.method_2591();
            region = Region.NONE;
            recipeWidget = null;
            return true;
        }
        return false;
    }

    private Position moveSlot(class_465<?> container, GridNavigation.Direction direction) {
        List<SlotTarget> targets = slotTargets(container);
        if (slot == null || !containsSlot(targets, slot)) {
            return null;
        }
        List<GridNavigation.Point> points = new ArrayList<GridNavigation.Point>(targets.size());
        int currentId = -1;
        for (int i = 0; i < targets.size(); i++) {
            SlotTarget target = targets.get(i);
            points.add(new GridNavigation.Point(i, target.x, target.y));
            if (target.slot == slot) {
                currentId = i;
            }
        }
        int next = GridNavigation.next(points, currentId, direction);
        return next < 0 ? null : selectSlot(container, targets.get(next));
    }

    private Position moveRecipe(class_437 currentclass_437, List<class_339> targets, GridNavigation.Direction direction, Region targetRegion) {
        int currentId = targets.indexOf(recipeWidget);
        if (currentId < 0) {
            return null;
        }
        List<GridNavigation.Point> points = widgetPoints(targets);
        int next = GridNavigation.nextLoose(points, currentId, direction);
        if (next < 0) {
            return null;
        }
        recipeWidget = targets.get(next);
        region = targetRegion;
        clearFocus(currentclass_437);
        return center(recipeWidget);
    }

    private Position moveNative(class_437 currentclass_437, GridNavigation.Direction direction, boolean allowSameTarget) {
        class_364 before = deepestFocused(currentclass_437);
        FabricScreenApi.keyPressed(currentclass_437, key(direction), 0, 0);
        class_364 focused = deepestFocused(currentclass_437);
        Position position = focusedPosition(focused);
        if (position != null && (allowSameTarget || focused != before)) {
            region = Region.NATIVE_WIDGET;
            slot = null;
            recipeWidget = null;
            return position;
        }
        if (allowSameTarget && position == null) {
            logUnavailableOnce(currentclass_437, "native focus returned no target for " + direction);
        }
        return null;
    }

    private Position discoverNative(class_437 currentclass_437, boolean logFailure) {
        Position focused = focusedPosition(deepestFocused(currentclass_437));
        if (focused == null) {
            FabricScreenApi.keyPressed(currentclass_437, GLFW.GLFW_KEY_TAB, 0, 0);
            focused = focusedPosition(deepestFocused(currentclass_437));
        }
        if (focused != null) {
            region = Region.NATIVE_WIDGET;
            slot = null;
            recipeWidget = null;
            return focused;
        }
        if (logFailure) {
            logDiscoveryFailureOnce(currentclass_437, "native-widget");
        }
        return null;
    }

    private Position selectRecipeTarget(class_437 currentclass_437, List<class_339> targets, double x, double y, Region targetRegion) {
        class_339 nearest = nearestWidget(targets, x, y);
        if (nearest == null) {
            logDiscoveryFailureOnce(currentclass_437, targetRegion.name().toLowerCase());
            return null;
        }
        region = targetRegion;
        recipeWidget = nearest;
        slot = null;
        clearFocus(currentclass_437);
        return center(nearest);
    }

    private Position selectRecipeBoundary(class_437 currentclass_437, List<class_339> targets, double y, boolean leftEdge, Region targetRegion) {
        class_339 best = null;
        int edge = leftEdge ? Integer.MAX_VALUE : Integer.MIN_VALUE;
        int rowDistance = Integer.MAX_VALUE;
        for (class_339 target : targets) {
            int targetEdge = leftEdge ? target.method_46426() : target.method_46426() + target.method_25368();
            int targetRowDistance = Math.abs(target.method_46427() + target.method_25364() / 2 - (int) y);
            boolean betterEdge = leftEdge ? targetEdge < edge : targetEdge > edge;
            if (betterEdge || (targetEdge == edge && targetRowDistance < rowDistance)) {
                best = target;
                edge = targetEdge;
                rowDistance = targetRowDistance;
            }
        }
        if (best == null) {
            logDiscoveryFailureOnce(currentclass_437, targetRegion.name().toLowerCase());
            return null;
        }
        region = targetRegion;
        recipeWidget = best;
        slot = null;
        clearFocus(currentclass_437);
        return center(best);
    }

    private Position selectSlot(class_437 currentclass_437, SlotTarget target) {
        region = Region.SLOT;
        slot = target.slot;
        recipeWidget = null;
        clearFocus(currentclass_437);
        return new Position(target.x, target.y);
    }

    private static List<SlotTarget> slotTargets(class_465<?> screen) {
        List<SlotTarget> targets = new ArrayList<SlotTarget>();
        int left = ((banditvault.fabriccontroller.mixin.BanditControllerContainerAccessor) screen).banditvault$leftPos();
        int top = ((banditvault.fabriccontroller.mixin.BanditControllerContainerAccessor) screen).banditvault$topPos();
        for (class_1735 candidate : screen.method_17577().field_7761) {
            if (candidate.method_7682() && candidate.method_51306()) {
                targets.add(new SlotTarget(candidate, left + candidate.field_7873 + 8, top + candidate.field_7872 + 8));
            }
        }
        return targets;
    }

    private List<class_339> recipeTargets(class_507 recipeBook) {
        List<class_339> targets = new ArrayList<class_339>();
        if (recipeBook == null || !recipeBook.method_2605()) {
            return targets;
        }
        try {
            BanditControllerRecipeBookAccessor accessor = (BanditControllerRecipeBookAccessor) recipeBook;
            addVisible(targets, accessor.banditvault$searchBox());
            for (class_339 tab : accessor.banditvault$tabButtons()) {
                addVisible(targets, tab);
            }
            BanditControllerRecipeBookPageAccessor page = (BanditControllerRecipeBookPageAccessor) accessor.banditvault$recipeBookPage();
            for (class_339 button : page.banditvault$buttons()) {
                addVisible(targets, button);
            }
            addVisible(targets, page.banditvault$backButton());
            addVisible(targets, page.banditvault$forwardButton());
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-book target access failed: " + error.getClass().getSimpleName());
        }
        return targets;
    }

    private List<class_339> overlayTargets(class_508 overlay) {
        List<class_339> targets = new ArrayList<class_339>();
        if (overlay == null || !overlay.method_2616()) {
            return targets;
        }
        try {
            for (class_339 button : ((BanditControllerRecipeOverlayAccessor) overlay).banditvault$recipeButtons()) {
                addVisible(targets, button);
            }
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-overlay target access failed: " + error.getClass().getSimpleName());
        }
        return targets;
    }

    private class_508 overlay(class_507 recipeBook) {
        if (recipeBook == null) {
            return null;
        }
        try {
            Object page = ((BanditControllerRecipeBookAccessor) recipeBook).banditvault$recipeBookPage();
            return ((BanditControllerRecipeBookPageAccessor) page).banditvault$overlay();
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-overlay hook unavailable: " + error.getClass().getSimpleName());
            return null;
        }
    }

    private static class_507 recipeBook(class_437 currentclass_437) {
        return currentclass_437 instanceof net.minecraft.class_518
            ? ((net.minecraft.class_518) currentclass_437).method_2659()
            : null;
    }

    private static boolean isNarrow(class_437 currentclass_437) {
        return currentclass_437.field_22789 < 379;
    }

    private static class_364 deepestFocused(class_4069 container) {
        class_364 focused = container.method_25399();
        while (focused instanceof class_4069) {
            class_364 child = ((class_4069) focused).method_25399();
            if (child == null || child == focused) {
                break;
            }
            focused = child;
        }
        return focused;
    }

    static void clearFocus(class_437 screen) {
        ((class_4069) screen).method_25395(null);
    }

    private static Position focusedPosition(class_364 focused) {
        return focused instanceof class_339 ? center((class_339) focused) : null;
    }

    private static Position center(class_339 widget) {
        return new Position(widget.method_46426() + widget.method_25368() / 2.0, widget.method_46427() + widget.method_25364() / 2.0);
    }

    private static int key(GridNavigation.Direction direction) {
        switch (direction) {
            case UP: return GLFW.GLFW_KEY_UP;
            case DOWN: return GLFW.GLFW_KEY_DOWN;
            case LEFT: return GLFW.GLFW_KEY_LEFT;
            case RIGHT: return GLFW.GLFW_KEY_RIGHT;
            default: return GLFW.GLFW_KEY_UNKNOWN;
        }
    }

    private static List<GridNavigation.Point> widgetPoints(List<class_339> widgets) {
        List<GridNavigation.Point> points = new ArrayList<GridNavigation.Point>(widgets.size());
        for (int i = 0; i < widgets.size(); i++) {
            class_339 widget = widgets.get(i);
            points.add(new GridNavigation.Point(i, widget.method_46426() + widget.method_25368() / 2, widget.method_46427() + widget.method_25364() / 2));
        }
        return points;
    }

    private static void addVisible(List<class_339> targets, class_339 widget) {
        if (widget != null && widget.field_22764 && widget.field_22763 && widget.method_25368() > 0 && widget.method_25364() > 0) {
            targets.add(widget);
        }
    }

    private static class_339 nearestWidget(List<class_339> targets, double x, double y) {
        class_339 best = null;
        double bestDistance = Double.MAX_VALUE;
        for (class_339 target : targets) {
            double dx = target.method_46426() + target.method_25368() / 2.0 - x;
            double dy = target.method_46427() + target.method_25364() / 2.0 - y;
            double distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                best = target;
                bestDistance = distance;
            }
        }
        return best;
    }

    private static SlotTarget nearestSlot(List<SlotTarget> targets, double x, double y) {
        SlotTarget best = null;
        double bestDistance = Double.MAX_VALUE;
        for (SlotTarget target : targets) {
            double dx = target.x - x;
            double dy = target.y - y;
            double distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                best = target;
                bestDistance = distance;
            }
        }
        return best;
    }

    private static SlotTarget nearestSlotOnBoundary(List<SlotTarget> targets, double y, boolean leftEdge) {
        SlotTarget best = null;
        int edge = leftEdge ? Integer.MAX_VALUE : Integer.MIN_VALUE;
        double rowDistance = Double.MAX_VALUE;
        for (SlotTarget target : targets) {
            boolean betterEdge = leftEdge ? target.x < edge : target.x > edge;
            double targetRowDistance = Math.abs(target.y - y);
            if (betterEdge || (target.x == edge && targetRowDistance < rowDistance)) {
                best = target;
                edge = target.x;
                rowDistance = targetRowDistance;
            }
        }
        return best;
    }

    private static SlotTarget nearestSlotInDirection(List<SlotTarget> targets, double x, double y, GridNavigation.Direction direction) {
        SlotTarget best = null;
        double bestDistance = Double.MAX_VALUE;
        for (SlotTarget target : targets) {
            boolean ahead;
            switch (direction) {
                case UP: ahead = target.y < y; break;
                case DOWN: ahead = target.y > y; break;
                case LEFT: ahead = target.x < x; break;
                case RIGHT: ahead = target.x > x; break;
                default: ahead = false;
            }
            if (!ahead) {
                continue;
            }
            double dx = target.x - x;
            double dy = target.y - y;
            double distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                best = target;
                bestDistance = distance;
            }
        }
        return best;
    }

    private static boolean containsSlot(List<SlotTarget> targets, class_1735 slot) {
        for (SlotTarget target : targets) {
            if (target.slot == slot) {
                return true;
            }
        }
        return false;
    }

    private void logUnavailableOnce(class_437 currentclass_437, String detail) {
        String screenName = currentclass_437 == null ? "unknown" : currentclass_437.getClass().getName();
        String key = screenName + ":" + detail;
        if (loggedUnavailableHooks.add(key)) {
            FabricControllerLog.log("Snap navigation hook unavailable screen=" + screenName + " detail=" + detail);
        }
    }

    private void logDiscoveryFailureOnce(class_437 currentclass_437, String targetRegion) {
        String screenName = currentclass_437 == null ? "unknown" : currentclass_437.getClass().getName();
        String key = screenName + ":" + targetRegion;
        if (loggedDiscoveryFailures.add(key)) {
            FabricControllerLog.log("Snap target discovery failed screen=" + screenName + " region=" + targetRegion);
        }
    }

    private static final class SlotTarget {
        final class_1735 slot;
        final int x;
        final int y;

        SlotTarget(class_1735 slot, int x, int y) {
            this.slot = slot;
            this.x = x;
            this.y = y;
        }
    }
}
