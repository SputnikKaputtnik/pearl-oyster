// Headless Ghidra script: decompile every function whose (demangled, namespace-qualified)
// name contains one of the given substrings.
//
// Usage: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//          -postScript DecompileByName.java <out.txt> <names.txt>
//   names.txt: one substring per line, e.g. "LApplication::onParseCommandLine"; '#' = comment.
//@category Pearl

import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

public class DecompileByName extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        List<String> wanted = new ArrayList<>();
        for (String line : Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8)) {
            String t = line.strip();
            if (!t.isEmpty() && !t.startsWith("#")) wanted.add(t);
        }
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            String q = f.getName(true);
            String hit = null;
            for (String w : wanted) if (q.contains(w)) { hit = w; break; }
            if (hit == null) continue;
            n++;
            out.append("\n// ==== ").append(q).append(" @ ").append(f.getEntryPoint())
               .append("  [match: ").append(hit).append("]\n");
            DecompileResults r = dec.decompileFunction(f, 180, monitor);
            out.append(r != null && r.decompileCompleted() ? r.getDecompiledFunction().getC()
                       : "// decompile failed\n");
        }
        dec.dispose();
        Files.writeString(Paths.get(args[0]), out.toString(), StandardCharsets.UTF_8);
        println("decompiled " + n + " functions -> " + args[0]);
    }
}
