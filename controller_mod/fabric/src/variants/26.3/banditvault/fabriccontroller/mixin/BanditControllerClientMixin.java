package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Minecraft.class)
public abstract class BanditControllerClientMixin {
    @Inject(method = "tick", at = @At("TAIL"))
    private void banditvault$tickControllerCompat(CallbackInfo ci) {
        BanditControllerCompat.tick((Minecraft)(Object)this);
    }
}
