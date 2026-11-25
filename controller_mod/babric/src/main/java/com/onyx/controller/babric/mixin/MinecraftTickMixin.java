package com.onyx.controller.babric.mixin;

import com.onyx.controller.babric.GamepadBridge;
import com.onyx.controller.babric.LegacyKeyBindings;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Beta 1.7.3's legacy method name is supplied by Babric's named runtime.
 * Disable annotation-processor remapping here: the old Babric mapping set
 * does not publish a usable refmap entry for this target.
 */
@Mixin(targets = "net.minecraft.client.Minecraft", remap = false)
public abstract class MinecraftTickMixin {
    @Inject(method = "runTick()V", at = @At("TAIL"), require = 1, remap = false)
    private void onyxXboxController$tick(CallbackInfo callback) {
        GamepadBridge.poll();
        LegacyKeyBindings.apply(this);
    }
}
