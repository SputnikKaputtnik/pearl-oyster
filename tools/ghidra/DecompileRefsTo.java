// Headless Ghidra script: decompile every function that references one of the given addresses
// (data or code), e.g. the storage of an engine shader constant.
//
// Usage: analyzeHeadless <proj> <name> -process <prog> -noanalysis
//          -postScript DecompileRefsTo.java <out.txt> <addr> [<addr> ...]
//@category Pearl

import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DecompileRefsTo extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        Set<Function> done = new LinkedHashSet<>();
        for (int i = 1; i < args.length; i++) {
            Address a = toAddr(args[i]);
            out.append("// refs to ").append(a).append(":\n");
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
                Function f = getFunctionContaining(r.getFromAddress());
                out.append("//   from ").append(r.getFromAddress()).append(" ").append(r.getReferenceType())
                   .append(" in ").append(f == null ? "?" : f.getName(true)).append("\n");
                if (f != null) done.add(f);
            }
        }
        for (Function f : done) {
            DecompileResults res = dec.decompileFunction(f, 120, monitor);
            out.append("\n// ==== ").append(f.getName(true)).append(" @ ").append(f.getEntryPoint()).append("\n");
            if (res != null && res.decompileCompleted()) out.append(res.getDecompiledFunction().getC());
        }
        Files.write(Paths.get(args[0]), out.toString().getBytes(StandardCharsets.UTF_8));
        println("functions: " + done.size());
    }
}
