package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.ZipFsPathResolver;
import net.minecraft.util.FileSystemUtil;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;

import java.io.IOException;
import java.net.URI;
import java.nio.file.Path;

@Mixin(FileSystemUtil.class)
public abstract class ZipFsBypassMixin {
    /**
     * @author BanditVault
     * @reason Xbox sandbox paths reject canonical path resolution
     */
    @Overwrite
    public static Path safeGetPath(URI uri) throws IOException {
        return ZipFsPathResolver.resolve(uri);
    }
}
