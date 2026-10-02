// Headless Ghidra script: find exact NUL-terminated strings in the program, list every
// reference to them, and decompile each referencing function once.
//
// Usage (analyzeHeadless ... -postScript DumpStringUsers.java <out.txt> <strings.txt>)
//   strings.txt: one literal per line (e.g. "-rendertodisk"); '#' lines are comments.
// Output: per string its addresses and referencing functions, then the decompiled C of all
// referencing functions (deduplicated).
//@category Pearl

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class DumpStringUsers extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        List<String> wanted = new ArrayList<>();
        for (String line : Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8)) {
            String t = line.strip();
            if (!t.isEmpty() && !t.startsWith("#")) wanted.add(t);
        }

        Memory mem = currentProgram.getMemory();
        ReferenceManager refs = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        Map<Function, Set<String>> users = new LinkedHashMap<>();
        StringBuilder out = new StringBuilder();
        out.append("# program: ").append(currentProgram.getName())
           .append("  image base ").append(currentProgram.getImageBase()).append("\n\n");

        for (String s : wanted) {
            byte[] pat = (s + "\0").getBytes(StandardCharsets.ISO_8859_1);
            out.append("## \"").append(s).append("\"\n");
            Address start = mem.getMinAddress();
            int hits = 0;
            while (start != null) {
                Address a = mem.findBytes(start, pat, null, true, monitor);
                if (a == null) break;
                start = a.add(1);
                // require a string boundary before the match
                try {
                    if (a.getOffset() > 0 && mem.getByte(a.subtract(1)) != 0) continue;
                } catch (Exception e) { /* start of block */ }
                hits++;
                out.append("  @").append(a);
                List<String> fnames = new ArrayList<>();
                for (Reference r : refs.getReferencesTo(a)) {
                    Address from = r.getFromAddress();
                    Function f = fm.getFunctionContaining(from);
                    String fn = f == null ? "<no function>" : f.getName() + "@" + f.getEntryPoint();
                    fnames.add(from + " in " + fn);
                    if (f != null) users.computeIfAbsent(f, k -> new LinkedHashSet<>()).add(s);
                }
                out.append(fnames.isEmpty() ? "  (no references)" : "  refs: " + fnames).append("\n");
            }
            if (hits == 0) out.append("  not found\n");
        }

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        out.append("\n\n# ===== decompiled referencing functions (").append(users.size()).append(") =====\n");
        for (Map.Entry<Function, Set<String>> e : users.entrySet()) {
            Function f = e.getKey();
            out.append("\n// ---- ").append(f.getName()).append(" @ ").append(f.getEntryPoint())
               .append("  uses: ").append(e.getValue()).append("\n");
            DecompileResults res = dec.decompileFunction(f, 120, monitor);
            if (res != null && res.decompileCompleted()) {
                out.append(res.getDecompiledFunction().getC());
            } else {
                out.append("// decompile failed: ").append(res == null ? "null" : res.getErrorMessage()).append("\n");
            }
        }
        dec.dispose();
        Files.writeString(Paths.get(outPath), out.toString(), StandardCharsets.UTF_8);
        println("wrote " + outPath + " (" + users.size() + " functions)");
    }
}
