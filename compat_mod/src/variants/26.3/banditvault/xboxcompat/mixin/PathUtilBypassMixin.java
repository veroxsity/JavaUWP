package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.PathUtilResolver;
import net.minecraft.util.FileUtil;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;

import java.io.IOException;
import java.nio.file.Path;

@Mixin(FileUtil.class)
public abstract class PathUtilBypassMixin {
    /**
     * @author BanditVault
     * @reason Xbox sandbox paths reject Path.toRealPath
     */
    @Overwrite
    public static void createDirectoriesSafe(Path path) throws IOException {
        PathUtilResolver.createDirectories(path);
    }
}
