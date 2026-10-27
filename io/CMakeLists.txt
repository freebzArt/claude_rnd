# File formats: OpenEXR sequences, movies (FFmpeg) and JSON.
option(PTRACK_WITH_FFMPEG "Movie input (MOV, MP4, MXF) through FFmpeg" ON)

add_library(ptrack_io
  src/exr.cpp
  src/media.cpp
  src/sequence.cpp
  src/track_json.cpp
  src/tracking_space.cpp
)

if(PTRACK_WITH_FFMPEG)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
  target_sources(ptrack_io PRIVATE src/movie.cpp)
  target_link_libraries(ptrack_io PRIVATE PkgConfig::FFMPEG)
  target_compile_definitions(ptrack_io PUBLIC PTRACK_HAVE_FFMPEG=1)
endif()
option(PTRACK_WITH_OCIO "Tracking-space conversion through OpenColorIO" ON)
if(PTRACK_WITH_OCIO)
  find_package(OpenColorIO 2 CONFIG QUIET)
  if(OpenColorIO_FOUND)
    target_link_libraries(ptrack_io PRIVATE OpenColorIO::OpenColorIO)
  else()
    # Some distributions (Debian/Ubuntu) install the CMake config where
    # find_package doesn't look; pkg-config finds it.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OCIO REQUIRED IMPORTED_TARGET OpenColorIO>=2)
    target_link_libraries(ptrack_io PRIVATE PkgConfig::OCIO)
  endif()
  target_compile_definitions(ptrack_io PUBLIC PTRACK_HAVE_OCIO=1)
endif()

add_library(ptrack::io ALIAS ptrack_io)
target_include_directories(ptrack_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ptrack_io
  PUBLIC ptrack::core
  PRIVATE OpenEXR::OpenEXR nlohmann_json::nlohmann_json)
ptrack_set_warnings(ptrack_io)
