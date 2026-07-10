package banditvault.d3d11mod.mixin;

import net.minecraft.server.MinecraftServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Singleplayer forces synchronous region-file writes (every chunk save fsyncs). Xbox UWP
 * brokered storage handles fsync very poorly, so chunk saving stalls the IO worker and
 * back-pressures the whole chunk system exactly while new chunks stream in. Async writes
 * trade a tiny crash-durability window for smooth chunk generation.
 */
@Mixin(MinecraftServer.class)
public abstract class MinecraftServerMixin {
    @Inject(method = "forceSynchronousWrites", at = @At("HEAD"), cancellable = true)
    private void banditvault$asyncChunkWrites(CallbackInfoReturnable<Boolean> cir) {
        cir.setReturnValue(false);
    }
}