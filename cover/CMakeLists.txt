## hello world plug-in

add_cflags("-std=gnu99")

## create and install a shared library that can plug into shadow
add_shadow_plugin(shadow-plugin-coverv cover.c)
target_link_libraries(shadow-plugin-cover)
install(TARGETS shadow-plugin-cover DESTINATION plugins)

## create and install an executable that can run outside of shadow
add_executable(cover cover.c)
target_link_libraries(cover)
install(TARGETS cover DESTINATION bin)
