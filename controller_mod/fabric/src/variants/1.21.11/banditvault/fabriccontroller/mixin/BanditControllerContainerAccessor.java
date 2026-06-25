package banditvault.fabriccontroller.mixin;

import net.minecraft.class_1713;
import net.minecraft.class_1735;
import net.minecraft.class_465;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(class_465.class)
public interface BanditControllerContainerAccessor {
    @Accessor("field_2776")
    int banditvault$leftPos();

    @Accessor("field_2800")
    int banditvault$topPos();

    @Accessor("field_2787")
    class_1735 banditvault$getSlotUnderMouse();

    @Invoker("method_2383")
    void banditvault$slotClicked(class_1735 slot, int slotId, int button, class_1713 actionType);
}
