package banditvault.neoforgecontroller;

import banditvault.controllercore.GridNavigation;
import banditvault.neoforgecontroller.mixin.NeoForgeControllerRecipeBookAccessor;
import banditvault.neoforgecontroller.mixin.NeoForgeControllerRecipeBookPageAccessor;
import banditvault.neoforgecontroller.mixin.NeoForgeControllerRecipeOverlayAccessor;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.client.gui.ComponentPath;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.events.ContainerEventHandler;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.navigation.FocusNavigationEvent;
import net.minecraft.client.gui.navigation.ScreenDirection;
import net.minecraft.client.gui.navigation.ScreenRectangle;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeUpdateListener;
import net.minecraft.world.inventory.Slot;

final class NeoForgeMenuNavigation {
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
    private Screen screen;
    private Region region = Region.NONE;
    private Slot slot;
    private AbstractWidget recipeWidget;

    void reset(Screen nextScreen) {
        screen = nextScreen;
        region = Region.NONE;
        slot = null;
        recipeWidget = null;
    }


    Slot selectedSlot(Screen currentScreen) {
        return currentScreen == screen && region == Region.SLOT ? slot : null;
    }

    boolean usesNativeActivation(Screen currentScreen) {
        return currentScreen == screen && region == Region.NATIVE_WIDGET && deepestFocused(currentScreen) != null;
    }

    Position discover(Screen currentScreen, double fromX, double fromY) {
        if (screen != currentScreen) {
            reset(currentScreen);
        }

        if (currentScreen instanceof AbstractContainerScreen) {
            AbstractContainerScreen<?> container = (AbstractContainerScreen<?>) currentScreen;
            RecipeBookComponent recipeBook = recipeBook(currentScreen);
            OverlayRecipeComponent overlay = overlay(recipeBook);
            if (overlay != null && overlay.isVisible()) {
                return selectRecipeTarget(currentScreen, overlayTargets(overlay), fromX, fromY, Region.RECIPE_OVERLAY);
            }
            if (recipeBook != null && recipeBook.isVisible() && isNarrow(currentScreen)) {
                return selectRecipeTarget(currentScreen, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
            }

            List<SlotTarget> slots = slotTargets(container);
            if (recipeBook != null && recipeBook.isVisible() && fromX < container.getGuiLeft()) {
                Position recipe = selectRecipeTarget(currentScreen, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
                if (recipe != null) {
                    return recipe;
                }
            }
            SlotTarget target = nearestSlot(slots, fromX, fromY);
            if (target != null) {
                return selectSlot(currentScreen, target);
            }
            return discoverNative(currentScreen, true);
        }
        return discoverNative(currentScreen, true);
    }

    Position move(Screen currentScreen, GridNavigation.Direction direction, double fromX, double fromY) {
        if (screen != currentScreen || region == Region.NONE) {
            Position discovered = discover(currentScreen, fromX, fromY);
            if (discovered == null) {
                return null;
            }
        }

        RecipeBookComponent recipeBook = recipeBook(currentScreen);
        OverlayRecipeComponent overlay = overlay(recipeBook);
        if (overlay != null && overlay.isVisible() && region != Region.RECIPE_OVERLAY) {
            return selectRecipeTarget(currentScreen, overlayTargets(overlay), fromX, fromY, Region.RECIPE_OVERLAY);
        }
        if (region == Region.RECIPE_OVERLAY) {
            if (overlay == null || !overlay.isVisible()) {
                return discover(currentScreen, fromX, fromY);
            }
            return moveRecipe(currentScreen, overlayTargets(overlay), direction, Region.RECIPE_OVERLAY);
        }

        if (currentScreen instanceof AbstractContainerScreen) {
            AbstractContainerScreen<?> container = (AbstractContainerScreen<?>) currentScreen;
            if (recipeBook != null && recipeBook.isVisible() && isNarrow(currentScreen) && region != Region.RECIPE_BOOK) {
                return selectRecipeTarget(currentScreen, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
            }
            if (region == Region.SLOT) {
                Position moved = moveSlot(container, direction);
                if (moved != null) {
                    return moved;
                }
                if (recipeBook != null && recipeBook.isVisible() && direction == GridNavigation.Direction.LEFT) {
                    Position recipe = selectRecipeBoundary(currentScreen, recipeTargets(recipeBook), fromY, false, Region.RECIPE_BOOK);
                    if (recipe != null) {
                        return recipe;
                    }
                }
                return moveNative(currentScreen, direction, false);
            }
            if (region == Region.RECIPE_BOOK) {
                Position moved = moveRecipe(currentScreen, recipeTargets(recipeBook), direction, Region.RECIPE_BOOK);
                if (moved != null) {
                    return moved;
                }
                if (!isNarrow(currentScreen) && direction == GridNavigation.Direction.RIGHT) {
                    SlotTarget boundary = nearestSlotOnBoundary(slotTargets(container), fromY, true);
                    if (boundary != null) {
                        return selectSlot(currentScreen, boundary);
                    }
                }
                return null;
            }
            if (region == Region.NATIVE_WIDGET) {
                if (recipeBook != null && recipeBook.isVisible() && !isNarrow(currentScreen) &&
                    direction == GridNavigation.Direction.LEFT) {
                    Position recipe = selectRecipeBoundary(
                        currentScreen,
                        recipeTargets(recipeBook),
                        fromY,
                        false,
                        Region.RECIPE_BOOK);
                    if (recipe != null) {
                        return recipe;
                    }
                }
                Position nativePosition = moveNative(currentScreen, direction, false);
                if (nativePosition != null) {
                    return nativePosition;
                }
                SlotTarget boundary = nearestSlotInDirection(slotTargets(container), fromX, fromY, direction);
                return boundary == null ? null : selectSlot(currentScreen, boundary);
            }
        }
        return moveNative(currentScreen, direction, true);
    }

    Position synchronize(Screen currentScreen, double fromX, double fromY) {
        if (screen != currentScreen || region == Region.NONE) {
            return discover(currentScreen, fromX, fromY);
        }
        RecipeBookComponent recipeBook = recipeBook(currentScreen);
        OverlayRecipeComponent overlay = overlay(recipeBook);
        if (overlay != null && overlay.isVisible()) {
            List<AbstractWidget> targets = overlayTargets(overlay);
            if (region != Region.RECIPE_OVERLAY || !targets.contains(recipeWidget)) {
                return selectRecipeTarget(currentScreen, targets, fromX, fromY, Region.RECIPE_OVERLAY);
            }
            return null;
        }
        if (region == Region.RECIPE_OVERLAY ||
            (region == Region.RECIPE_BOOK && (recipeBook == null || !recipeBook.isVisible()))) {
            region = Region.NONE;
            return discover(currentScreen, fromX, fromY);
        }
        if (recipeBook != null && recipeBook.isVisible() && (isNarrow(currentScreen) || region == Region.RECIPE_BOOK)) {
            List<AbstractWidget> targets = recipeTargets(recipeBook);
            if (region != Region.RECIPE_BOOK || !targets.contains(recipeWidget)) {
                return selectRecipeTarget(currentScreen, targets, fromX, fromY, Region.RECIPE_BOOK);
            }
        }
        if (region == Region.SLOT && currentScreen instanceof AbstractContainerScreen &&
            !containsSlot(slotTargets((AbstractContainerScreen<?>) currentScreen), slot)) {
            region = Region.NONE;
            return discover(currentScreen, fromX, fromY);
        }
        return null;
    }

    boolean handleBack(Screen currentScreen) {
        RecipeBookComponent recipeBook = recipeBook(currentScreen);
        OverlayRecipeComponent overlay = overlay(recipeBook);
        if (overlay != null && overlay.isVisible()) {
            overlay.setVisible(false);
            region = Region.NONE;
            recipeWidget = null;
            return true;
        }
        if (recipeBook != null && recipeBook.isVisible() && isNarrow(currentScreen)) {
            recipeBook.toggleVisibility();
            region = Region.NONE;
            recipeWidget = null;
            return true;
        }
        return false;
    }

    private Position moveSlot(AbstractContainerScreen<?> container, GridNavigation.Direction direction) {
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

    private Position moveRecipe(Screen currentScreen, List<AbstractWidget> targets, GridNavigation.Direction direction, Region targetRegion) {
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
        currentScreen.clearFocus();
        return center(recipeWidget.getRectangle());
    }

    private Position moveNative(Screen currentScreen, GridNavigation.Direction direction, boolean allowSameTarget) {
        GuiEventListener before = deepestFocused(currentScreen);
        applyNavigation(currentScreen, new FocusNavigationEvent.ArrowNavigation(nativeDirection(direction)), false);
        GuiEventListener focused = deepestFocused(currentScreen);
        Position position = focusedPosition(focused);
        if (position != null && (allowSameTarget || focused != before)) {
            region = Region.NATIVE_WIDGET;
            slot = null;
            recipeWidget = null;
            return position;
        }
        if (allowSameTarget && position == null) {
            logUnavailableOnce(currentScreen, "native focus returned no target for " + direction);
        }
        return null;
    }

    private Position discoverNative(Screen currentScreen, boolean logFailure) {
        Position focused = focusedPosition(deepestFocused(currentScreen));
        if (focused == null) {
            applyNavigation(currentScreen, new FocusNavigationEvent.TabNavigation(true), true);
            focused = focusedPosition(deepestFocused(currentScreen));
        }
        if (focused != null) {
            region = Region.NATIVE_WIDGET;
            slot = null;
            recipeWidget = null;
            return focused;
        }
        if (logFailure) {
            logDiscoveryFailureOnce(currentScreen, "native-widget");
        }
        return null;
    }

    private Position selectRecipeTarget(Screen currentScreen, List<AbstractWidget> targets, double x, double y, Region targetRegion) {
        AbstractWidget nearest = nearestWidget(targets, x, y);
        if (nearest == null) {
            logDiscoveryFailureOnce(currentScreen, targetRegion.name().toLowerCase());
            return null;
        }
        region = targetRegion;
        recipeWidget = nearest;
        slot = null;
        currentScreen.clearFocus();
        return center(nearest.getRectangle());
    }

    private Position selectRecipeBoundary(Screen currentScreen, List<AbstractWidget> targets, double y, boolean leftEdge, Region targetRegion) {
        AbstractWidget best = null;
        int edge = leftEdge ? Integer.MAX_VALUE : Integer.MIN_VALUE;
        int rowDistance = Integer.MAX_VALUE;
        for (AbstractWidget target : targets) {
            int targetEdge = leftEdge ? target.getX() : target.getRight();
            int targetRowDistance = Math.abs(target.getY() + target.getHeight() / 2 - (int) y);
            boolean betterEdge = leftEdge ? targetEdge < edge : targetEdge > edge;
            if (betterEdge || (targetEdge == edge && targetRowDistance < rowDistance)) {
                best = target;
                edge = targetEdge;
                rowDistance = targetRowDistance;
            }
        }
        if (best == null) {
            logDiscoveryFailureOnce(currentScreen, targetRegion.name().toLowerCase());
            return null;
        }
        region = targetRegion;
        recipeWidget = best;
        slot = null;
        currentScreen.clearFocus();
        return center(best.getRectangle());
    }

    private Position selectSlot(Screen currentScreen, SlotTarget target) {
        region = Region.SLOT;
        slot = target.slot;
        recipeWidget = null;
        currentScreen.clearFocus();
        return new Position(target.x, target.y);
    }

    private static List<SlotTarget> slotTargets(AbstractContainerScreen<?> screen) {
        List<SlotTarget> targets = new ArrayList<SlotTarget>();
        int left = screen.getGuiLeft();
        int top = screen.getGuiTop();
        for (Slot candidate : screen.getMenu().slots) {
            if (candidate.isActive() && candidate.isHighlightable()) {
                targets.add(new SlotTarget(candidate, left + candidate.x + 8, top + candidate.y + 8));
            }
        }
        return targets;
    }

    private List<AbstractWidget> recipeTargets(RecipeBookComponent recipeBook) {
        List<AbstractWidget> targets = new ArrayList<AbstractWidget>();
        if (recipeBook == null || !recipeBook.isVisible()) {
            return targets;
        }
        try {
            NeoForgeControllerRecipeBookAccessor accessor = (NeoForgeControllerRecipeBookAccessor) recipeBook;
            addVisible(targets, accessor.banditvault$searchBox());
            addVisible(targets, accessor.banditvault$filterButton());
            for (AbstractWidget tab : accessor.banditvault$tabButtons()) {
                addVisible(targets, tab);
            }
            NeoForgeControllerRecipeBookPageAccessor page = (NeoForgeControllerRecipeBookPageAccessor) accessor.banditvault$recipeBookPage();
            for (AbstractWidget button : page.banditvault$buttons()) {
                addVisible(targets, button);
            }
            addVisible(targets, page.banditvault$backButton());
            addVisible(targets, page.banditvault$forwardButton());
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-book target access failed: " + error.getClass().getSimpleName());
        }
        return targets;
    }

    private List<AbstractWidget> overlayTargets(OverlayRecipeComponent overlay) {
        List<AbstractWidget> targets = new ArrayList<AbstractWidget>();
        if (overlay == null || !overlay.isVisible()) {
            return targets;
        }
        try {
            for (AbstractWidget button : ((NeoForgeControllerRecipeOverlayAccessor) overlay).banditvault$recipeButtons()) {
                addVisible(targets, button);
            }
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-overlay target access failed: " + error.getClass().getSimpleName());
        }
        return targets;
    }

    private OverlayRecipeComponent overlay(RecipeBookComponent recipeBook) {
        if (recipeBook == null) {
            return null;
        }
        try {
            Object page = ((NeoForgeControllerRecipeBookAccessor) recipeBook).banditvault$recipeBookPage();
            return ((NeoForgeControllerRecipeBookPageAccessor) page).banditvault$overlay();
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-overlay hook unavailable: " + error.getClass().getSimpleName());
            return null;
        }
    }

    private static RecipeBookComponent recipeBook(Screen currentScreen) {
        return NeoForgeVersionApi.recipeBook(currentScreen);
    }

    private static boolean isNarrow(Screen currentScreen) {
        return currentScreen.width < 379;
    }

    static GuiEventListener deepestFocused(ContainerEventHandler container) {
        GuiEventListener focused = container.getFocused();
        while (focused instanceof ContainerEventHandler) {
            GuiEventListener child = ((ContainerEventHandler) focused).getFocused();
            if (child == null || child == focused) {
                break;
            }
            focused = child;
        }
        return focused;
    }

    private static Position focusedPosition(GuiEventListener focused) {
        if (focused == null) {
            return null;
        }
        ScreenRectangle rectangle = focused.getRectangle();
        return rectangle.width() <= 0 || rectangle.height() <= 0 ? null : center(rectangle);
    }

    private static Position center(ScreenRectangle rectangle) {
        return new Position(rectangle.left() + rectangle.width() / 2.0, rectangle.top() + rectangle.height() / 2.0);
    }

    private static ScreenDirection nativeDirection(GridNavigation.Direction direction) {
        switch (direction) {
            case UP: return ScreenDirection.UP;
            case DOWN: return ScreenDirection.DOWN;
            case LEFT: return ScreenDirection.LEFT;
            case RIGHT: return ScreenDirection.RIGHT;
            default: throw new IllegalArgumentException("Unknown navigation direction " + direction);
        }
    }

    private static void applyNavigation(Screen screen, FocusNavigationEvent navigation, boolean retryAfterClearingFocus) {
        ComponentPath path = screen.nextFocusPath(navigation);
        if (path == null && retryAfterClearingFocus) {
            screen.clearFocus();
            path = screen.nextFocusPath(navigation);
        }
        if (path != null) {
            screen.clearFocus();
            path.applyFocus(true);
        }
    }

    private static List<GridNavigation.Point> widgetPoints(List<AbstractWidget> widgets) {
        List<GridNavigation.Point> points = new ArrayList<GridNavigation.Point>(widgets.size());
        for (int i = 0; i < widgets.size(); i++) {
            AbstractWidget widget = widgets.get(i);
            points.add(new GridNavigation.Point(i, widget.getX() + widget.getWidth() / 2, widget.getY() + widget.getHeight() / 2));
        }
        return points;
    }

    private static void addVisible(List<AbstractWidget> targets, AbstractWidget widget) {
        if (widget != null && widget.visible && widget.active && widget.getWidth() > 0 && widget.getHeight() > 0) {
            targets.add(widget);
        }
    }

    private static AbstractWidget nearestWidget(List<AbstractWidget> targets, double x, double y) {
        AbstractWidget best = null;
        double bestDistance = Double.MAX_VALUE;
        for (AbstractWidget target : targets) {
            double dx = target.getX() + target.getWidth() / 2.0 - x;
            double dy = target.getY() + target.getHeight() / 2.0 - y;
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

    private static boolean containsSlot(List<SlotTarget> targets, Slot slot) {
        for (SlotTarget target : targets) {
            if (target.slot == slot) {
                return true;
            }
        }
        return false;
    }

    private void logUnavailableOnce(Screen currentScreen, String detail) {
        String screenName = currentScreen == null ? "unknown" : currentScreen.getClass().getName();
        String key = screenName + ":" + detail;
        if (loggedUnavailableHooks.add(key)) {
            NeoForgeControllerLog.log("Snap navigation hook unavailable screen=" + screenName + " detail=" + detail);
        }
    }

    private void logDiscoveryFailureOnce(Screen currentScreen, String targetRegion) {
        String screenName = currentScreen == null ? "unknown" : currentScreen.getClass().getName();
        String key = screenName + ":" + targetRegion;
        if (loggedDiscoveryFailures.add(key)) {
            NeoForgeControllerLog.log("Snap target discovery failed screen=" + screenName + " region=" + targetRegion);
        }
    }

    private static final class SlotTarget {
        final Slot slot;
        final int x;
        final int y;

        SlotTarget(Slot slot, int x, int y) {
            this.slot = slot;
            this.x = x;
            this.y = y;
        }
    }
}
