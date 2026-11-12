set(hello_sources
    main.c
    ${CMAKE_SOURCE_DIR}/examples/common/demo.c
)

vflash_add_program(vflash_probe flashem-direct.ld ${hello_sources})
vflash_add_program(vflash_disc_probe firmware-disc.ld ${hello_sources})
vflash_add_disc(disc_image vflash_disc_probe ".")
add_custom_target(boot_image ALL DEPENDS "${CMAKE_BINARY_DIR}/BOOT.BIN")
