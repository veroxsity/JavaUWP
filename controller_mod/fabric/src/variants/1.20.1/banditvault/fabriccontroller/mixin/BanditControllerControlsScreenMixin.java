package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import banditvault.fabriccontroller.BanditControllerSettingsScreen;
import net.minecraft.class_310;
import net.minecraft.class_4185;
import net.minecraft.class_437;
import net.minecraft.class_458;
import net.minecraft.class_2561;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_458.class)
public abstract class BanditControllerControlsScreenMixin extends class_437 {
    protected BanditControllerControlsScreenMixin() {
        super(class_2561.method_30163(""));
    }

    @Inject(method = "method_25426", at = @At("TAIL"))
    private void banditvault$addControllerSettingsButton(CallbackInfo ci) {
        class_437 screen = (class_437) (Object) this;
        class_4185 button = BanditControllerCompat.createButton(this.field_22789 / 2 - 75, Math.max(4, this.field_22790 - 52), 150, 20, "Bandit Controller...", ignored ->
            class_310.method_1551().method_1507(new BanditControllerSettingsScreen(screen)));
        this.method_37063(button);
    }
}
