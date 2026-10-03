# Builds libogg 1.3.6 + libvorbis/libvorbisfile 1.3.7 (official xiph sources, checkouts in
# OGG_DIR / VORBIS_DIR) as static libraries for every platform, so desktop and Quest decode the
# Ogg Vorbis audio with the same code. Generated headers go to the build directory.

set(OGG_DIR "C:/Tools/ogg" CACHE PATH "libogg source checkout (v1.3.6)")
set(VORBIS_DIR "C:/Tools/vorbis" CACHE PATH "libvorbis source checkout (v1.3.7)")
if(NOT EXISTS "${OGG_DIR}/src/framing.c" OR NOT EXISTS "${VORBIS_DIR}/lib/vorbisfile.c")
  message(FATAL_ERROR "libogg/libvorbis sources not found in ${OGG_DIR} / ${VORBIS_DIR}")
endif()

set(_ogg_gen "${CMAKE_BINARY_DIR}/ogg-gen")
set(INCLUDE_INTTYPES_H 1)
set(INCLUDE_STDINT_H 1)
set(INCLUDE_SYS_TYPES_H 1)
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)
configure_file("${OGG_DIR}/include/ogg/config_types.h.in" "${_ogg_gen}/ogg/config_types.h" @ONLY)

add_library(ogg STATIC "${OGG_DIR}/src/bitwise.c" "${OGG_DIR}/src/framing.c")
target_include_directories(ogg PUBLIC "${_ogg_gen}" "${OGG_DIR}/include")
target_compile_options(ogg PRIVATE -w)

set(_vb "${VORBIS_DIR}/lib")
add_library(vorbis STATIC
  ${_vb}/mdct.c ${_vb}/smallft.c ${_vb}/block.c ${_vb}/envelope.c ${_vb}/window.c ${_vb}/lsp.c
  ${_vb}/lpc.c ${_vb}/analysis.c ${_vb}/synthesis.c ${_vb}/psy.c ${_vb}/info.c ${_vb}/floor1.c
  ${_vb}/floor0.c ${_vb}/res0.c ${_vb}/mapping0.c ${_vb}/registry.c ${_vb}/codebook.c
  ${_vb}/sharedbook.c ${_vb}/lookup.c ${_vb}/bitrate.c)
target_include_directories(vorbis PUBLIC "${VORBIS_DIR}/include" PRIVATE "${_vb}")
target_link_libraries(vorbis PUBLIC ogg)
target_compile_options(vorbis PRIVATE -w)
if(NOT WIN32)
  target_link_libraries(vorbis PUBLIC m)
endif()

add_library(vorbisfile STATIC ${_vb}/vorbisfile.c)
target_include_directories(vorbisfile PUBLIC "${VORBIS_DIR}/include" PRIVATE "${_vb}")
target_link_libraries(vorbisfile PUBLIC vorbis)
target_compile_options(vorbisfile PRIVATE -w)
