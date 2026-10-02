// The ghidra740 annotation files (M3 spec §5.3) and coverage file (§5.1). Not a script: shared by
// BuildProject, CheckProject, DumpProject, ExportAnnotations and ValidateModule.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryBlock;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;
import java.util.stream.*;

public class Annot {
    public static final List<String> SPACES = List.of("ram", "rom", "low", "ramcode");
    public static final List<String> KINDS = List.of("func", "label", "data", "jumptable", "entry", "comment");
    public static final List<String> PRIMITIVES = List.of("byte", "word", "ptr", "ptrm1", "char", "string");
    public static final String TYPES_FILE = "types.txt";
    public static final String NEW_FILE = "new.txt";
    public static final String BM_CATEGORY = "ghidra740";

    public static final class Rec {
        public final String file, space, kind, name, type, comment;
        public final int line, addr;

        public Rec(String file, int line, String space, int addr, String kind, String name, String type, String comment) {
            this.file = file; this.line = line; this.space = space; this.addr = addr;
            this.kind = kind; this.name = name; this.type = type; this.comment = comment == null ? "" : comment;
        }
        public boolean named() { return !name.equals("-"); }
        public String where() { return file + ":" + line; }
        public String format() {
            return String.format("%-8s %04X  %-9s %-30s %-14s %s", space, addr, kind, name, type, comment).stripTrailing();
        }
    }

    public static final class Field {
        public final int offset; public final String name, type, comment;
        public Field(int offset, String name, String type, String comment) {
            this.offset = offset; this.name = name; this.type = type; this.comment = comment == null ? "" : comment;
        }
    }

    public static final class Struct {
        public final String name, comment; public final int size; public final List<Field> fields = new ArrayList<>();
        public Struct(String name, int size, String comment) { this.name = name; this.size = size; this.comment = comment == null ? "" : comment; }
    }

    public static final class Annotations {
        public final List<Rec> recs = new ArrayList<>();
        public final Map<String, Struct> structs = new TreeMap<>();
        public final Map<String, String> headers = new TreeMap<>();   // file name -> first-line comment
    }

    public static final class TypeRef {
        public final String base; public final int count; public final boolean array;
        TypeRef(String base, int count, boolean array) { this.base = base; this.count = count; this.array = array; }
    }

    public static final class Cov {
        public final List<String[]> exec = new ArrayList<>();    // {space, hexaddr}
        public final List<String[]> edges = new ArrayList<>();   // {fromspace, hexfrom, tospace, hexto}
    }

    public static final Comparator<Rec> ORDER = Comparator
        .comparingInt((Rec r) -> SPACES.indexOf(r.space))
        .thenComparingInt(r -> r.addr)
        .thenComparingInt(r -> KINDS.indexOf(r.kind));

    private static final Pattern REC = Pattern.compile("^(\\S+)\\s+([0-9A-Fa-f]{4})\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)(?:\\s+(.*?))?\\s*$");
    private static final Pattern STRUCT = Pattern.compile("^struct\\s+(\\S+)\\s+(\\d+)(?:\\s+(.*?))?\\s*$");
    private static final Pattern FIELD = Pattern.compile("^field\\s+(\\S+)\\s+(\\d+)\\s+(\\S+)\\s+(\\S+)(?:\\s+(.*?))?\\s*$");
    private static final Pattern TYPE = Pattern.compile("^([A-Za-z_][A-Za-z0-9_]*)(?:\\[(\\d+)\\])?$");
    private static final Pattern NAME = Pattern.compile("^[A-Za-z_][A-Za-z0-9_]*$");

    /** The repo root that ghidra740\install.bat wrote to repo.txt beside ghidra_scripts/. */
    public static Path repoRoot(GhidraScript script) throws IOException {
        Path ext = Paths.get(script.getSourceFile().getParentFile().getParentFile().getAbsolutePath());
        return Paths.get(Files.readString(ext.resolve("repo.txt"), StandardCharsets.UTF_8).trim());
    }

    public static TypeRef parseType(String t) {
        Matcher m = TYPE.matcher(t);
        if (!m.matches())
            return null;
        return m.group(2) == null ? new TypeRef(m.group(1), 1, false) : new TypeRef(m.group(1), Integer.parseInt(m.group(2)), true);
    }

    /** Size in bytes, or -1 for an unknown base type. */
    public static int sizeOf(TypeRef t, Map<String, Struct> structs) {
        int e;
        switch (t.base) {
            case "byte": case "char": case "string": e = 1; break;
            case "word": case "ptr": case "ptrm1": e = 2; break;
            default: e = structs.containsKey(t.base) ? structs.get(t.base).size : -1;
        }
        return e < 0 ? -1 : e * t.count;
    }

    public static boolean inRange(String space, int addr) {
        switch (space) {
            case "ram": return addr < 0x4000;
            case "rom": return addr >= 0x4000;
            case "low": return addr >= 0x4000 && addr < 0x8000;
            case "ramcode": return addr >= 0x0200 && addr <= 0x03FE;
            default: return false;
        }
    }

    public static Annotations load(Path dir) throws IOException {
        Annotations a = new Annotations();
        List<String> errors = new ArrayList<>();
        Path types = dir.resolve(TYPES_FILE);
        if (Files.exists(types))
            loadTypes(types, a, errors);
        List<Path> files;
        try (Stream<Path> s = Files.list(dir)) {
            files = s.filter(p -> p.getFileName().toString().endsWith(".txt") && !p.getFileName().toString().equals(TYPES_FILE))
                     .sorted().collect(Collectors.toList());
        }
        for (Path p : files)
            loadRecords(p, a, errors);
        validate(a, errors);
        if (!errors.isEmpty())
            throw new IllegalArgumentException("annotation errors:\n  " + String.join("\n  ", errors));
        return a;
    }

    private static void loadTypes(Path p, Annotations a, List<String> errors) throws IOException {
        List<String> lines = Files.readAllLines(p, StandardCharsets.UTF_8);
        for (int i = 0; i < lines.size(); i++) {
            String line = lines.get(i).strip();
            if (i == 0 && line.startsWith("#")) { a.headers.put(TYPES_FILE, line); continue; }
            if (line.isEmpty() || line.startsWith("#")) continue;
            Matcher s = STRUCT.matcher(line), f = FIELD.matcher(line);
            if (s.matches()) {
                a.structs.put(s.group(1), new Struct(s.group(1), Integer.parseInt(s.group(2)), s.group(3)));
            } else if (f.matches()) {
                Struct st = a.structs.get(f.group(1));
                if (st == null) errors.add(TYPES_FILE + ":" + (i + 1) + ": field of undeclared struct " + f.group(1));
                else st.fields.add(new Field(Integer.parseInt(f.group(2)), f.group(3), f.group(4), f.group(5)));
            } else {
                errors.add(TYPES_FILE + ":" + (i + 1) + ": malformed line");
            }
        }
    }

    private static void loadRecords(Path p, Annotations a, List<String> errors) throws IOException {
        String file = p.getFileName().toString();
        List<String> lines = Files.readAllLines(p, StandardCharsets.UTF_8);
        for (int i = 0; i < lines.size(); i++) {
            String line = lines.get(i).strip();
            if (i == 0 && line.startsWith("#")) { a.headers.put(file, line); continue; }
            if (line.isEmpty() || line.startsWith("#")) continue;
            Matcher m = REC.matcher(line);
            if (!m.matches()) { errors.add(file + ":" + (i + 1) + ": malformed record"); continue; }
            a.recs.add(new Rec(file, i + 1, m.group(1), Integer.parseInt(m.group(2), 16), m.group(3), m.group(4), m.group(5), m.group(6)));
        }
    }

    private static void validate(Annotations a, List<String> errors) {
        for (Struct s : a.structs.values()) {
            for (Field f : s.fields) {
                TypeRef t = parseType(f.type);
                if (t == null || !PRIMITIVES.contains(t.base) || t.base.equals("string"))
                    errors.add(TYPES_FILE + ": " + s.name + "." + f.name + ": field type must be a primitive (not string): " + f.type);
                else if (f.offset + sizeOf(t, a.structs) > s.size)
                    errors.add(TYPES_FILE + ": " + s.name + "." + f.name + " runs past the struct size " + s.size);
            }
        }
        Set<String> names = new HashSet<>(), namedAt = new HashSet<>(), eolAt = new HashSet<>(), kindAt = new HashSet<>();
        List<int[]> spans = new ArrayList<>();   // {spaceIndex, start, end, recIndex}
        for (int i = 0; i < a.recs.size(); i++) {
            Rec r = a.recs.get(i);
            String at = r.space + ":" + String.format("%04X", r.addr);
            if (!SPACES.contains(r.space)) { errors.add(r.where() + ": unknown space " + r.space); continue; }
            if (!KINDS.contains(r.kind)) { errors.add(r.where() + ": unknown kind " + r.kind); continue; }
            if (!inRange(r.space, r.addr)) errors.add(r.where() + ": address " + at + " outside its space");
            if (!kindAt.add(at + ":" + r.kind)) errors.add(r.where() + ": duplicate " + r.kind + " at " + at);
            boolean wantsName = !r.kind.equals("entry") && !r.kind.equals("comment");
            if (wantsName != r.named()) errors.add(r.where() + ": " + r.kind + (wantsName ? " needs a name" : " takes no name (-)"));
            if (r.named()) {
                if (!NAME.matcher(r.name).matches()) errors.add(r.where() + ": bad name " + r.name);
                if (!names.add(r.name)) errors.add(r.where() + ": duplicate name " + r.name);
                if (!namedAt.add(at)) errors.add(r.where() + ": a second named record at " + at);
            }
            if (r.kind.equals("comment") && r.comment.isEmpty()) errors.add(r.where() + ": a comment record needs text");
            if (!r.kind.equals("func") && !r.kind.equals("entry") && !r.comment.isEmpty() && !eolAt.add(at))
                errors.add(r.where() + ": a second EOL comment at " + at);
            boolean typed = r.kind.equals("data") || r.kind.equals("jumptable");
            if (!typed) {
                if (!r.type.equals("-")) errors.add(r.where() + ": " + r.kind + " takes no type (-)");
                continue;
            }
            TypeRef t = parseType(r.type);
            int size = t == null ? -1 : sizeOf(t, a.structs);
            if (size <= 0) { errors.add(r.where() + ": bad type " + r.type); continue; }
            if (t.base.equals("string") && !t.array) errors.add(r.where() + ": string needs a length, string[n]");
            if (r.kind.equals("jumptable") && (!t.array || !(t.base.equals("ptr") || t.base.equals("ptrm1"))))
                errors.add(r.where() + ": jumptable type must be ptr[n] or ptrm1[n]");
            if (r.kind.equals("data") && t.base.equals("ptrm1"))
                errors.add(r.where() + ": ptrm1 is only for jumptable records");
            int sp = r.space.equals("rom") ? 0 : SPACES.indexOf(r.space);   // ram and rom share the default space
            spans.add(new int[] { sp, r.addr, r.addr + size - 1, i });
        }
        spans.sort(Comparator.<int[]>comparingInt(s -> s[0]).thenComparingInt(s -> s[1]));
        for (int i = 1; i < spans.size(); i++) {
            int[] p = spans.get(i - 1), q = spans.get(i);
            if (p[0] == q[0] && q[1] <= p[2])
                errors.add(a.recs.get(q[3]).where() + ": overlaps " + a.recs.get(p[3]).name + " (" + a.recs.get(p[3]).where() + ")");
        }
    }

    /** Writes every file in canonical form: header, then records in ORDER (types.txt: structs by name). */
    public static void write(Annotations a, Path dir) throws IOException {
        Files.createDirectories(dir);
        Map<String, List<Rec>> byFile = new TreeMap<>();
        for (Rec r : a.recs)
            byFile.computeIfAbsent(r.file, k -> new ArrayList<>()).add(r);
        Set<String> files = new TreeSet<>(byFile.keySet());
        files.addAll(a.headers.keySet());
        files.remove(TYPES_FILE);
        for (String f : files) {
            StringBuilder sb = new StringBuilder();
            if (a.headers.containsKey(f)) sb.append(a.headers.get(f)).append('\n');
            List<Rec> rs = byFile.getOrDefault(f, new ArrayList<>());
            rs.sort(ORDER);
            for (Rec r : rs) sb.append(r.format()).append('\n');
            Files.writeString(dir.resolve(f), sb.toString(), StandardCharsets.UTF_8);
        }
        if (a.structs.isEmpty() && !a.headers.containsKey(TYPES_FILE))
            return;
        StringBuilder sb = new StringBuilder();
        if (a.headers.containsKey(TYPES_FILE)) sb.append(a.headers.get(TYPES_FILE)).append('\n');
        for (Struct s : a.structs.values()) {
            sb.append(String.format("%-6s %-24s %5d  %s", "struct", s.name, s.size, s.comment).stripTrailing()).append('\n');
            List<Field> fs = new ArrayList<>(s.fields);
            fs.sort(Comparator.comparingInt(f -> f.offset));
            for (Field f : fs)
                sb.append(String.format("%-6s %-24s %5d  %-24s %-10s %s", "field", s.name, f.offset, f.name, f.type, f.comment).stripTrailing()).append('\n');
        }
        Files.writeString(dir.resolve(TYPES_FILE), sb.toString(), StandardCharsets.UTF_8);
    }

    public static Cov loadCoverage(Path p) throws IOException {
        Cov c = new Cov();
        if (!Files.exists(p))
            return c;
        for (String line : Files.readAllLines(p, StandardCharsets.US_ASCII)) {
            line = line.strip();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] t = line.split("\\s+");
            if (t[0].equals("x") && t.length == 3) c.exec.add(new String[] { t[1], t[2] });
            else if (t[0].equals("e") && t.length == 5) c.edges.add(new String[] { t[1], t[2], t[3], t[4] });
            else throw new IllegalArgumentException(p + ": bad coverage line: " + line);
        }
        return c;
    }

    /** Address of a record or coverage entry. "ram" coverage (RAM outside RAMCODE) has no block and maps to the default space. */
    public static Address addr(Program p, String space, int a) {
        switch (space) {
            case "ram": case "rom":
                return p.getAddressFactory().getDefaultAddressSpace().getAddress(a);
            case "low": case "ramcode": {
                MemoryBlock b = p.getMemory().getBlock(space.equals("low") ? "LOW" : "RAMCODE");
                if (b == null) throw new IllegalStateException("no " + space + " block: run BuildProject first");
                return b.getStart().getAddressSpace().getAddress(a);
            }
            default:
                throw new IllegalArgumentException("unknown space " + space);
        }
    }

    public static String spaceName(Address a) {
        if (a.getAddressSpace().isOverlaySpace()) {
            String n = a.getAddressSpace().getName();
            if (n.startsWith("LOW")) return "low";
            if (n.startsWith("RAMCODE")) return "ramcode";
        }
        return a.getOffset() < 0x4000 ? "ram" : "rom";
    }
}
