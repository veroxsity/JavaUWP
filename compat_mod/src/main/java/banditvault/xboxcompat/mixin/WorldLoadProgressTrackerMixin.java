package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.XboxCompatLog;
import net.minecraft.class_11544;
import net.minecraft.class_11545;
import net.minecraft.class_11653;
import net.minecraft.class_1923;
import net.minecraft.class_1937;
import net.minecraft.class_5321;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(class_11653.class)
public abstract class WorldLoadProgressTrackerMixin {
    @Unique
    private long banditvault$chunkEventCount = 0L;

    @Unique
    private boolean banditvault$readyLogged = false;

    @Shadow
    public abstract float method_72905();

    @Inject(method = "method_72900", at = @At("TAIL"))
    private void banditvault$logTrackerListener(class_11544 listener, CallbackInfo ci) {
        XboxCompatLog.log("world-load tracker listener=" + banditvault$className(listener));
    }

    @Inject(method = "method_72281", at = @At("TAIL"))
    private void banditvault$logStageStarted(class_11545.class_11546 stage, int total, CallbackInfo ci) {
        XboxCompatLog.log("world-load stage start stage=" + banditvault$stageName(stage)
            + " total=" + total);
    }

    @Inject(method = "method_72282", at = @At("TAIL"))
    private void banditvault$logStageProgress(class_11545.class_11546 stage, int current, int total, CallbackInfo ci) {
        if (current <= 1 || current == total || current % 128 == 0) {
            XboxCompatLog.log("world-load stage progress stage=" + banditvault$stageName(stage)
                + " current=" + current + "/" + total
                + " overall=" + this.method_72905());
        }
    }

    @Inject(method = "method_72280", at = @At("TAIL"))
    private void banditvault$logStageFinished(class_11545.class_11546 stage, CallbackInfo ci) {
        XboxCompatLog.log("world-load stage finish stage=" + banditvault$stageName(stage)
            + " overall=" + this.method_72905());
    }

    @Inject(method = "method_72279", at = @At("TAIL"))
    private void banditvault$logChunkEvent(class_5321<class_1937> dimension, class_1923 chunkPos, CallbackInfo ci) {
        this.banditvault$chunkEventCount++;
        if (this.banditvault$chunkEventCount <= 16 || this.banditvault$chunkEventCount % 128 == 0) {
            XboxCompatLog.log("world-load chunk event count=" + this.banditvault$chunkEventCount
                + " dim=" + dimension
                + " chunk=" + chunkPos);
        }
    }

    @Inject(method = "method_72902", at = @At("TAIL"))
    private void banditvault$logReady(CallbackInfoReturnable<Boolean> cir) {
        if (Boolean.TRUE.equals(cir.getReturnValue()) && !this.banditvault$readyLogged) {
            this.banditvault$readyLogged = true;
            XboxCompatLog.log("world-load tracker ready chunkEvents=" + this.banditvault$chunkEventCount
                + " overall=" + this.method_72905());
        }
    }

    @Unique
    private static String banditvault$stageName(class_11545.class_11546 stage) {
        return stage == null ? "null" : stage.name() + "#" + stage.ordinal();
    }

    @Unique
    private static String banditvault$className(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }
}
