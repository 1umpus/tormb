## hello world plug-in

add_cflags("-std=gnu99")

## create and install a shared library that can plug into shadow
add_shadow_plugin(shadow-plugin-tor_server tor_server.c)
target_link_libraries(shadow-plugin-tor_server)
install(TARGETS shadow-plugin-tor_server DESTINATION plugins)

## create and install an executable that can run outside of shadow
add_executable(tor_server tor_server.c)
target_link_libraries(tor_server)
install(TARGETS tor_server DESTINATION bin)
