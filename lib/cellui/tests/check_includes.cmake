# Fails if a file under lib/cellui includes a header by a path that leaves the
# library (a "../" that climbs out of it, or anything under src/).
file(GLOB_RECURSE files ${DIR}/include/*.h ${DIR}/src/*.cpp ${DIR}/src/*.h ${DIR}/tests/*.cpp ${DIR}/tests/*.h)
set(bad "")
foreach(f ${files})
  file(STRINGS ${f} lines REGEX "^[ \t]*#[ \t]*include[ \t]*\"[^\"]*(\\.\\./|src/)")
  if(lines)
    list(APPEND bad "${f}: ${lines}")
  endif()
endforeach()
if(bad)
  string(REPLACE ";" "\n" bad "${bad}")
  message(FATAL_ERROR "cellui must not include anything outside lib/cellui:\n${bad}")
endif()
