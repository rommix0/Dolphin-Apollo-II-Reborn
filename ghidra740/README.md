# ghidra740 — the Apollo 2 ROM in Ghidra (milestone 3)

Design: `docs/superpowers/specs/2026-10-01-m3-ghidra740-design.md`.

- `module/MELPS740/`: the MELPS 740 SLEIGH module plus the project scripts (`ghidra_scripts/`).
- `annotations/*.txt`: the source of truth. Every name, type, comment and extra seed lives here.
- `coverage/corpus.cov`: the instructions the firmware executes on the golden corpus plus our own
  `coverage/extra_*.txt` inputs.

## Entry points (from the repo root; set APOLLO2_ROM in a worktree)

| Command | Does |
|---|---|
| `ghidra740\install.bat` | Compiles the language and installs module and scripts into `%APPDATA%\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740`, with `repo.txt` naming this checkout. The installed `m740.sla` is replaced only when it changes. Restart a running Ghidra after SLEIGH changes. |
| `ghidra740\validate.bat [N]` | Checks the module against `cpu740` (N vectors per opcode, default 64). |
| `ghidra740\coverage.bat` | Regenerates `coverage/corpus.cov`. |
| `ghidra740\rebuild.bat [--format\|--roundtrip\|--reset]` | Rebuilds the headless scratch project in `build\ghidra740\project` and writes `build\ghidra740\check.txt`. Exits 0 only when every criterion passes. `--reset` reuses the program (`ResetProject.java`, as in the GUI procedure) instead of importing the ROM again, and runs the `--roundtrip` check. |
| `ghidra740\dump.bat` | Per-function disassembly and decompiled C in `build\ghidra740\dump\`. |

The bats use `GHIDRA_INSTALL_DIR` only when it holds a Ghidra (`support\analyzeHeadless.bat`), and
otherwise `C:\ghidra_12.1.3_PUBLIC`. `ghidra_env.bat` also passes the project directory in its 8.3
form, because `analyzeHeadless` rejects a path with a directory starting with `.` (a `.worktrees`
checkout).

## Labelling loop

1. `rebuild.bat`, then `dump.bat`.
2. Read `build\ghidra740\dump\` and `check.txt`, and edit the subsystem's annotation file.
3. `rebuild.bat --format`, then `rebuild.bat --roundtrip`; commit when the subsystem's offenders are gone.

## The GUI project (apollo_ghidra\, project apollo_disasm) over MCP

The `ghidra-mcp` tool `run_ghidra_script` copies one script file into `~\ghidra_scripts`, runs it
and deletes it, so a script that uses `Annot.java` does not compile through it. Run the installed
scripts through Ghidra's own script lookup instead, with `run_script_inline` and this body (the
GUI's script path includes `Extensions\MELPS740\ghidra_scripts`, where `Annot.java` sits beside them):

```java
import ghidra.app.script.GhidraScript;
public class Run740 extends GhidraScript {
    @Override
    public void run() throws Exception {
        runScript("BuildProject.java", new String[0]);   // or CheckProject.java, or ExportAnnotations.java with {"check"} / {"write"}
    }
}
```

The program is refreshed in place, never deleted and imported again: ghidra-mcp leaks a DomainFile
consumer per script run and per import or open, so Ghidra refuses to delete the program ("in use")
until it restarts. `ResetProject.java` returns the program to its just-imported state instead
(every memory block, symbol, function, comment, bookmark, reference and data type goes; the imported
file bytes and the analysis options stay), and `rebuild.bat --reset` proves that reset plus build
gives the same program as a fresh import plus build.

1. Run `ExportAnnotations.java` with `check`. Its verdict is in `build\ghidra740\export.txt`
   (`RESULT PASS|FAIL`, then `EXPORT DONE`). If the marker is missing the check did not complete:
   stop. If it says FAIL, stop: there are GUI edits; export them with `ExportAnnotations.java write`,
   review and commit them, and run the check again until it passes.
2. Run `ResetProject.java`, then `BuildProject.java`, then `CheckProject.java`: one wrapper body with
   the three `runScript` calls in that order, so an exception stops the rest. ResetProject prints
   `removed N blocks, ... program is as imported`, or throws if anything survives; then neither
   build on the program nor save it: undo it in the GUI. BuildProject must print `0 error(s)`; it
   refuses (an ERROR line) a program that is already built, so a refresh cannot build twice. It
   also turns every analyzer off in the program's analysis options, so the GUI's auto-analysis
   never touches the build.
3. Run `ExportAnnotations.java` with `check` again: it must now say `RESULT PASS`.
4. `save_program`.

First build only (no program yet): `import_file` the ROM with `language=MELPS740:LE:16:default`,
`compiler_spec=default`, `auto_analyze=false`, then steps 2 to 4.

Names made in the GUI reach git only through `ExportAnnotations.java write`. New records land in
`annotations/new.txt`: move each one to its subsystem's file. `write` deletes annotation files it
no longer produces. Edits the format cannot express are lost on the next rebuild: secondary labels,
plate comments on non-functions, names or types on unnamed data, pointer types other than `ptr`,
namespaces, and a jumptable made without the `ghidra740` bookmark (it exports as plain data).

## Annotation format

`SPACE ADDR KIND NAME TYPE [COMMENT]`, whitespace-separated; the comment is the rest of the line.

| Field | Values |
|---|---|
| SPACE | `ram` (< $4000), `rom` (>= $4000), `low` (file 0x0000-0x3FFF at $4000 while P2.7 = 1), `ramcode` ($0200-$03FE, boot copy of $ADFE) |
| KIND | `func` (comment = plate), `label`, `data`, `jumptable` (`ptr[n]`, or `ptrm1[n]` for target-1 tables), `entry` (seed, name `-`), `comment` (name `-`, text required) |
| TYPE | `byte word ptr char string[n]`, `T[n]`, struct names from `types.txt`; `-` for func/label/entry/comment |

Only a first-line `#` header survives `--format`. Name prefixes, by file:
`boot_ bank_ isr_ ser_ cmd_ tn_ lts_ ph_ pros_ par_ frm_ util_`. Padding is `unused_XXXX` in
`unused.txt`, and SFRs (no prefix) are in `sfr.txt`. RAM variables use their owner's prefix in `ram.txt`.
Committed files hold addresses, names, types and our own comments, never ROM bytes.

Direct `JSR`/`JMP` from ROM into `$0200-$03FE` are retargeted to the `RAMCODE` overlay by
`BuildProject`. Other pointer, jumptable and vector targets resolve in the default space.
