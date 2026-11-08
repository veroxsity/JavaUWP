package banditvault.fabriccontroller.mixin;

import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.inventory.Slot;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(AbstractContainerScreen.class)
public interface BanditControllerContainerAccessor {
    @Accessor("leftPos")
    int banditvault$leftPos();

    @Accessor("topPos")
    int banditvault$topPos();

    @Accessor("hoveredSlot")
    Slot banditvault$getSlotUnderMouse();

    @Invoker("slotClicked")
    void banditvault$slotClicked(Slot slot, int slotId, int button, ContainerInput actionType);
}
