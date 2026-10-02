// Checks the MELPS740 module against cpu740 (M3 spec §4): build/ghidra740/vectors.txt in,
// build/ghidra740/validate.txt out. Run headless on a blank program (ghidra740\validate.bat).
//@category ghidra740
import ghidra.app.emulator.EmulatorHelper;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.*;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class ValidateModule extends GhidraScript {
    private static final String[] FLAG_BITS = { "C", "Z", "I", "D", "B", "T", "V", "N" };   // P bit 0..7
    private static final Pattern NUM = Pattern.compile("0X([0-9A-F]+)|\\$([0-9A-F]+)");
    private final List<String> mismatches = new ArrayList<>();
    private int executed, decodeOnly;

    @Override
    public void run() throws Exception {
        Path root = Annot.repoRoot(this);
        Path in = root.resolve("build/ghidra740/vectors.txt");
        Path out = root.resolve("build/ghidra740/validate.txt");
        PseudoDisassembler pd = new PseudoDisassembler(currentProgram);
        AddressSpace ram = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (String line : Files.readAllLines(in, StandardCharsets.US_ASCII)) {
            if (line.isBlank() || line.startsWith("#"))
                continue;
            monitor.checkCancelled();
            check(line, pd, ram);
        }
        StringBuilder sb = new StringBuilder();
        for (String m : mismatches)
            sb.append(m).append('\n');
        sb.append(String.format("executed %d, decode-only %d, mismatches %d%n", executed, decodeOnly, mismatches.size()));
        sb.append(mismatches.isEmpty() ? "RESULT PASS\n" : "RESULT FAIL\n");
        Files.writeString(out, sb.toString(), StandardCharsets.US_ASCII);
        println("ValidateModule: " + (mismatches.isEmpty() ? "PASS" : mismatches.size() + " mismatches") + " -> " + out);
    }

    /** Case, spacing and the "\" of JSR \$FFxx removed; every hex number ($12, 0x12) rewritten in decimal. */
    static String canon(String s) {
        String u = s.toUpperCase(Locale.ROOT).replace("\\", "");
        Matcher m = NUM.matcher(u);
        StringBuilder sb = new StringBuilder();
        while (m.find()) {
            String h = m.group(1) != null ? m.group(1) : m.group(2);
            m.appendReplacement(sb, Long.toString(Long.parseLong(h, 16)));
        }
        m.appendTail(sb);
        return sb.toString().replaceAll("\\s+", "");
    }

    private static Map<String, String> fields(String part) {
        Map<String, String> f = new HashMap<>();
        for (String tok : part.trim().split("\\s+")) {
            int eq = tok.indexOf('=');
            if (eq > 0)
                f.put(tok.substring(0, eq), tok.substring(eq + 1));
        }
        return f;
    }

    private static int hex(String s) { return Integer.parseInt(s, 16); }

    private static byte[] bytes(String list) {
        String[] p = list.split(",");
        byte[] b = new byte[p.length];
        for (int i = 0; i < p.length; i++)
            b[i] = (byte) hex(p[i]);
        return b;
    }

    /** "AAAA:VV,AAAA:VV" -> ordered map; empty string -> empty map. */
    private static Map<Integer, Integer> pairs(String list) {
        Map<Integer, Integer> m = new LinkedHashMap<>();
        if (list == null || list.isEmpty())
            return m;
        for (String p : list.split(",")) {
            String[] kv = p.split(":");
            m.put(hex(kv[0]), hex(kv[1]));
        }
        return m;
    }

    private void check(String line, PseudoDisassembler pd, AddressSpace ram) throws Exception {
        String[] parts = line.split(" \\| ");
        Map<String, String> pre = fields(parts[0]);
        int pc = hex(pre.get("pc"));
        byte[] code = bytes(pre.get("op"));
        String want = parts[parts.length - 1].trim();
        Address at = ram.getAddress(pc);
        String got;
        try {
            PseudoInstruction ins = pd.disassemble(at, code);
            got = ins == null ? null : ins.toString();
        } catch (Exception e) {
            got = null;
        }
        boolean illegal = want.startsWith("DB ");
        if (illegal ? got != null : got == null || !canon(got).equals(canon(want)))
            mismatches.add(String.format("DISASM %s: ghidra \"%s\", cpu740 \"%s\"", pre.get("op"), got, want));
        if (line.startsWith("D ")) {
            decodeOnly++;
            return;
        }
        executed++;
        emulate(line, pre, fields(parts[1]), code, at, pc, ram);
    }

    private void emulate(String line, Map<String, String> pre, Map<String, String> post, byte[] code,
                         Address at, int pc, AddressSpace ram) throws Exception {
        EmulatorHelper emu = new EmulatorHelper(currentProgram);
        try {
            for (Map.Entry<Integer, Integer> m : pairs(pre.get("r")).entrySet())
                emu.writeMemoryValue(ram.getAddress(m.getKey()), 1, m.getValue());
            emu.writeMemory(at, code);
            emu.writeRegister("A", hex(pre.get("a")));
            emu.writeRegister("X", hex(pre.get("x")));
            emu.writeRegister("Y", hex(pre.get("y")));
            emu.writeRegister("SP", 0x100 | hex(pre.get("s")));
            int p = hex(pre.get("p"));
            for (int b = 0; b < 8; b++)
                emu.writeRegister(FLAG_BITS[b], (p >> b) & 1);
            emu.writeRegister(emu.getPCRegister(), pc);
            emu.enableMemoryWriteTracking(true);
            if (!emu.step(monitor)) {
                mismatches.add("EMU " + pre.get("op") + ": step failed: " + emu.getLastError() + " | " + line);
                return;
            }
            List<String> diffs = new ArrayList<>();
            cmp(diffs, "A", emu.readRegister("A").intValue(), hex(post.get("A")));
            cmp(diffs, "X", emu.readRegister("X").intValue(), hex(post.get("X")));
            cmp(diffs, "Y", emu.readRegister("Y").intValue(), hex(post.get("Y")));
            int sp = emu.readRegister("SP").intValue();
            cmp(diffs, "S", sp & 0xFF, hex(post.get("S")));
            cmp(diffs, "SH", sp >> 8, 1);
            int gp = 0;
            for (int b = 0; b < 8; b++)
                gp |= (emu.readRegister(FLAG_BITS[b]).intValue() & 1) << b;
            cmp(diffs, "P", gp, hex(post.get("P")));
            cmp(diffs, "PC", emu.readRegister(emu.getPCRegister()).intValue(), hex(post.get("PC")));
            Map<Integer, Integer> wantW = pairs(post.get("w"));
            Set<Integer> gotW = new TreeSet<>();
            for (AddressRange r : emu.getTrackedMemoryWriteSet())
                for (Address a = r.getMinAddress(); a != null && a.compareTo(r.getMaxAddress()) <= 0; a = a.next())
                    if (a.getAddressSpace().equals(ram))
                        gotW.add((int) a.getOffset());
            if (!gotW.equals(wantW.keySet()))
                diffs.add("writes at " + hexSet(gotW) + " want " + hexSet(wantW.keySet()));
            for (Map.Entry<Integer, Integer> w : wantW.entrySet())
                cmp(diffs, String.format("mem[%04X]", w.getKey()),
                    emu.readMemoryByte(ram.getAddress(w.getKey())) & 0xFF, w.getValue());
            if (!diffs.isEmpty())
                mismatches.add("EMU " + pre.get("op") + ": " + String.join(", ", diffs) + " | " + line);
        } finally {
            emu.dispose();
        }
    }

    private static void cmp(List<String> diffs, String what, int got, int want) {
        if (got != want)
            diffs.add(String.format("%s got %X want %X", what, got, want));
    }

    private static String hexSet(Collection<Integer> s) {
        StringBuilder sb = new StringBuilder("{");
        for (int v : s)
            sb.append(sb.length() > 1 ? "," : "").append(String.format("%04X", v));
        return sb.append('}').toString();
    }
}
