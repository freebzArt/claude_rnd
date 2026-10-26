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
)
target_link_libraries(ptrack_tests PRIVATE ptrack::core ptrack::io doctest::doctest)
ptrack_set_warnings(ptrack_tests)
add_test(NAME ptrack_tests COMMAND ptrack_tests)

if(TARGET ptrack)
  add_test(NAME cli_end_to_end
    COMMAND ${CMAKE_COMMAND} -DPTRACK=$<TARGET_FILE:ptrack> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_e2e.cmake)
endif()
