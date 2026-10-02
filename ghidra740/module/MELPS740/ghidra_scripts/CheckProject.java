// Checks the project against the M3 done criteria (M3 spec §6) and writes build/ghidra740/check.txt.
//@category ghidra740
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class CheckProject extends GhidraScript {
    private static final List<String> CODE_BLOCKS = List.of("ROM_LO", "ROM", "LOW", "RAMCODE");
    private static final Set<String> NEVER_USED = Set.of("SET", "SED", "MUL", "DIV", "STP", "BRK");
    private final StringBuilder out = new StringBuilder();
    private boolean pass = true;

    @Override
    public void run() throws Exception {
        Path root = Annot.repoRoot(this);
        Path report = root.resolve("build/ghidra740/check.txt");
        Files.createDirectories(report.getParent());
        out.append("INFO 1 module: checked separately by ghidra740\\validate.bat\n");
        if (checkBuild(root)) {
            AddressSet code = new AddressSet();
            for (String b : CODE_BLOCKS) {
                MemoryBlock blk = currentProgram.getMemory().getBlock(b);
                code.add(blk.getStart(), blk.getEnd());
            }
            Path covFile = root.resolve("ghidra740/coverage/corpus.cov");
            Annot.Cov cov = Annot.loadCoverage(covFile);
            dynamicCoverage(cov, Files.exists(covFile));
            staticCoverage(code);
            indirectFlow(code, cov);
            names(code);
            census(code);
        }
        out.append(pass ? "RESULT PASS\n" : "RESULT FAIL\n");
        Files.writeString(report, out.toString(), StandardCharsets.UTF_8);
        for (String line : out.toString().split("\n"))
            if (!line.startsWith("  "))
                println(line);
    }

    private void report(int n, String name, String detail, List<String> bad) {
        if (bad.isEmpty()) {
            out.append(String.format("PASS %d %s: %s\n", n, name, detail));
            return;
        }
        pass = false;
        out.append(String.format("FAIL %d %s: %s, %d problem(s)\n", n, name, detail, bad.size()));
        for (String b : bad) out.append("  ").append(b).append('\n');
    }

    private String where(Address a) { return Annot.spaceName(a) + " " + String.format("%04X", a.getOffset()); }

    private boolean checkBuild(Path root) throws Exception {
        List<String> bad = new ArrayList<>();
        Path build = root.resolve("build/ghidra740/build.txt");
        if (!Files.exists(build)) bad.add("no build/ghidra740/build.txt: BuildProject did not run");
        else for (String l : Files.readAllLines(build, StandardCharsets.UTF_8)) if (l.startsWith("ERROR ")) bad.add(l.substring(6));
        for (String b : CODE_BLOCKS)
            if (currentProgram.getMemory().getBlock(b) == null) bad.add("missing memory block " + b);
        report(0, "build", "BuildProject report and memory blocks", bad);
        return CODE_BLOCKS.stream().allMatch(b -> currentProgram.getMemory().getBlock(b) != null);
    }

    private void dynamicCoverage(Annot.Cov cov, boolean present) {
        List<String> bad = new ArrayList<>();
        if (!present) bad.add("ghidra740/coverage/corpus.cov is absent: no dynamic coverage to check against");
        else if (cov.exec.isEmpty()) bad.add("ghidra740/coverage/corpus.cov lists no executed addresses");
        int n = 0;
        for (String[] x : cov.exec) {
            if (x[0].equals("ram")) { bad.add("ram " + x[1] + ": executed in RAM outside RAMCODE"); continue; }
            Address a = Annot.addr(currentProgram, x[0], Integer.parseInt(x[1], 16));
            n++;
            if (getInstructionAt(a) == null) bad.add(where(a) + ": not an instruction start");
            else if (getFunctionContaining(a) == null) bad.add(where(a) + ": not inside a function");
        }
        report(2, "dynamic-coverage", n + " executed addresses", bad);
    }

    private void staticCoverage(AddressSet code) throws Exception {
        List<String> bad = new ArrayList<>();
        long undefined = 0;
        for (AddressRange r : currentProgram.getListing().getUndefinedRanges(code, true, monitor)) {
            undefined += r.getLength();
            bad.add(where(r.getMinAddress()) + "-" + String.format("%04X", r.getMaxAddress().getOffset()) + ": undefined (" + r.getLength() + " bytes)");
        }
        Iterator<Bookmark> it = currentProgram.getBookmarkManager().getBookmarksIterator(BookmarkType.ERROR);
        while (it.hasNext()) {
            Bookmark b = it.next();
            bad.add(where(b.getAddress()) + ": " + b.getCategory() + ": " + b.getComment());
        }
        report(3, "static-coverage", undefined + " undefined bytes in " + String.join("/", CODE_BLOCKS), bad);
    }

    private static boolean isIndirect(int op) { return op == 0x6C || op == 0xB2 || op == 0x02; }

    /**
     * M3 spec 6.4: JMP ($4A) and the RAM jump tables (the indirect jumps with edges in the corpus) must be resolved,
     * so every target is a function; every other indirect jump needs resolved targets or a comment.
     */
    private void indirectFlow(AddressSet code, Annot.Cov cov) throws Exception {
        List<String> bad = new ArrayList<>();
        Set<String> dispatch = new HashSet<>();
        for (String[] e : cov.edges) dispatch.add(e[0] + " " + String.format("%04X", Integer.parseInt(e[1], 16)));
        int n = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(code, true);
        while (it.hasNext()) {
            Instruction in = it.next();
            int op = in.getByte(0) & 0xFF;
            if (!isIndirect(op)) continue;
            n++;
            String rule = null;
            if (op != 0x6C && (in.getByte(1) & 0xFF) == 0x4A) rule = "JMP ($4A) must be resolved";
            else if (dispatch.contains(where(in.getAddress()))) rule = "RAM jump table dispatch (edges in corpus.cov) must be resolved";
            List<Address> targets = new ArrayList<>();
            for (Reference r : in.getReferencesFrom())
                if (r.getReferenceType().isComputed()) targets.add(r.getToAddress());
            if (targets.isEmpty()) {
                if (rule != null) bad.add(where(in.getAddress()) + " " + in + ": no resolved targets (" + rule + "; a comment does not count)");
                else if (getEOLComment(in.getAddress()) == null) bad.add(where(in.getAddress()) + " " + in + ": no resolved targets and no comment");
            }
            for (Address t : targets)
                if (getFunctionAt(t) == null) bad.add(where(in.getAddress()) + " " + in + ": target " + where(t) + " is not a function entry");
        }
        report(4, "indirect-flow", n + " indirect JMP/JSR", bad);
    }

    private boolean named(Address a) {
        Symbol s = getSymbolAt(a);
        if (s != null && s.getSource() != SourceType.DEFAULT) return true;
        Data d = currentProgram.getListing().getDataContaining(a);
        Symbol ds = d == null ? null : d.getPrimarySymbol();
        return ds != null && ds.getSource() != SourceType.DEFAULT;
    }

    /** RAM base address used by (zp,X), (zp),Y, JMP/JSR (zp) or abs,X / abs,Y below $4000; null otherwise. */
    private static Integer ramBase(Instruction in) throws Exception {
        int op = in.getByte(0) & 0xFF;
        boolean zpPtr = (op & 0x1F) == 0x01 || (op & 0x1F) == 0x11 || op == 0x02 || op == 0xB2;
        boolean absIdx = (op & 0x1F) == 0x1D || (op & 0x1F) == 0x19 || op == 0x1E || op == 0x3E || op == 0x5E
            || op == 0x7E || op == 0xDE || op == 0xFE || op == 0xBC || op == 0xBE;
        if (zpPtr) return in.getByte(1) & 0xFF;
        if (absIdx) {
            int a = (in.getByte(1) & 0xFF) | (in.getByte(2) & 0xFF) << 8;
            return a < 0x4000 ? a : null;
        }
        return null;
    }

    private void names(AddressSet code) throws Exception {
        List<String> bad = new ArrayList<>();
        int functions = 0, data = 0;
        for (Function f : currentProgram.getListing().getFunctions(true)) {
            functions++;
            if (f.getSymbol().getSource() == SourceType.DEFAULT) bad.add(where(f.getEntryPoint()) + ": default function name " + f.getName());
            if (getPlateComment(f.getEntryPoint()) == null) bad.add(where(f.getEntryPoint()) + ": " + f.getName() + " has no plate comment");
        }
        DataIterator di = currentProgram.getListing().getDefinedData(code, true);
        while (di.hasNext()) {
            Data d = di.next();
            data++;
            Symbol s = d.getPrimarySymbol();
            if (s == null || s.getSource() == SourceType.DEFAULT) bad.add(where(d.getAddress()) + ": unnamed data " + d.getDataType().getName());
        }
        Set<Integer> bases = new TreeSet<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(code, true);
        while (it.hasNext()) {
            Integer b = ramBase(it.next());
            if (b != null) bases.add(b);
        }
        AddressSpace ds = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (int b : bases)
            if (!named(ds.getAddress(b))) bad.add(String.format("ram %04X: RAM pointer/buffer base has no name", b));
        report(5, "names", functions + " functions, " + data + " data items, " + bases.size() + " RAM bases", bad);
    }

    private void census(AddressSet code) {
        Map<String, Integer> counts = new TreeMap<>();
        Map<String, List<String>> where = new TreeMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(code, true);
        while (it.hasNext()) {
            Instruction in = it.next();
            String m = in.getMnemonicString().toUpperCase(Locale.ROOT).replaceAll("[0-7]$", "");
            counts.merge(m, 1, Integer::sum);
            if (NEVER_USED.contains(m)) where.computeIfAbsent(m, k -> new ArrayList<>()).add(where(in.getAddress()));
        }
        for (Map.Entry<String, Integer> e : counts.entrySet()) out.append(String.format("CENSUS %-4s %d\n", e.getKey(), e.getValue()));
        for (Map.Entry<String, List<String>> e : where.entrySet())
            out.append("FINDING 6 ").append(e.getKey()).append(" used at ").append(String.join(", ", e.getValue())).append('\n');
        report(6, "opcode-census", counts.size() + " mnemonics" + (where.isEmpty() ? ", none of SET/SED/MUL/DIV/STP/BRK" : ", see FINDING lines"), List.of());
    }
}
