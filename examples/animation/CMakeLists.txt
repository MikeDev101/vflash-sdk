vflash_add_program(vflash_animation_probe firmware-disc.ld
    main.c animation.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c)
target_compile_definitions(vflash_animation_probe PRIVATE VFLASH_DOUBLE_BUFFER=1)
# Compiler integer-division helpers only; no libc or operating-system runtime.
target_link_libraries(vflash_animation_probe PRIVATE gcc)
vflash_add_disc(animation_disc_image vflash_animation_probe animation)

vflash_add_program(vflash_mmu_animation firmware-disc.ld
    main.c animation.c
    ${CMAKE_SOURCE_DIR}/src/platform/clock.c
    ${CMAKE_SOURCE_DIR}/src/platform/mmu.c
    ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
target_compile_definitions(vflash_mmu_animation PRIVATE VFLASH_DOUBLE_BUFFER=1 VFLASH_ENABLE_MMU=1)
target_link_libraries(vflash_mmu_animation PRIVATE gcc)
vflash_add_disc(mmu_animation_disc_image vflash_mmu_animation mmu/animation)
