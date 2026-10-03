// oyster_lua: runs a Lua file (or a chunk with -e) on the embedded LuaJIT; smoke test and
// scratch tool for the upcoming native API layer.
#include <cstdio>
#include <cstring>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "luajit.h"
#include "lualib.h"
}

int main(int argc, char** argv) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    int rc = 0;
    if (argc >= 3 && std::strcmp(argv[1], "-e") == 0) rc = luaL_dostring(L, argv[2]);
    else if (argc >= 2) rc = luaL_dofile(L, argv[1]);
    else rc = luaL_dostring(L, "print(jit.version, jit.arch, jit.status())");
    if (rc != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_close(L);
    return 0;
}
