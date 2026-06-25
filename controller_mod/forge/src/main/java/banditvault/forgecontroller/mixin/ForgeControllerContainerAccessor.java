package banditvault.forgecontroller.mixin;

import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.world.inventory.AbstractContainerMenu;
import net.minecraft.world.inventory.ClickType;
import net.minecraft.world.inventory.Slot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(value = AbstractContainerScreen.class, remap = false)
public interface ForgeControllerContainerAccessor {
    @Accessor("f_97735_")
    int banditvault$leftPos();

    @Accessor("f_97736_")
    int banditvault$topPos();

    @Accessor("f_97732_")
    AbstractContainerMenu banditvault$menu();

    @Invoker("m_6597_")
    void banditvault$slotClicked(Slot slot, int slotId, int mouseButton, ClickType clickType);
}
