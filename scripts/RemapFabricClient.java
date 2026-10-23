import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.Files;
import java.util.Arrays;
import net.fabricmc.loader.impl.lib.mappingio.format.tiny.Tiny1FileReader;
import net.fabricmc.loader.impl.lib.mappingio.format.tiny.Tiny2FileReader;
import net.fabricmc.loader.impl.lib.mappingio.tree.MemoryMappingTree;
import net.fabricmc.loader.impl.lib.tinyremapper.NonClassCopyMode;
import net.fabricmc.loader.impl.lib.tinyremapper.InputTag;
import net.fabricmc.loader.impl.lib.tinyremapper.OutputConsumerPath;
import net.fabricmc.loader.impl.lib.tinyremapper.TinyRemapper;
import net.fabricmc.loader.impl.lib.tinyremapper.TinyUtils;

public final class RemapFabricClient {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) throw new IllegalArgumentException("Expected client jar, output jar and mappings");
        Path input = Paths.get(args[0]);
        MemoryMappingTree mappings = new MemoryMappingTree();
        try (var reader = Files.newBufferedReader(Paths.get(args[2]))) {
            reader.mark(1);
            int initial = reader.read();
            reader.reset();
            if (initial == 't') Tiny2FileReader.read(reader, mappings);
            else Tiny1FileReader.read(reader, mappings);
        }
        TinyRemapper remapper = TinyRemapper.newRemapper((level, message) -> System.out.println(message))
            .withMappings(TinyUtils.createMappingProvider(mappings, "official", "intermediary"))
            .rebuildSourceFilenames(true)
            .build();
        try (OutputConsumerPath output = new OutputConsumerPath.Builder(Paths.get(args[1])).assumeArchive(true).build()) {
            output.addNonClassFiles(input, NonClassCopyMode.FIX_META_INF, remapper);
            remapper.readClassPathAsync(Arrays.stream(args).skip(3).map(Paths::get).toArray(Path[]::new));
            remapper.readInputsAsync((InputTag) null, input);
            remapper.apply(output);
        } finally {
            remapper.finish();
        }
    }
}
