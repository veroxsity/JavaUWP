package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.XboxCompatLog;
import net.minecraft.class_310;
import net.minecraft.class_437;
import net.minecraft.class_542;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_310.class)
public abstract class MinecraftClientProbeMixin {
    private static long banditvault$tickCount = 0L;
    private static boolean banditvault$uncaughtHandlerInstalled = false;

    @Shadow
    public class_437 field_1755;

    @Inject(method = "<init>", at = @At("TAIL"))
    private void banditvault$logClientConstructed(class_542 args, CallbackInfo ci) {
        XboxCompatLog.log("MinecraftClient constructed");
        banditvault$installUncaughtExceptionHandler();
    }

    @Inject(method = "method_1514", at = @At("HEAD"))
    private void banditvault$logMainLoopEntered(CallbackInfo ci) {
        XboxCompatLog.log("MinecraftClient main loop entered");
    }

    @Inject(method = "method_1514", at = @At("TAIL"))
    private void banditvault$logMainLoopExited(CallbackInfo ci) {
        XboxCompatLog.log("MinecraftClient main loop exited");
    }

    @Inject(method = "method_1507", at = @At("HEAD"))
    private void banditvault$logSetScreenHead(class_437 screen, CallbackInfo ci) {
        XboxCompatLog.log("setScreen head -> " + banditvault$screenName(screen));
    }

    @Inject(method = "method_1507", at = @At("TAIL"))
    private void banditvault$logSetScreenTail(class_437 screen, CallbackInfo ci) {
        XboxCompatLog.log("setScreen tail -> current=" + banditvault$screenName(this.field_1755));
    }

    @Inject(method = "method_1574", at = @At("HEAD"))
    private void banditvault$logClientTick(CallbackInfo ci) {
        banditvault$tickCount++;
        if (banditvault$tickCount <= 5 || banditvault$tickCount % 120 == 0) {
            XboxCompatLog.log("client tick=" + banditvault$tickCount
                + " screen=" + banditvault$screenName(this.field_1755));
        }
    }

    private static String banditvault$screenName(class_437 screen) {
        return screen == null ? "null" : screen.getClass().getName();
    }

    private static synchronized void banditvault$installUncaughtExceptionHandler() {
        if (banditvault$uncaughtHandlerInstalled) {
            return;
        }

        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            XboxCompatLog.logException(
                "Uncaught exception on thread=" + thread.getName(),
                throwable);
            if (previous != null) {
                previous.uncaughtException(thread, throwable);
            }
        });
        banditvault$uncaughtHandlerInstalled = true;
        XboxCompatLog.log("Installed default uncaught exception handler");
    }
}
