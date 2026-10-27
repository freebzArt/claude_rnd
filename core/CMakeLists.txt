# Tracking engine: no file-format or UI dependencies.
add_library(ptrack_core
  src/colour.cpp
  src/export_nuke.cpp
  src/motion.cpp
  src/pyramid.cpp
  src/raster.cpp
  src/solver.cpp
  src/synth.cpp
  src/tracker.cpp
  src/warp.cpp
)
add_library(ptrack::core ALIAS ptrack_core)
target_include_directories(ptrack_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ptrack_core PUBLIC Eigen3::Eigen)
ptrack_set_warnings(ptrack_core)
