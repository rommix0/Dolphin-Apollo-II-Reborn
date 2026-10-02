// Writes per-function disassembly and decompiled C plus indexes to build/ghidra740/dump/ (M3 spec §7),
// for the labelling loop. Read-only.
//@category ghidra740
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

public class DumpProject extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path out = Annot.repoRoot(this).resolve("build/ghidra740/dump");
        Files.createDirectories(out);
        try (Stream<Path> s = Files.list(out)) {
            for (Path p : s.collect(Collectors.toList())) Files.delete(p);
        }
        Listing listing = currentProgram.getListing();
        CodeUnitFormat fmt = new CodeUnitFormat(new CodeUnitFormatOptions());
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        StringBuilder index = new StringBuilder("# space addr  size name | called by | calls\n");
        for (Function f : listing.getFunctions(true)) {
            monitor.checkCancelled();
            Address e = f.getEntryPoint();
            String tag = Annot.spaceName(e) + "_" + String.format("%04X", e.getOffset());
            String callers = names(f.getCallingFunctions(monitor)), callees = names(f.getCalledFunctions(monitor));
            index.append(String.format("%-7s %04X %5d %s | %s | %s%n", Annot.spaceName(e), e.getOffset(),
                f.getBody().getNumAddresses(), f.getName(), callers, callees));
            StringBuilder sb = new StringBuilder();
            sb.append("// ").append(f.getName()).append(" @ ").append(tag).append('\n');
            String plate = getPlateComment(e);
            if (plate != null) sb.append("// ").append(plate.replace("\n", "\n// ")).append('\n');
            sb.append("// called by: ").append(callers).append("\n// calls: ").append(callees).append("\n\n");
            for (CodeUnit cu : listing.getCodeUnits(f.getBody(), true)) {
                for (Symbol s : currentProgram.getSymbolTable().getSymbols(cu.getAddress()))
                    sb.append(s.getName()).append(":\n");
                String eol = cu.getComment(CommentType.EOL);
                sb.append(String.format("  %04X  %-9s %s%s%n", cu.getAddress().getOffset(), bytes(cu),
                    fmt.getRepresentationString(cu), eol == null ? "" : "    ; " + eol));
            }
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            sb.append("\n/* decompiled */\n").append(r.decompileCompleted() && r.getDecompiledFunction() != null
                ? r.getDecompiledFunction().getC() : "/* decompile failed: " + r.getErrorMessage() + " */\n");
            Files.writeString(out.resolve(tag + "_" + f.getName() + ".txt"), sb.toString(), StandardCharsets.UTF_8);
        }
        di.dispose();
        Files.writeString(out.resolve("functions.txt"), index.toString(), StandardCharsets.UTF_8);

        StringBuilder data = new StringBuilder("# space addr  len type name ; comment | referenced from\n");
        DataIterator dit = listing.getDefinedData(true);
        while (dit.hasNext()) {
            Data d = dit.next();
            String eol = d.getComment(CommentType.EOL);
            data.append(String.format("%-7s %04X %5d %-14s %s%s | %s%n", Annot.spaceName(d.getAddress()), d.getAddress().getOffset(),
                d.getLength(), d.getDataType().getName(), d.getLabel() == null ? "-" : d.getLabel(),
                eol == null ? "" : " ; " + eol, refs(d.getAddress())));
        }
        Files.writeString(out.resolve("data.txt"), data.toString(), StandardCharsets.UTF_8);

        AddressSet code = new AddressSet();
        for (String b : List.of("ROM_LO", "ROM", "LOW", "RAMCODE")) {
            MemoryBlock blk = currentProgram.getMemory().getBlock(b);
            code.add(blk.getStart(), blk.getEnd());
        }
        StringBuilder undef = new StringBuilder("# space start-end len | first bytes | referenced from (first address)\n");
        for (AddressRange r : listing.getUndefinedRanges(code, true, monitor)) {
            byte[] b = new byte[(int) Math.min(16, r.getLength())];
            currentProgram.getMemory().getBytes(r.getMinAddress(), b);
            StringBuilder hx = new StringBuilder();
            for (byte x : b) hx.append(String.format("%02X ", x & 0xFF));
            undef.append(String.format("%-7s %04X-%04X %5d | %s| %s%n", Annot.spaceName(r.getMinAddress()), r.getMinAddress().getOffset(),
                r.getMaxAddress().getOffset(), r.getLength(), hx, refs(r.getMinAddress())));
        }
        Files.writeString(out.resolve("undefined.txt"), undef.toString(), StandardCharsets.UTF_8);
        println("DumpProject: wrote " + out);
    }

    private static String names(Set<Function> fs) {
        return fs.stream().map(Function::getName).sorted().collect(Collectors.joining(" "));
    }

    private static String bytes(CodeUnit cu) throws Exception {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < Math.min(cu.getLength(), 3); i++) sb.append(String.format("%02X", cu.getByte(i) & 0xFF));
        return sb.toString();
    }

    private String refs(Address a) {
        List<String> r = new ArrayList<>();
        for (Reference ref : currentProgram.getReferenceManager().getReferencesTo(a)) {
            if (r.size() == 8) { r.add("..."); break; }
            r.add(Annot.spaceName(ref.getFromAddress()) + ":" + String.format("%04X", ref.getFromAddress().getOffset()));
        }
        return String.join(" ", r);
    }
}
