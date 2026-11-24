package com.onyx.controller.babric.mixin;

import com.onyx.controller.babric.GamepadBridge;
import com.onyx.controller.babric.LegacyKeyBindings;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(targets = "net.minecraft.client.Minecraft")
public abstract class MinecraftTickMixin {
    @Inject(method = "runTick()V", at = @At("TAIL"), require = 1)
    private void onyxXboxController$tick(CallbackInfo callback) {
        GamepadBridge.poll();
        LegacyKeyBindings.apply(this);
    }
}
