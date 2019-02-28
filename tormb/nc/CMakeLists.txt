## hello world plug-in

add_cflags("-std=gnu99")

## create and install a shared library that can plug into shadow
add_shadow_plugin(shadow-plugin-nc nc.c)
target_link_libraries(shadow-plugin-nc)
install(TARGETS shadow-plugin-nc DESTINATION plugins)

## create and install an executable that can run outside of shadow
add_executable(nc nc.c)
target_link_libraries(nc)
install(TARGETS nc DESTINATION bin)
