add_executable(ptrack main.cpp)
target_link_libraries(ptrack PRIVATE ptrack::core ptrack::io)
ptrack_set_warnings(ptrack)
