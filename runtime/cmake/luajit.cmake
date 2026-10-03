# Builds LuaJIT 2.1 (official source, user-supplied checkout in LUAJIT_DIR) as a static library,
# mirroring the steps of src/msvcbuild.bat with the current C compiler. Generated files go to
# the build directory; the LuaJIT source tree is left untouched.
#
# Host build only (desktop). Android cross builds will need a host-compiled buildvm with
# target DynASM flags (TODO when the Quest target is added).

set(LUAJIT_DIR "C:/Tools/luajit" CACHE PATH "LuaJIT 2.1 source checkout")
set(_lj_src "${LUAJIT_DIR}/src")
set(_lj_gen "${CMAKE_BINARY_DIR}/luajit-gen")
file(MAKE_DIRECTORY "${_lj_gen}" "${_lj_gen}/jit")

if(NOT EXISTS "${_lj_src}/lj_api.c")
  message(FATAL_ERROR "LuaJIT source not found in ${LUAJIT_DIR}")
endif()

if(WIN32)
  set(_lj_dasmflags -D WIN -D JIT -D FFI -D ENDIAN_LE -D FPU -D P64)
  set(_lj_vmobjmode peobj)
  set(_lj_vmobj "${_lj_gen}/lj_vm.o")
else()
  set(_lj_dasmflags -D JIT -D FFI -D ENDIAN_LE -D FPU -D P64)
  set(_lj_vmobjmode elfasm)
  set(_lj_vmobj "${_lj_gen}/lj_vm.S")
endif()
set(_lj_all_lib lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c
    lib_package.c lib_debug.c lib_jit.c lib_ffi.c lib_buffer.c)

function(_lj_run)
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${_lj_src}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "LuaJIT build step failed: ${ARGN}\n${out}\n${err}")
  endif()
endfunction()

if(NOT EXISTS "${_lj_gen}/lj_folddef.h")
  message(STATUS "Generating LuaJIT VM in ${_lj_gen}")
  set(_cc "${CMAKE_C_COMPILER}")
  _lj_run("${_cc}" -O2 host/minilua.c -o "${_lj_gen}/minilua${CMAKE_EXECUTABLE_SUFFIX}" -lm)
  _lj_run("${_lj_gen}/minilua" ../dynasm/dynasm.lua -LN ${_lj_dasmflags} -o "${_lj_gen}/buildvm_arch.h" vm_x64.dasc)
  execute_process(COMMAND git -C "${LUAJIT_DIR}" show -s --format=%ct OUTPUT_FILE "${_lj_gen}/luajit_relver.txt")
  _lj_run("${_lj_gen}/minilua" host/genversion.lua luajit_rolling.h "${_lj_gen}/luajit_relver.txt" "${_lj_gen}/luajit.h")
  file(GLOB _lj_buildvm_src "${_lj_src}/host/buildvm*.c")
  _lj_run("${_cc}" -O2 -I. -I "${_lj_gen}" -I ../dynasm ${_lj_buildvm_src} -o "${_lj_gen}/buildvm${CMAKE_EXECUTABLE_SUFFIX}")
  set(_bvm "${_lj_gen}/buildvm")
  _lj_run("${_bvm}" -m ${_lj_vmobjmode} -o "${_lj_vmobj}")
  foreach(m bcdef ffdef libdef recdef)
    _lj_run("${_bvm}" -m ${m} -o "${_lj_gen}/lj_${m}.h" ${_lj_all_lib})
  endforeach()
  _lj_run("${_bvm}" -m vmdef -o "${_lj_gen}/jit/vmdef.lua" ${_lj_all_lib})
  _lj_run("${_bvm}" -m folddef -o "${_lj_gen}/lj_folddef.h" lj_opt_fold.c)
endif()

# like msvcbuild.bat: all lj_*.c and lib_*.c (incl. lib_aux.c / lib_init.c)
file(GLOB _lj_core "${_lj_src}/lj_*.c")
file(GLOB _lj_libs "${_lj_src}/lib_*.c")

# Table iteration order of the original (LuaJIT 2.0.4): 2.0 places string keys by their content
# hash (seed-free lookup3 variant), 2.1 by an interned string id, randomised by default. Story
# scripts iterate tables with pairs() (e.g. Story:findDef), so the order must be deterministic
# and should equal the original's. A patched copy of lj_str.c (build dir only) sets the string
# id to the 2.0 hash; the security randomisation is disabled. 2.1 is kept for ARM64 (Quest).
option(OYSTER_LUAJIT20_TABLE_ORDER "LuaJIT 2.0 compatible, deterministic table order" ON)
if(OYSTER_LUAJIT20_TABLE_ORDER)
  file(READ "${_lj_src}/lj_str.c" _lj_str)
  string(REPLACE "#ifndef STRID_RESEED_INTERVAL\n  s->sid = g->str.id++;"
                 "#ifndef STRID_RESEED_INTERVAL\n  s->sid = (StrID)hash;  /* Oyster: LuaJIT 2.0 table order */\n  g->str.id++;"
                 _lj_str "${_lj_str}")
  string(REPLACE "g->str.seed = lj_prng_u64(&g->prng);"
                 "(void)lj_prng_u64(&g->prng);\n  g->str.seed = 0;  /* Oyster: LuaJIT 2.0 string hash */"
                 _lj_str "${_lj_str}")
  string(FIND "${_lj_str}" "Oyster: LuaJIT 2.0 table order" _p1)
  string(FIND "${_lj_str}" "Oyster: LuaJIT 2.0 string hash" _p2)
  if(_p1 EQUAL -1 OR _p2 EQUAL -1)
    message(FATAL_ERROR "lj_str.c changed upstream: LuaJIT 2.0 table order patch does not apply")
  endif()
  file(MAKE_DIRECTORY "${_lj_gen}/patched")
  file(WRITE "${_lj_gen}/patched/lj_str.c" "${_lj_str}")
  list(REMOVE_ITEM _lj_core "${_lj_src}/lj_str.c")
  list(APPEND _lj_core "${_lj_gen}/patched/lj_str.c")
endif()

enable_language(C)
add_library(luajit STATIC ${_lj_core} ${_lj_libs} "${_lj_vmobj}")
# generated headers first (luajit.h, lj_*def.h), then the source tree
target_include_directories(luajit BEFORE PUBLIC "${_lj_gen}" "${_lj_src}")
target_compile_options(luajit PRIVATE -O2 -fomit-frame-pointer -w)
if(OYSTER_LUAJIT20_TABLE_ORDER)
  target_compile_definitions(luajit PRIVATE LUAJIT_SECURITY_PRNG=0 LUAJIT_SECURITY_STRHASH=0
                                            LUAJIT_SECURITY_STRID=0)
endif()
set_source_files_properties("${_lj_vmobj}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
