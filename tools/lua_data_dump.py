#!/usr/bin/env python3
"""Evaluate a Spotlight Stories package's *data* scripts in a sandboxed Lua 5.1 and
dump the resulting `def` table as JSON.

Only the declarative data files (<package>/scripts/data/*.lua, pearlpackage/rendergraphs)
are executed. Engine types (Vector3, Quaternion, Color, ...) and engine constants
(RTA_COLOR0, PF_RGBA8, ...) are replaced by stubs that record their name/arguments, so
nothing from the original runtime is needed or run.

Usage:
    lua_data_dump.py <install_root> <package> <out.json> [entry]
      entry defaults to "<package>/scripts/data/package"
Requires: pip install lupa
"""
import json
import os
import sys

from lupa import lua51

SANDBOX = r"""
local root, package = ...
local loaded = {}
local env = {}
local function ctor(name)
  return function(...) return { __type = name, args = { ... } } end
end
for _, n in ipairs({"Vector2","Vector3","Vector4","Quaternion","Color","Matrix3","Matrix4"}) do
  env[n] = ctor(n)
end
env.bit = { lshift = function(a, b) return a * 2^b end,
            rshift = function(a, b) return math.floor(a / 2^b) end,
            bor = function(...) local r = 0; for _, v in ipairs({...}) do r = r + v end; return r end }
env.Platform = { package = package, android = false, windows = true }
env.math, env.string, env.table, env.pairs, env.ipairs, env.type, env.tostring, env.tonumber =
  math, string, table, pairs, ipairs, type, tostring, tonumber
env.print = print
env.require = function(name)
  if loaded[name] then return end
  loaded[name] = true
  local path = root .. "/" .. name .. ".lua"
  local f, err = loadfile(path)
  if not f then error("require " .. name .. ": " .. tostring(err)) end
  setfenv(f, env)
  f()
end
-- Unknown globals: ALL_CAPS -> symbolic constant; anything else -> nil.
setmetatable(env, { __index = function(t, k)
  if type(k) == "string" and k:match("^[A-Z][A-Z0-9_]+$") then return { __const = k } end
  return nil
end })
env.def = {}
return env
"""


def to_py(obj, depth=0, seen=None):
    """Convert a Lua table to JSON-able Python. Shared sub-tables are expanded each time;
    true cycles are cut and marked."""
    if seen is None:
        seen = set()
    if lua51.lua_type(obj) != "table":
        return obj
    if id(obj) in seen or depth > 64:
        return {"__cycle": True}
    seen = seen | {id(obj)}
    keys = list(obj.keys())
    if keys and all(isinstance(k, int) for k in keys) and sorted(keys) == list(range(1, len(keys) + 1)):
        return [to_py(obj[k], depth + 1, seen) for k in sorted(keys)]
    return {str(k): to_py(obj[k], depth + 1, seen) for k in keys}


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    root, package, out = argv[1], argv[2], argv[3]
    entry = argv[4] if len(argv) > 4 else f"{package}/scripts/data/package"
    lua = lua51.LuaRuntime(unpack_returned_tuples=True)
    env = lua.execute(SANDBOX, root.replace("\\", "/"), package)
    env.require(entry)
    data = to_py(env["def"])
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1, sort_keys=True)
    print(f"{out}: top-level keys: {sorted(data.keys())}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
