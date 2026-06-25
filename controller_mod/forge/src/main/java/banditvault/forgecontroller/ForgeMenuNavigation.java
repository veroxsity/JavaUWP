package banditvault.forgecontroller;

import banditvault.controllercore.GridNavigation;
import banditvault.forgecontroller.mixin.ForgeControllerContainerAccessor;
import banditvault.forgecontroller.mixin.ForgeControllerRecipeBookAccessor;
import banditvault.forgecontroller.mixin.ForgeControllerRecipeBookPageAccessor;
import banditvault.forgecontroller.mixin.ForgeControllerRecipeOverlayAccessor;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.events.ContainerEventHandler;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeUpdateListener;
import net.minecraft.world.inventory.Slot;
import org.lwjgl.glfw.GLFW;

final class ForgeMenuNavigation {
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
            if (overlay != null && overlay.m_100212_()) {
                return selectRecipeTarget(currentScreen, overlayTargets(overlay), fromX, fromY, Region.RECIPE_OVERLAY);
            }
            if (recipeBook != null && recipeBook.m_100385_() && isNarrow(currentScreen)) {
                return selectRecipeTarget(currentScreen, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
            }

            List<SlotTarget> slots = slotTargets(container);
            if (recipeBook != null && recipeBook.m_100385_() && fromX < ((ForgeControllerContainerAccessor) container).banditvault$leftPos()) {
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
        if (overlay != null && overlay.m_100212_() && region != Region.RECIPE_OVERLAY) {
            return selectRecipeTarget(currentScreen, overlayTargets(overlay), fromX, fromY, Region.RECIPE_OVERLAY);
        }
        if (region == Region.RECIPE_OVERLAY) {
            if (overlay == null || !overlay.m_100212_()) {
                return discover(currentScreen, fromX, fromY);
            }
            return moveRecipe(currentScreen, overlayTargets(overlay), direction, Region.RECIPE_OVERLAY);
        }

        if (currentScreen instanceof AbstractContainerScreen) {
            AbstractContainerScreen<?> container = (AbstractContainerScreen<?>) currentScreen;
            if (recipeBook != null && recipeBook.m_100385_() && isNarrow(currentScreen) && region != Region.RECIPE_BOOK) {
                return selectRecipeTarget(currentScreen, recipeTargets(recipeBook), fromX, fromY, Region.RECIPE_BOOK);
            }
            if (region == Region.SLOT) {
                Position moved = moveSlot(container, direction);
                if (moved != null) {
                    return moved;
                }
                if (recipeBook != null && recipeBook.m_100385_() && direction == GridNavigation.Direction.LEFT) {
                    Position recipe = selectRecipeBoundary(currentScreen, recipeTargets(recipeBook), fromY, false, Region.RECIPE_BOOK);
                    if (recipe != null) {
                        return recipe;
                    }
                }
                return moveNative(currentScreen, direction, false);
            }
            if (region == Region.RECIPE_BOOK) {
                if (!isNarrow(currentScreen) && direction == GridNavigation.Direction.RIGHT) {
                    SlotTarget boundary = nearestSlotOnBoundary(slotTargets(container), fromY, true);
                    if (boundary != null) {
                        return selectSlot(currentScreen, boundary);
                    }
                }
                Position moved = moveRecipe(currentScreen, recipeTargets(recipeBook), direction, Region.RECIPE_BOOK);
                if (moved != null) {
                    return moved;
                }
                return null;
            }
            if (region == Region.NATIVE_WIDGET) {
                if (recipeBook != null && recipeBook.m_100385_() && !isNarrow(currentScreen) &&
                    direction == GridNavigation.Direction.LEFT) {
                    Position recipe = selectRecipeBoundary(currentScreen, recipeTargets(recipeBook), fromY, false, Region.RECIPE_BOOK);
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
        if (overlay != null && overlay.m_100212_()) {
            List<AbstractWidget> targets = overlayTargets(overlay);
            if (region != Region.RECIPE_OVERLAY || !targets.contains(recipeWidget)) {
                return selectRecipeTarget(currentScreen, targets, fromX, fromY, Region.RECIPE_OVERLAY);
            }
            return null;
        }
        if (region == Region.RECIPE_OVERLAY ||
            (region == Region.RECIPE_BOOK && (recipeBook == null || !recipeBook.m_100385_()))) {
            region = Region.NONE;
            return discover(currentScreen, fromX, fromY);
        }
        if (recipeBook != null && recipeBook.m_100385_() && isNarrow(currentScreen)) {
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
        if (overlay != null && overlay.m_100212_()) {
            overlay.m_100204_(false);
            region = Region.NONE;
            recipeWidget = null;
            return true;
        }
        if (recipeBook != null && recipeBook.m_100385_() && isNarrow(currentScreen)) {
            recipeBook.m_100384_();
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
        clearFocus(currentScreen);
        return center(recipeWidget);
    }

    private Position moveNative(Screen currentScreen, GridNavigation.Direction direction, boolean allowSameTarget) {
        GuiEventListener before = deepestFocused(currentScreen);
        currentScreen.m_7933_(key(direction), 0, 0);
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
            currentScreen.m_7933_(GLFW.GLFW_KEY_TAB, 0, 0);
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
        clearFocus(currentScreen);
        return center(nearest);
    }

    private Position selectRecipeBoundary(Screen currentScreen, List<AbstractWidget> targets, double y, boolean leftEdge, Region targetRegion) {
        AbstractWidget best = null;
        int edge = leftEdge ? Integer.MAX_VALUE : Integer.MIN_VALUE;
        int rowDistance = Integer.MAX_VALUE;
        for (AbstractWidget target : targets) {
            int targetEdge = leftEdge ? target.m_252754_() : target.m_252754_() + target.m_5711_();
            int targetRowDistance = Math.abs(target.m_252907_() + target.m_93694_() / 2 - (int) y);
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
        clearFocus(currentScreen);
        return center(best);
    }

    private Position selectSlot(Screen currentScreen, SlotTarget target) {
        region = Region.SLOT;
        slot = target.slot;
        recipeWidget = null;
        clearFocus(currentScreen);
        return new Position(target.x, target.y);
    }

    private static List<SlotTarget> slotTargets(AbstractContainerScreen<?> screen) {
        List<SlotTarget> targets = new ArrayList<SlotTarget>();
        ForgeControllerContainerAccessor accessor = (ForgeControllerContainerAccessor) screen;
        int left = accessor.banditvault$leftPos();
        int top = accessor.banditvault$topPos();
        for (Slot candidate : accessor.banditvault$menu().f_38839_) {
            if (candidate.m_6659_() && candidate.m_280329_()) {
                targets.add(new SlotTarget(candidate, left + candidate.f_40220_ + 8, top + candidate.f_40221_ + 8));
            }
        }
        return targets;
    }

    private List<AbstractWidget> recipeTargets(RecipeBookComponent recipeBook) {
        List<AbstractWidget> targets = new ArrayList<AbstractWidget>();
        if (recipeBook == null || !recipeBook.m_100385_()) {
            return targets;
        }
        try {
            ForgeControllerRecipeBookAccessor accessor = (ForgeControllerRecipeBookAccessor) recipeBook;
            addVisible(targets, accessor.banditvault$searchBox());
            addVisible(targets, accessor.banditvault$filterButton());
            for (AbstractWidget tab : accessor.banditvault$tabButtons()) {
                addVisible(targets, tab);
            }
            ForgeControllerRecipeBookPageAccessor page = (ForgeControllerRecipeBookPageAccessor) accessor.banditvault$recipeBookPage();
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
        if (overlay == null || !overlay.m_100212_()) {
            return targets;
        }
        try {
            for (AbstractWidget button : ((ForgeControllerRecipeOverlayAccessor) overlay).banditvault$recipeButtons()) {
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
            Object page = ((ForgeControllerRecipeBookAccessor) recipeBook).banditvault$recipeBookPage();
            return ((ForgeControllerRecipeBookPageAccessor) page).banditvault$overlay();
        } catch (RuntimeException error) {
            logUnavailableOnce(screen, "recipe-overlay hook unavailable: " + error.getClass().getSimpleName());
            return null;
        }
    }

    private static RecipeBookComponent recipeBook(Screen currentScreen) {
        return currentScreen instanceof RecipeUpdateListener
            ? ((RecipeUpdateListener) currentScreen).m_5564_()
            : null;
    }

    private static boolean isNarrow(Screen currentScreen) {
        return currentScreen.f_96543_ < 379;
    }

    private static GuiEventListener deepestFocused(ContainerEventHandler container) {
        GuiEventListener focused = container.m_7222_();
        while (focused instanceof ContainerEventHandler) {
            GuiEventListener child = ((ContainerEventHandler) focused).m_7222_();
            if (child == null || child == focused) {
                break;
            }
            focused = child;
        }
        return focused;
    }

    static void clearFocus(Screen screen) {
        if (screen instanceof ContainerEventHandler) {
            ((ContainerEventHandler) screen).m_7522_(null);
        }
    }

    private static Position focusedPosition(GuiEventListener focused) {
        return focused instanceof AbstractWidget ? center((AbstractWidget) focused) : null;
    }

    private static Position center(AbstractWidget widget) {
        return new Position(widget.m_252754_() + widget.m_5711_() / 2.0, widget.m_252907_() + widget.m_93694_() / 2.0);
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

    private static List<GridNavigation.Point> widgetPoints(List<AbstractWidget> widgets) {
        List<GridNavigation.Point> points = new ArrayList<GridNavigation.Point>(widgets.size());
        for (int i = 0; i < widgets.size(); i++) {
            AbstractWidget widget = widgets.get(i);
            points.add(new GridNavigation.Point(i, widget.m_252754_() + widget.m_5711_() / 2, widget.m_252907_() + widget.m_93694_() / 2));
        }
        return points;
    }

    private static void addVisible(List<AbstractWidget> targets, AbstractWidget widget) {
        if (widget != null && widget.f_93624_ && widget.f_93623_ && widget.m_5711_() > 0 && widget.m_93694_() > 0) {
            targets.add(widget);
        }
    }

    private static AbstractWidget nearestWidget(List<AbstractWidget> targets, double x, double y) {
        AbstractWidget best = null;
        double bestDistance = Double.MAX_VALUE;
        for (AbstractWidget target : targets) {
            double dx = target.m_252754_() + target.m_5711_() / 2.0 - x;
            double dy = target.m_252907_() + target.m_93694_() / 2.0 - y;
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
            ForgeControllerLog.log("Snap navigation hook unavailable screen=" + screenName + " detail=" + detail);
        }
    }

    private void logDiscoveryFailureOnce(Screen currentScreen, String targetRegion) {
        String screenName = currentScreen == null ? "unknown" : currentScreen.getClass().getName();
        String key = screenName + ":" + targetRegion;
        if (loggedDiscoveryFailures.add(key)) {
            ForgeControllerLog.log("Snap target discovery failed screen=" + screenName + " region=" + targetRegion);
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
