// Headless Ghidra script: for each class name, find its `vftable` symbol (from RTTI), read the
// first N function pointers and decompile them.
//
// Usage: ... -postScript DumpVtable.java <out.txt> <Class1+Class2+...> [N=16]
//@category Pearl

import java.nio.charset.StandardCharsets;
import java.nio.file.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DumpVtable extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int n = args.length > 2 ? Integer.parseInt(args[2]) : 16;
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        for (String cls : args[1].split("[,+]")) {
            Address vt = null;
            for (Symbol s : st.getSymbols("vftable")) {
                if (s.getParentNamespace().getName().equals(cls)) { vt = s.getAddress(); break; }
            }
            out.append("# class ").append(cls).append(" vftable @ ").append(vt).append("\n");
            if (vt == null) continue;
            for (int i = 0; i < n; i++) {
                long ptr = getLong(vt.add(8L * i));
                Address fa = toAddr(ptr);
                Function f = fm.getFunctionAt(fa);
                out.append("  slot +").append(Integer.toHexString(8 * i)).append(" -> ").append(fa)
                   .append(" ").append(f == null ? "<none>" : f.getName(true)).append("\n");
                if (f == null) break;
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                out.append("// ==== ").append(cls).append(" slot +").append(Integer.toHexString(8 * i))
                   .append(" ").append(f.getName(true)).append(" @ ").append(fa).append("\n");
                out.append(r != null && r.decompileCompleted() ? r.getDecompiledFunction().getC()
                           : "// decompile failed\n");
            }
        }
        dec.dispose();
        Files.writeString(Paths.get(args[0]), out.toString(), StandardCharsets.UTF_8);
        println("wrote " + args[0]);
    }
}
