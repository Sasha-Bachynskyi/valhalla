function(set_variable_from_rel_or_absolute_path var root rel_or_abs_path)
  if(IS_ABSOLUTE "${rel_or_abs_path}")
    set(${var} "${rel_or_abs_path}" PARENT_SCOPE)
  else()
    set(${var} "${root}/${rel_or_abs_path}" PARENT_SCOPE)
  endif()
endfunction()

# configure a pkg-config file libvalhalla.pc
function(configure_valhalla_pc)
  set(prefix ${CMAKE_INSTALL_PREFIX})
  set(exec_prefix ${prefix})
  set_variable_from_rel_or_absolute_path("libdir" "$\{prefix\}" "${CMAKE_INSTALL_LIBDIR}")
  set_variable_from_rel_or_absolute_path("includedir" "$\{prefix\}" "${CMAKE_INSTALL_INCLUDEDIR}")
  # Build strings of dependencies
  set(LIBS "")
  set(REQUIRES "zlib")
  set(LIBS_PRIVATE "${CMAKE_THREAD_LIBS_INIT}")
  if (INSTALL_VENDORED_LIBS)
    set(CFLAGS "-I$\{includedir\}/valhalla/third_party")
  endif()

  if(TARGET protobuf::libprotobuf-lite)
    list(APPEND REQUIRES protobuf-lite)
  else()
    list(APPEND REQUIRES protobuf)
  endif()
  
  if(ENABLE_DATA_TOOLS)
    list(APPEND REQUIRES spatialite sqlite3 luajit geos)
  endif()
  if(ENABLE_HTTP OR ENABLE_PYTHON_BINDINGS)
    list(APPEND REQUIRES libcurl)
  endif()
  if(ENABLE_SERVICES)
    list(APPEND REQUIRES libprime_server)
  endif()
  if(WIN32 AND NOT MINGW)
    list(APPEND LIBS_PRIVATE -lole32 -lshell32)
  else()
    if(NOT "-lm" IN_LIST LIBS_PRIVATE)
        list(APPEND LIBS_PRIVATE -lm)
    endif()
  endif()
  list(JOIN LIBS " " LIBS)
  list(JOIN REQUIRES " " REQUIRES)
  list(JOIN LIBS_PRIVATE " " LIBS_PRIVATE)

  configure_file(
    ${CMAKE_SOURCE_DIR}/libvalhalla.pc.in
    ${CMAKE_BINARY_DIR}/libvalhalla.pc
    @ONLY)
endfunction()
