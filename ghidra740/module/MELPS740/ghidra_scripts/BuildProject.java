// Rebuilds the program from the ROM image and ghidra740/annotations (M3 spec §5.2-§5.4).
// Run on a freshly imported UK_V2.01_4.04_ROM.BIN (raw binary, MELPS740:LE:16:default, no analysis).
// Writes build/ghidra740/build.txt: ERROR lines and a SUMMARY line. CheckProject reports them as criterion 0.
//@category ghidra740
import ghidra.app.script.GhidraScript;
import ghidra.program.database.mem.FileBytes;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class BuildProject extends GhidraScript {
    private final List<String> errors = new ArrayList<>();
    private final Set<Address> functionTargets = new TreeSet<>();

    @Override
    public void run() throws Exception {
        Path root = Annot.repoRoot(this);
        Path report = root.resolve("build/ghidra740/build.txt");
        Files.createDirectories(report.getParent());
        String summary = "SUMMARY not built";
        try {
            Annot.Annotations an = Annot.load(root.resolve("ghidra740/annotations"));
            Annot.Cov cov = Annot.loadCoverage(root.resolve("ghidra740/coverage/corpus.cov"));
            buildMemory();
            Map<String, DataType> structs = createStructs(an);
            applyRecords(an, structs);
            List<Address> seeds = new ArrayList<>();
            vectorSeeds(seeds);
            recordSeeds(an, seeds);
            coverageSeeds(cov, seeds);
            for (Address a : seeds)
                if (getInstructionAt(a) == null && !disassemble(a) && getInstructionAt(a) == null)
                    errors.add("cannot disassemble seed " + Annot.spaceName(a) + " " + hex(a));
            coverageEdges(cov);
            ramcodeFlows();
            createFunctions(an);
            summary = String.format("SUMMARY %d records, %d seeds, %d functions, %d instructions",
                an.recs.size(), seeds.size(), currentProgram.getFunctionManager().getFunctionCount(),
                currentProgram.getListing().getNumInstructions());
        } catch (Exception e) {
            // One ERROR line per problem (CheckProject criterion 0 reads them): a multi-line message such as the
            // annotation loader's "annotation errors:" list is split and its header line dropped.
            String[] lines = String.valueOf(e.getMessage()).split("\\R");
            if (lines.length == 1) errors.add(e.getClass().getSimpleName() + ": " + lines[0]);
            else for (int i = 1; i < lines.length; i++) if (!lines[i].isBlank()) errors.add(lines[i].strip());
        }
        StringBuilder sb = new StringBuilder();
        for (String e : errors) sb.append("ERROR ").append(e).append('\n');
        sb.append(summary).append('\n');
        Files.writeString(report, sb.toString(), StandardCharsets.UTF_8);
        println("BuildProject: " + errors.size() + " error(s); " + summary);
        for (String e : errors) printerr("BuildProject: " + e);
    }

    private static String hex(Address a) { return String.format("%04X", a.getOffset()); }

    private void buildMemory() throws Exception {
        Memory mem = currentProgram.getMemory();
        if (mem.getBlock("ROM") != null)
            throw new IllegalStateException("already built: delete the program and import the ROM again");
        List<FileBytes> fbs = mem.getAllFileBytes();
        if (fbs.size() != 1 || fbs.get(0).getSize() != 0x10000)
            throw new IllegalStateException("expected one 64 KB imported file (UK_V2.01_4.04_ROM.BIN)");
        FileBytes fb = fbs.get(0);
        for (MemoryBlock b : mem.getBlocks())
            mem.removeBlock(b, monitor);
        AddressSpace ds = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem.createUninitializedBlock("ZP", ds.getAddress(0x0000), 0xDA, false).setPermissions(true, true, false);
        MemoryBlock sfr = mem.createUninitializedBlock("SFR", ds.getAddress(0x00DA), 0x26, false);
        sfr.setPermissions(true, true, false);
        sfr.setVolatile(true);
        mem.createUninitializedBlock("RAM", ds.getAddress(0x0100), 0x3F00, false).setPermissions(true, true, true);
        mem.createInitializedBlock("ROM_LO", ds.getAddress(0x4000), fb, 0x4000, 0x4000, false).setPermissions(true, false, true);
        mem.createInitializedBlock("ROM", ds.getAddress(0x8000), fb, 0x8000, 0x8000, false).setPermissions(true, false, true);
        mem.createInitializedBlock("LOW", ds.getAddress(0x4000), fb, 0x0000, 0x4000, true).setPermissions(true, false, true);
        mem.createInitializedBlock("RAMCODE", ds.getAddress(0x0200), fb, 0xADFE, 0x1FF, true).setPermissions(true, true, true);
    }

    private DataType primitive(String base) {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        switch (base) {
            case "byte": return ByteDataType.dataType;
            case "word": case "ptrm1": return WordDataType.dataType;
            case "ptr": return new PointerDataType(VoidDataType.dataType, 2, dtm);
            case "char": return CharDataType.dataType;
            case "string": return StringDataType.dataType;
            default: return null;
        }
    }

    private DataType dataType(Annot.TypeRef t, Map<String, DataType> structs) {
        DataType e = structs.containsKey(t.base) ? structs.get(t.base) : primitive(t.base);
        if (e == null || !t.array || t.base.equals("string"))
            return e;
        return new ArrayDataType(e, t.count, e.getLength(), currentProgram.getDataTypeManager());
    }

    private Map<String, DataType> createStructs(Annot.Annotations an) {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        Map<String, DataType> out = new HashMap<>();
        for (Annot.Struct s : an.structs.values()) {
            StructureDataType st = new StructureDataType(new CategoryPath("/" + Annot.BM_CATEGORY), s.name, s.size, dtm);
            if (!s.comment.isEmpty()) st.setDescription(s.comment);
            for (Annot.Field f : s.fields) {
                DataType dt = dataType(Annot.parseType(f.type), Map.of());
                st.replaceAtOffset(f.offset, dt, dt.getLength(), f.name, f.comment.isEmpty() ? null : f.comment);
            }
            out.put(s.name, dtm.addDataType(st, DataTypeConflictHandler.REPLACE_HANDLER));
        }
        return out;
    }

    private void applyRecords(Annot.Annotations an, Map<String, DataType> structs) {
        BookmarkManager bm = currentProgram.getBookmarkManager();
        for (Annot.Rec r : an.recs) {
            try {
                Address a = Annot.addr(currentProgram, r.space, r.addr);
                switch (r.kind) {
                    case "data":
                    case "jumptable": {
                        Annot.TypeRef t = Annot.parseType(r.type);
                        int len = t.base.equals("string") ? t.count : -1;
                        DataUtilities.createData(currentProgram, a, dataType(t, structs), len,
                            DataUtilities.ClearDataMode.CHECK_FOR_SPACE);
                        createLabel(a, r.name, true, SourceType.USER_DEFINED);
                        if (!r.comment.isEmpty()) setEOLComment(a, r.comment);
                        if (r.kind.equals("jumptable")) {
                            bm.setBookmark(a, BookmarkType.NOTE, Annot.BM_CATEGORY, t.base.equals("ptrm1") ? "jumptable-m1" : "jumptable");
                            jumptableTargets(a, t);
                        }
                        break;
                    }
                    case "label":
                        createLabel(a, r.name, true, SourceType.USER_DEFINED);
                        if (!r.comment.isEmpty()) setEOLComment(a, r.comment);
                        break;
                    case "comment":
                        setEOLComment(a, r.comment);
                        break;
                    case "entry":
                        bm.setBookmark(a, BookmarkType.NOTE, Annot.BM_CATEGORY, r.comment.isEmpty() ? "entry" : "entry: " + r.comment);
                        break;
                    default:   // func: created after disassembly
                        break;
                }
            } catch (Exception e) {
                errors.add(r.where() + ": " + e.getMessage());
            }
        }
    }

    /** Targets of a code-pointer table, in the default space. ptrm1 entries hold target - 1. */
    private void jumptableTargets(Address table, Annot.TypeRef t) throws Exception {
        AddressSpace ds = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (int i = 0; i < t.count; i++) {
            int v = currentProgram.getMemory().getShort(table.add(2L * i)) & 0xFFFF;
            if (t.base.equals("ptrm1")) {
                v = (v + 1) & 0xFFFF;
                currentProgram.getReferenceManager().addMemoryReference(table.add(2L * i), ds.getAddress(v),
                    RefType.DATA, SourceType.USER_DEFINED, 0);
            }
            functionTargets.add(ds.getAddress(v));
        }
    }

    private void vectorSeeds(List<Address> seeds) throws Exception {
        AddressSpace ds = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (int v = 0xFFF4; v <= 0xFFFE; v += 2) {
            Address t = ds.getAddress(currentProgram.getMemory().getShort(ds.getAddress(v)) & 0xFFFF);
            seeds.add(t);
            functionTargets.add(t);
        }
    }

    private void recordSeeds(Annot.Annotations an, List<Address> seeds) {
        for (Annot.Rec r : an.recs)
            if (r.kind.equals("func") || r.kind.equals("entry"))
                seeds.add(Annot.addr(currentProgram, r.space, r.addr));
        seeds.addAll(functionTargets);   // jumptable targets collected by applyRecords
    }

    private void coverageSeeds(Annot.Cov cov, List<Address> seeds) {
        for (String[] x : cov.exec) {
            if (x[0].equals("ram")) {
                errors.add("coverage: execution in RAM outside RAMCODE at " + x[1] + " (not disassembled)");
                continue;
            }
            seeds.add(Annot.addr(currentProgram, x[0], Integer.parseInt(x[1], 16)));
        }
    }

    private void coverageEdges(Annot.Cov cov) {
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (String[] e : cov.edges) {
            Address from = Annot.addr(currentProgram, e[0], Integer.parseInt(e[1], 16));
            Address to = Annot.addr(currentProgram, e[2], Integer.parseInt(e[3], 16));
            Instruction in = getInstructionAt(from);
            if (in == null) {
                errors.add("coverage edge from " + e[0] + " " + e[1] + ": no instruction");
                continue;
            }
            try {
                int op = in.getByte(0) & 0xFF;
                rm.addMemoryReference(from, to, op == 0x02 ? RefType.COMPUTED_CALL : RefType.COMPUTED_JUMP,
                    SourceType.USER_DEFINED, Reference.MNEMONIC);
                functionTargets.add(to);
            } catch (Exception ex) {
                errors.add("coverage edge " + String.join(" ", e) + ": " + ex.getMessage());
            }
        }
    }

    /** A direct JSR/JMP from ROM into $0200-$03FE targets the RAMCODE overlay (boot copies $ADFE there), not uninitialised RAM. */
    private void ramcodeFlows() throws Exception {
        MemoryBlock rc = currentProgram.getMemory().getBlock("RAMCODE");
        ReferenceManager rm = currentProgram.getReferenceManager();
        BookmarkManager bm = currentProgram.getBookmarkManager();
        AddressSpace ds = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Set<Address> targets = new TreeSet<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction in = it.next();
            if (rc.contains(in.getAddress())) continue;
            for (Reference r : in.getReferencesFrom()) {
                Address t = r.getToAddress();
                if (!r.getReferenceType().isFlow() || t.getAddressSpace() != ds || t.getOffset() < 0x200 || t.getOffset() > 0x3FE) continue;
                Address to = rc.getStart().getAddressSpace().getAddress(t.getOffset());
                rm.delete(r);
                rm.addMemoryReference(in.getAddress(), to, r.getReferenceType(), SourceType.USER_DEFINED, r.getOperandIndex());
                if (r.getReferenceType().isCall()) functionTargets.add(to);
                for (Bookmark b : bm.getBookmarks(in.getAddress(), BookmarkType.ERROR))
                    if (b.getCategory().equals("Bad Instruction")) bm.removeBookmark(b);
                targets.add(to);
            }
        }
        // The disassembler stopped at the RAM block, so a target that no seed covered is disassembled here, before
        // createFunctions makes call targets into functions (jump targets stay code inside their function, as elsewhere).
        for (Address to : targets)
            if (getInstructionAt(to) == null && !disassemble(to) && getInstructionAt(to) == null)
                errors.add("cannot disassemble RAMCODE flow target " + Annot.spaceName(to) + " " + hex(to));
    }

    private void createFunctions(Annot.Annotations an) throws Exception {
        for (Annot.Rec r : an.recs) {
            if (!r.kind.equals("func")) continue;
            Address a = Annot.addr(currentProgram, r.space, r.addr);
            if (getInstructionAt(a) == null) { errors.add(r.where() + ": no instruction at function entry"); continue; }
            Function f = getFunctionAt(a);
            if (f == null) f = createFunction(a, r.name);
            else f.setName(r.name, SourceType.USER_DEFINED);
            if (f == null) { errors.add(r.where() + ": cannot create function " + r.name); continue; }
            if (!r.comment.isEmpty()) setPlateComment(a, r.comment);
        }
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction in = it.next();
            if (in.getFlowType().isCall())
                for (Address t : in.getFlows())
                    functionTargets.add(t);
        }
        for (Address t : functionTargets)
            if (getFunctionAt(t) == null && getInstructionAt(t) != null && createFunction(t, null) == null)
                errors.add("cannot create a function at " + Annot.spaceName(t) + " " + hex(t));
    }
}
