package banditvault.forgecontroller.mixin;

import banditvault.forgecontroller.ForgeControllerCompat;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.controls.ControlsScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = ControlsScreen.class, remap = false)
public abstract class ForgeControllerControlsScreenMixin extends Screen {
    protected ForgeControllerControlsScreenMixin() {
        super(ForgeControllerCompat.textLiteral(""));
    }

    @Inject(method = "m_7856_", at = @At("TAIL"), remap = false)
    private void banditvault$addControllerSettingsButton(CallbackInfo ci) {
        Screen screen = (Screen) (Object) this;
        Button button = ForgeControllerCompat.createButton(
            this.f_96543_ / 2 - 75,
            Math.max(4, this.f_96544_ - 52),
            150,
            20,
            "Bandit Controller...",
            ignored -> Minecraft.m_91087_().m_91152_(ForgeControllerCompat.createSettingsScreen(screen)));
        this.m_142416_(button);
    }
}
