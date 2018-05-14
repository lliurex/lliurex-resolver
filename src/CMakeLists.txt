

add_executable(lliurex-resolver main.cpp)
target_link_libraries(lliurex-resolver apt-pkg)
add_definitions(-std=c++11 -g -O2)

install(TARGETS lliurex-resolver RUNTIME DESTINATION bin)


