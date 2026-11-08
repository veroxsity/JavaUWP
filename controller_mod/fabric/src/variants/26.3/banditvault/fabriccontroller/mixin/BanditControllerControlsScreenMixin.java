package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import banditvault.fabriccontroller.BanditControllerSettingsScreen;
import banditvault.fabriccontroller.FabricClientApi;
import java.util.Collections;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.OptionsList;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.options.controls.ControlsScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ControlsScreen.class)
public abstract class BanditControllerControlsScreenMixin {
    @Inject(method = "addOptions", at = @At("TAIL"))
    private void banditvault$addControllerSettingsButton(CallbackInfo ci) {
        Screen screen = (Screen) (Object) this;
        Button button = BanditControllerCompat.createButton(0, 0, 150, 20, "Bandit Controller...", ignored ->
            FabricClientApi.setScreen(Minecraft.getInstance(), new BanditControllerSettingsScreen(screen)));
        OptionsList list = ((BanditControllerOptionsSubScreenAccessor) this).banditvault$optionsList();
        list.addSmall(Collections.<AbstractWidget>singletonList(button));
    }
}
