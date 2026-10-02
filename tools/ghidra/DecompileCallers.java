// Headless Ghidra script: for each target (qualified-name substring, or 0x<address> of a
// function), list its callers up to a given depth and decompile targets and callers.
//
// Usage: analyzeHeadless <proj> <name> -process <prog> -noanalysis -readOnly
//          -postScript DecompileCallers.java <out.txt> <targets.txt> [depth=1]
//@category Pearl

import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DecompileCallers extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int depth = args.length > 2 ? Integer.parseInt(args[2]) : 1;
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        List<Function> roots = new ArrayList<>();
        for (String line : Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8)) {
            String t = line.strip();
            if (t.isEmpty() || t.startsWith("#")) continue;
            if (t.startsWith("0x")) {
                Address a = toAddr(Long.parseUnsignedLong(t.substring(2), 16));
                Function f = fm.getFunctionContaining(a);
                if (f != null) roots.add(f);
            } else {
                for (Function f : fm.getFunctions(true)) if (f.getName(true).contains(t)) roots.add(f);
            }
        }

        LinkedHashMap<Function, String> all = new LinkedHashMap<>();
        StringBuilder tree = new StringBuilder("# call tree (callers, depth " + depth + ")\n");
        Deque<Object[]> q = new ArrayDeque<>();
        for (Function r : roots) { all.put(r, "target"); q.add(new Object[]{r, 0}); }
        while (!q.isEmpty()) {
            Object[] e = q.poll();
            Function f = (Function) e[0];
            int d = (Integer) e[1];
            Set<Function> callers = new LinkedHashSet<>();
            for (Reference r : rm.getReferencesTo(f.getEntryPoint())) {
                Function c = fm.getFunctionContaining(r.getFromAddress());
                if (c != null) callers.add(c);
            }
            tree.append("  ".repeat(d)).append(f.getName(true)).append(" @ ").append(f.getEntryPoint())
                .append("  <- ").append(callers.size()).append(" caller(s)\n");
            if (d >= depth) continue;
            for (Function c : callers) {
                if (!all.containsKey(c)) {
                    all.put(c, "caller of " + f.getName(true));
                    q.add(new Object[]{c, d + 1});
                }
            }
        }

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        StringBuilder out = new StringBuilder(tree);
        for (Map.Entry<Function, String> e : all.entrySet()) {
            Function f = e.getKey();
            out.append("\n// ==== ").append(f.getName(true)).append(" @ ").append(f.getEntryPoint())
               .append("  [").append(e.getValue()).append("]\n");
            DecompileResults r = dec.decompileFunction(f, 180, monitor);
            out.append(r != null && r.decompileCompleted() ? r.getDecompiledFunction().getC()
                       : "// decompile failed\n");
        }
        dec.dispose();
        Files.writeString(Paths.get(args[0]), out.toString(), StandardCharsets.UTF_8);
        println("roots " + roots.size() + ", functions " + all.size() + " -> " + args[0]);
    }
}
