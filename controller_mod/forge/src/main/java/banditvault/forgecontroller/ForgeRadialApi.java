package banditvault.forgecontroller;

import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;

final class ForgeRadialApi {
    static ItemStack icon(String item) {
        Item resolved = BuiltInRegistries.f_257033_.m_7745_(new ResourceLocation("minecraft", item));
        return resolved == null ? null : new ItemStack(resolved);
    }
}
