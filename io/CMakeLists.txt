# File formats: OpenEXR sequences and JSON today; MOV via FFmpeg next.
add_library(ptrack_io
  src/exr.cpp
  src/sequence.cpp
  src/track_json.cpp
)
add_library(ptrack::io ALIAS ptrack_io)
target_include_directories(ptrack_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ptrack_io
  PUBLIC ptrack::core
  PRIVATE OpenEXR::OpenEXR nlohmann_json::nlohmann_json)
ptrack_set_warnings(ptrack_io)
