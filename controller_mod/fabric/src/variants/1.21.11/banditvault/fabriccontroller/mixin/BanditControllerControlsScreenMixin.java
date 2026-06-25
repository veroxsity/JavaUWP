package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import banditvault.fabriccontroller.BanditControllerSettingsScreen;
import java.util.Collections;
import net.minecraft.class_310;
import net.minecraft.class_339;
import net.minecraft.class_353;
import net.minecraft.class_4185;
import net.minecraft.class_437;
import net.minecraft.class_458;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_458.class)
public abstract class BanditControllerControlsScreenMixin {
    @Inject(method = "method_60325", at = @At("TAIL"))
    private void banditvault$addControllerSettingsButton(CallbackInfo ci) {
        class_437 screen = (class_437) (Object) this;
        class_4185 button = BanditControllerCompat.createButton(0, 0, 150, 20, "Bandit Controller...", ignored ->
            class_310.method_1551().method_1507(new BanditControllerSettingsScreen(screen)));
        class_353 list = ((BanditControllerOptionsSubScreenAccessor) this).banditvault$optionsList();
        list.method_58227(Collections.<class_339>singletonList(button));
    }
}
