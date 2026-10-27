find_package(doctest REQUIRED)

add_executable(ptrack_tests
  main.cpp
  test_motion.cpp
  test_pyramid.cpp
  test_raster.cpp
  test_solver.cpp
  test_tracker.cpp
  test_export_nuke.cpp
  test_exr.cpp
  test_frame_cache.cpp
  test_movie.cpp
  test_tracking_space.cpp
)
target_link_libraries(ptrack_tests PRIVATE ptrack::core ptrack::io doctest::doctest)
ptrack_set_warnings(ptrack_tests)
target_compile_definitions(ptrack_tests PRIVATE PTRACK_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Fast unit tests in one run; the slow synthetic-shot accuracy tests split
# into groups so `ctest -j` runs them in parallel.
add_test(NAME unit COMMAND ptrack_tests --source-file-exclude=*test_tracker.cpp)
foreach(group "accuracy*" "no silent*,auto-pause*" "lighting*" "re-anchoring*")
  string(REGEX REPLACE "[^a-z]+" "_" name "${group}")
  add_test(NAME "tracker_${name}" COMMAND ptrack_tests --source-file=*test_tracker.cpp "--test-case=${group}")
endforeach()

if(TARGET ptrack)
  add_test(NAME cli_end_to_end
    COMMAND ${CMAKE_COMMAND} -DPTRACK=$<TARGET_FILE:ptrack> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_e2e.cmake)
endif()
