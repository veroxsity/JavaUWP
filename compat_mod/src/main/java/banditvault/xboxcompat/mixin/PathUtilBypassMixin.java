package banditvault.xboxcompat.mixin;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.class_4239;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;

@Mixin(class_4239.class)
public abstract class PathUtilBypassMixin {
    /**
     * @author Codex
     * @reason Xbox Dev Mode's packaged filesystem rejects Path.toRealPath()
     * on writable sandbox paths like game/saves/.../region and game/debug.
     * Minecraft only needs the directory to exist here, so bypass the
     * canonicalization step and create it directly.
     */
    @Overwrite
    public static void method_47525(Path path) throws IOException {
        Files.createDirectories(path);
    }
}
