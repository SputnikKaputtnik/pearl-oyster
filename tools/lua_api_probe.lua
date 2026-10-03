-- Native API probe: runs the original story bootstrap on plain LuaJIT with logging proxies in
-- place of every native table/function, and records which native names are touched (and how).
-- Usage: oyster_lua tools/lua_api_probe.lua   (env OYSTER_ROOT = install dir, OYSTER_OUT = log)
local ROOT = os.getenv("OYSTER_ROOT") or "C:/Tools/pearl-master/steam-476540-build1340090"
local OUT = os.getenv("OYSTER_OUT") or "lua_api_probe.txt"
local FRAMES = tonumber(os.getenv("OYSTER_FRAMES") or "3")

package.path = ROOT .. "/?.lua;" .. ROOT .. "/?/init.lua"

local log = {}        -- path -> {get=n, call=n, set=n, args={sig=true}}
local order = {}
local function rec(path, kind, extra)
  local e = log[path]
  if not e then
    e = {get = 0, call = 0, set = 0, args = {}}
    log[path] = e
    order[#order + 1] = path
  end
  e[kind] = e[kind] + 1
  if extra then e.args[extra] = true end
end

local proxies = setmetatable({}, {__mode = "k"})
local function isproxy(v) return proxies[v] ~= nil end
local mt = {}
local function proxy(path)
  local p = setmetatable({}, mt)
  proxies[p] = path
  return p
end
local function sig(...)
  local n = select("#", ...)
  local t = {}
  for i = 1, n do
    local v = select(i, ...)
    if isproxy(v) then t[i] = "<" .. proxies[v] .. ">"
    elseif type(v) == "string" then t[i] = string.format("%q", #v > 40 and v:sub(1, 40) .. "..." or v)
    elseif type(v) == "number" or type(v) == "boolean" then t[i] = tostring(v)
    else t[i] = type(v) end
  end
  return table.concat(t, ", ")
end
mt.__index = function(p, k)
  local path = proxies[p] .. "." .. tostring(k)
  rec(path, "get")
  return proxy(path)
end
mt.__newindex = function(p, k, v)
  rec(proxies[p] .. "." .. tostring(k), "set")
  rawset(p, k, v)
end
mt.__call = function(p, ...)
  local path = proxies[p]
  -- method call: first arg is the proxy itself
  local a = {...}
  local s
  if isproxy(a[1]) then s = sig(select(2, ...)) else s = sig(...) end
  rec(path, "call", s)
  return proxy(path .. "()")
end
local function arith(a, b) return proxy("<arith>") end
for _, m in ipairs({"__add", "__sub", "__mul", "__div", "__mod", "__pow", "__unm"}) do mt[m] = arith end
mt.__concat = function(a, b) return tostring(a) .. tostring(b) end
mt.__tostring = function(p) return "<" .. (proxies[p] or "?") .. ">" end
mt.__len = function() return 0 end
mt.__eq = function() return false end
mt.__lt = function() return false end
mt.__le = function() return false end

-- unknown globals become proxies (native API surface)
setmetatable(_G, {__index = function(t, k)
  if k == "jit" or k == "bit" or k == "ffi" then return nil end
  rec(k, "get")
  return proxy(k)
end})

-- Platform info the host provides before scripts run (storyplayer.exe -package pearl_vrcam)
Platform = proxy("Platform")
rawset(Platform, "package", "pearl_vrcam")

local function step(name, fn, ...)
  local ok, err = xpcall(fn, debug.traceback, ...)
  print(string.format("%-28s %s", name, ok and "ok" or ("FAILED\n" .. tostring(err))))
  return ok
end

step("require story/scripts/app", function() require("story/scripts/app") end)
if rawget(_G, "Application") then
  step("Application.onInitialize", Application.onInitialize)
  step("Application.onReshape", Application.onReshape, 1280, 720)
  for i = 1, FRAMES do
    step("Application.onUpdate #" .. i, Application.onUpdate)
    step("Application.onRender #" .. i, Application.onRender)
  end
end

-- write log: top-level native tables first, then members
table.sort(order)
local f = assert(io.open(OUT, "w"))
for _, path in ipairs(order) do
  local e = log[path]
  local a = {}
  for s in pairs(e.args) do a[#a + 1] = "(" .. s .. ")" end
  table.sort(a)
  f:write(string.format("%-60s get %4d call %4d set %3d  %s\n", path, e.get, e.call, e.set,
    table.concat(a, " "):sub(1, 300)))
end
f:close()
print("native names touched: " .. #order .. " -> " .. OUT)
