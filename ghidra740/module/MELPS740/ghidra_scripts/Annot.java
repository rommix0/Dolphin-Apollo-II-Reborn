// Shared helpers for the ghidra740 scripts. Not a script itself.
import ghidra.app.script.GhidraScript;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

public class Annot {
    /** The repo root that ghidra740\install.bat wrote to repo.txt beside ghidra_scripts/. */
    public static Path repoRoot(GhidraScript script) throws IOException {
        Path ext = Paths.get(script.getSourceFile().getParentFile().getParentFile().getAbsolutePath());
        return Paths.get(Files.readString(ext.resolve("repo.txt"), StandardCharsets.UTF_8).trim());
    }
}
