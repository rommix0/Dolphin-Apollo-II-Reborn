// Returns the program to its just-imported state, so BuildProject can run on it again without deleting and
// re-importing it (the GUI cannot delete a program that ghidra-mcp still holds open; see ghidra740/README.md).
// Removes every memory block (with them the code units, symbols, functions, comments, bookmarks, references and
// overlay spaces in them), then whatever lives outside memory (symbols in other spaces, external locations,
// namespaces, classes, libraries, equates, function tags, property maps) and every data type. Keeps the imported file bytes (BuildProject builds its blocks from
// them) and the program options (analysis stays off). Throws if anything survives.
//@category ghidra740
import ghidra.app.script.GhidraScript;
import ghidra.program.database.mem.FileBytes;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;
import java.util.function.ObjLongConsumer;

public class ResetProject extends GhidraScript {
    @Override
    public void run() throws Exception {
        Program p = currentProgram;
        Memory mem = p.getMemory();
        SymbolTable st = p.getSymbolTable();
        DataTypeManager dtm = p.getDataTypeManager();
        if (!importedFile(mem))
            throw new IllegalStateException("ResetProject: expected one 64 KB imported file (UK_V2.01_4.04_ROM.BIN): not resetting");
        int blocks = mem.getBlocks().length, functions = p.getFunctionManager().getFunctionCount();
        int symbols = st.getNumSymbols(), types = dtm.getDataTypeCount(true);

        for (MemoryBlock b : mem.getBlocks())
            mem.removeBlock(b, monitor);

        // Left outside memory: anything in a space that has no blocks, and the non-address things.
        AddressSet all = new AddressSet();
        for (AddressSpace s : p.getAddressFactory().getAllAddressSpaces())
            if (s.isMemorySpace() || s.isRegisterSpace())
                all.add(s.getMinAddress(), s.getMaxAddress());
        ReferenceManager rm = p.getReferenceManager();
        for (Address a : list(rm.getReferenceSourceIterator(all, true))) rm.removeAllReferencesFrom(a);
        for (Address a : list(p.getListing().getCommentAddressIterator(all, true))) p.getListing().clearComments(a, a);
        for (Bookmark b : list(p.getBookmarkManager().getBookmarksIterator())) p.getBookmarkManager().removeBookmark(b);
        for (Address a : list(st.getExternalEntryPointIterator())) st.removeExternalEntryPoint(a);
        for (Equate e : list(p.getEquateTable().getEquates())) p.getEquateTable().removeEquate(e.getName());
        for (FunctionTag t : new ArrayList<>(p.getFunctionManager().getFunctionTagManager().getAllFunctionTags())) t.delete();
        for (String m : list(p.getUsrPropertyManager().propertyManagers())) p.getUsrPropertyManager().removePropertyMap(m);
        // External locations go through their manager (removeSymbolSpecial leaves an external function behind).
        ExternalManager em = p.getExternalManager();
        for (String lib : em.getExternalLibraryNames())
            for (ExternalLocation loc : list(em.getExternalLocations(lib))) loc.getSymbol().delete();
        // Symbols at addresses, then the namespaces, classes and libraries (no address, so getAllSymbols skips them),
        // repeated while that makes progress.
        Symbol global = p.getGlobalNamespace().getSymbol();
        for (int left = -1; st.getNumSymbols() > 0 && st.getNumSymbols() != left; ) {
            left = st.getNumSymbols();
            for (Symbol s : list(st.getAllSymbols(false))) if (!s.isDeleted()) st.removeSymbolSpecial(s);
            for (Symbol s : list(st.getChildren(global))) if (!s.isDeleted()) s.delete();
        }

        dtm.remove(list(dtm.getAllDataTypes()), monitor);
        Category root = dtm.getRootCategory();
        for (Category c : root.getCategories()) root.removeCategory(c.getName(), monitor);

        List<String> left = survivors(p);
        if (!left.isEmpty())
            throw new IllegalStateException("ResetProject: the program is only partly reset (" + String.join(", ", left)
                + "): do not build on it; undo, or delete the program and import the ROM again");
        println(String.format("ResetProject: removed %d blocks, %d functions, %d symbols, %d data types; program is as imported",
            blocks, functions, symbols, types));
    }

    private static boolean importedFile(Memory mem) {
        List<FileBytes> fbs = mem.getAllFileBytes();
        return fbs.size() == 1 && fbs.get(0).getSize() == 0x10000;
    }

    private static <T> List<T> list(Iterator<T> it) {
        List<T> out = new ArrayList<>();
        it.forEachRemaining(out::add);
        return out;
    }

    /** What a just-imported program does not have (beyond its one file-bytes record), one entry per kind. */
    private static List<String> survivors(Program p) {
        List<String> out = new ArrayList<>();
        ObjLongConsumer<String> add = (what, n) -> { if (n != 0) out.add(n + " " + what); };
        add.accept("memory blocks", p.getMemory().getBlocks().length);
        add.accept("overlay spaces", Arrays.stream(p.getAddressFactory().getAllAddressSpaces()).filter(AddressSpace::isOverlaySpace).count());
        add.accept("file bytes other than the 64 KB import", importedFile(p.getMemory()) ? 0 : 1);
        add.accept("symbols", p.getSymbolTable().getNumSymbols());
        add.accept("functions", p.getFunctionManager().getFunctionCount());
        add.accept("external functions", list(p.getFunctionManager().getExternalFunctions()).size());
        add.accept("external libraries", p.getExternalManager().getExternalLibraryNames().length);
        add.accept("entry points", list(p.getSymbolTable().getExternalEntryPointIterator()).size());
        add.accept("instructions", p.getListing().getNumInstructions());
        add.accept("data items", p.getListing().getNumDefinedData());
        add.accept("reference sources", p.getReferenceManager().getReferenceSourceCount());
        add.accept("comment addresses", p.getListing().getCommentAddressCount());
        add.accept("bookmarks", p.getBookmarkManager().getBookmarkCount());
        add.accept("equates", list(p.getEquateTable().getEquates()).size());
        add.accept("function tags", p.getFunctionManager().getFunctionTagManager().getAllFunctionTags().size());
        add.accept("property maps", list(p.getUsrPropertyManager().propertyManagers()).size());
        add.accept("relocations", p.getRelocationTable().getSize());
        add.accept("data types", p.getDataTypeManager().getDataTypeCount(true));
        add.accept("data type categories", p.getDataTypeManager().getCategoryCount() - 1);
        return out;
    }
}
