// Writes the program's names, types, comments and seeds back to the annotation files (M3 spec §5.3).
// Args: "write" (default) rewrites ghidra740/annotations; "check" writes build/ghidra740/export and compares
// (build/ghidra740/roundtrip.txt). A record keeps the file it came from; new records go to new.txt.
// build/ghidra740/export.txt ends in "EXPORT DONE" only when the run completed; in check mode it also carries the
// verdict (DIFF lines and RESULT PASS|FAIL), and roundtrip.txt is deleted first so no stale verdict can survive.
// Headless write refuses to run unless BuildProject completed without errors (build.txt): an export of a
// half-built project would silently drop records. write deletes annotation files that no longer hold anything.
//@category ghidra740
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.data.Array;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

public class ExportAnnotations extends GhidraScript {
    private final Map<String, String> fileOfKey = new HashMap<>(), fileOfAddr = new HashMap<>();
    private final Set<String> named = new HashSet<>(), eolUsed = new HashSet<>();
    private final Map<String, String> tableKind = new HashMap<>();   // addr key -> jumptable | jumptable-m1

    @Override
    public void run() throws Exception {
        String mode = getScriptArgs().length > 0 ? getScriptArgs()[0] : "write";
        if (!mode.equals("write") && !mode.equals("check"))
            throw new IllegalArgumentException("mode must be write or check, not " + mode);
        Path root = Annot.repoRoot(this);
        Path done = root.resolve("build/ghidra740/export.txt");
        Files.createDirectories(done.getParent());
        Files.deleteIfExists(done);
        Path dir = root.resolve("ghidra740/annotations");
        if (mode.equals("write") && isRunningHeadless()) {
            // Headless, build.txt is this run's own BuildProject report: no report, no SUMMARY (BuildProject did not
            // complete) or an ERROR line means the program is not the full project. In the GUI there is no such
            // report to trust (build.txt would be whichever unrelated run wrote it last), so the caller checks.
            Path buildReport = root.resolve("build/ghidra740/build.txt");
            List<String> lines = Files.exists(buildReport) ? Files.readAllLines(buildReport, StandardCharsets.UTF_8) : List.of();
            boolean built = lines.stream().anyMatch(l -> l.startsWith("SUMMARY ") && !l.equals("SUMMARY not built"));
            if (!built || lines.stream().anyMatch(l -> l.startsWith("ERROR ")))
                throw new IllegalStateException("BuildProject did not complete cleanly (build/ghidra740/build.txt): not exporting a partial project");
        }
        if (mode.equals("check")) Files.deleteIfExists(root.resolve("build/ghidra740/roundtrip.txt"));
        Annot.Annotations old = Annot.load(dir);
        for (Annot.Rec r : old.recs) {
            fileOfKey.put(key(r.space, r.addr) + ":" + r.kind, r.file);
            fileOfAddr.putIfAbsent(key(r.space, r.addr), r.file);
        }
        Annot.Annotations now = new Annot.Annotations();
        now.headers.putAll(old.headers);
        collect(now);
        collectStructs(now);
        if (mode.equals("write")) {
            Annot.write(now, dir);
            println("ExportAnnotations: wrote " + now.recs.size() + " records to " + dir);
            Files.writeString(done, "wrote " + now.recs.size() + " records\nEXPORT DONE\n", StandardCharsets.UTF_8);
            return;
        }
        Path tmp = root.resolve("build/ghidra740/export");
        Files.createDirectories(tmp);
        try (Stream<Path> s = Files.list(tmp)) {
            for (Path p : s.collect(Collectors.toList())) Files.delete(p);
        }
        Annot.write(now, tmp);
        Set<String> files = new TreeSet<>();
        try (Stream<Path> a = Files.list(dir); Stream<Path> b = Files.list(tmp)) {
            a.forEach(p -> files.add(p.getFileName().toString()));
            b.forEach(p -> files.add(p.getFileName().toString()));
        }
        StringBuilder sb = new StringBuilder();
        for (String f : files) {
            Path x = dir.resolve(f), y = tmp.resolve(f);
            if (!Files.exists(x) || !Files.exists(y) || !Files.readString(x).equals(Files.readString(y)))
                sb.append("DIFF ").append(f).append('\n');
        }
        sb.append(sb.length() == 0 ? "RESULT PASS\n" : "RESULT FAIL\n");
        Files.writeString(root.resolve("build/ghidra740/roundtrip.txt"), sb.toString(), StandardCharsets.UTF_8);
        println("ExportAnnotations check: " + sb.toString().replace('\n', ' '));
        Files.writeString(done, "checked " + now.recs.size() + " records\n" + sb + "EXPORT DONE\n", StandardCharsets.UTF_8);
    }

    private static String key(String space, long addr) { return space + ":" + String.format("%04X", addr); }
    private static String key(Address a) { return key(Annot.spaceName(a), a.getOffset()); }
    private static String oneLine(String s) { return s == null ? "" : s.replaceAll("\\s*[\\r\\n]+\\s*", " ").strip(); }

    private void add(Annot.Annotations now, Address a, String kind, String name, String type, String comment) {
        String k = key(a);
        String file = fileOfKey.getOrDefault(k + ":" + kind, fileOfAddr.getOrDefault(k, Annot.NEW_FILE));
        now.recs.add(new Annot.Rec(file, 0, Annot.spaceName(a), (int) a.getOffset(), kind, name, type, oneLine(comment)));
    }

    private String eol(Address a) {
        String c = getEOLComment(a);
        if (c != null) eolUsed.add(key(a));
        return c;
    }

    private void collect(Annot.Annotations now) throws Exception {
        Listing listing = currentProgram.getListing();
        Iterator<Bookmark> bit = currentProgram.getBookmarkManager().getBookmarksIterator(BookmarkType.NOTE);
        while (bit.hasNext()) {
            Bookmark b = bit.next();
            if (!b.getCategory().equals(Annot.BM_CATEGORY)) continue;
            String d = b.getComment();
            if (d.equals("jumptable") || d.equals("jumptable-m1")) tableKind.put(key(b.getAddress()), d);
            else if (d.equals("entry")) add(now, b.getAddress(), "entry", "-", "-", "");
            else if (d.startsWith("entry: ")) add(now, b.getAddress(), "entry", "-", "-", d.substring(7));
        }
        for (Function f : listing.getFunctions(true)) {
            if (f.getSymbol().getSource() == SourceType.DEFAULT) continue;
            Address a = f.getEntryPoint();
            add(now, a, "func", f.getName(), "-", getPlateComment(a));
            named.add(key(a));
        }
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            Symbol s = d.getPrimarySymbol();
            if (s == null || s.getSource() == SourceType.DEFAULT) continue;
            Address a = d.getAddress();
            String tk = tableKind.get(key(a));
            add(now, a, tk == null ? "data" : "jumptable", s.getName(), typeString(d, "jumptable-m1".equals(tk)), eol(a));
            named.add(key(a));
        }
        SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(false);
        while (si.hasNext()) {
            Symbol s = si.next();
            if (s.getSymbolType() != SymbolType.LABEL || s.getSource() == SourceType.DEFAULT || !s.isPrimary()) continue;
            Address a = s.getAddress();
            if (!a.isMemoryAddress() || named.contains(key(a))) continue;
            add(now, a, "label", s.getName(), "-", eol(a));
            named.add(key(a));
        }
        AddressIterator ci = listing.getCommentAddressIterator(CommentType.EOL, currentProgram.getMemory(), true);
        while (ci.hasNext()) {
            Address a = ci.next();
            if (eolUsed.contains(key(a))) continue;
            add(now, a, "comment", "-", "-", getEOLComment(a));
        }
    }

    private static String base(DataType dt, boolean m1) {
        if (dt instanceof Pointer) return "ptr";
        if (dt instanceof ByteDataType) return "byte";
        if (dt instanceof WordDataType) return m1 ? "ptrm1" : "word";
        if (dt instanceof CharDataType) return "char";
        if (dt instanceof Structure) return dt.getName();
        return "?" + dt.getName();   // fails validation on the next load, so it gets noticed
    }

    private static String typeString(Data d, boolean m1) {
        DataType dt = d.getDataType();
        if (dt instanceof Array) {
            Array arr = (Array) dt;
            return base(arr.getDataType(), m1) + "[" + arr.getNumElements() + "]";
        }
        if (dt instanceof StringDataType) return "string[" + d.getLength() + "]";
        return base(dt, m1);
    }

    private void collectStructs(Annot.Annotations now) {
        Category cat = currentProgram.getDataTypeManager().getCategory(new CategoryPath("/" + Annot.BM_CATEGORY));
        if (cat == null) return;
        for (DataType dt : cat.getDataTypes()) {
            if (!(dt instanceof Structure)) continue;
            Structure st = (Structure) dt;
            Annot.Struct s = new Annot.Struct(st.getName(), st.getLength(), oneLine(st.getDescription()));
            for (DataTypeComponent c : st.getDefinedComponents()) {
                DataType ct = c.getDataType();
                String type = ct instanceof Array
                    ? base(((Array) ct).getDataType(), false) + "[" + ((Array) ct).getNumElements() + "]"
                    : base(ct, false);
                s.fields.add(new Annot.Field(c.getOffset(), c.getFieldName(), type, oneLine(c.getComment())));
            }
            now.structs.put(s.name, s);
        }
    }
}
