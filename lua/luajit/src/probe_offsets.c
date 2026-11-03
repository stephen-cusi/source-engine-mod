#include <stdio.h>
#include <stddef.h>
#define LUAJIT_ENABLE_GC64 1
#include "lua.h"
#include "lauxlib.h"
#include "lj_obj.h"

int main() {
    printf("LJ_GC64: %d\n", (int)LJ_GC64);
    printf("sizeof(MRef): %zu\n", sizeof(MRef));
    printf("sizeof(GCRef): %zu\n", sizeof(GCRef));
    printf("sizeof(TValue): %zu\n", sizeof(TValue));
    printf("sizeof(lua_State): 0x%zx (%zu)\n", sizeof(lua_State), sizeof(lua_State));
    printf("offsetof cframe: 0x%zx\n", offsetof(lua_State, cframe));
    printf("offsetof stacksize: 0x%zx\n", offsetof(lua_State, stacksize));
    printf("offsetof gm_reserved: 0x%zx\n", offsetof(lua_State, gm_reserved));
    printf("offsetof luabase: 0x%zx\n", offsetof(lua_State, luabase));
    return 0;
}
