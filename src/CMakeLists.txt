
set(APP_NAME lliurex-resolver)

find_Library(APT_PKG_LIB apt-pkg)

if (NOT APT_PKG_LIB)
    message(FATAL_ERROR "libapt-pkg not found")
endif()

add_executable(${APP_NAME} main.cpp)
target_link_libraries(${APP_NAME} ${APT_PKG_LIB})

install(TARGETS ${APP_NAME} RUNTIME DESTINATION bin)


