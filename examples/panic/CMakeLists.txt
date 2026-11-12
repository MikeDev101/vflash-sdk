# Index order is part of the diagnostic assembly's PANIC_TEST_MODE contract.
set(panic_modes manual undefined stack nested swi thumb restore system stack-undefined thumb-swi)
set(panic_mode_index 0)
foreach(mode IN LISTS panic_modes)
    vflash_add_program(vflash_panic_${mode} firmware-disc.ld
        main.c triggers.S
        ${CMAKE_SOURCE_DIR}/examples/common/demo.c
        ${CMAKE_SOURCE_DIR}/src/platform/clock.c)
    target_compile_definitions(vflash_panic_${mode} PRIVATE PANIC_TEST_MODE=${panic_mode_index})
    if(mode STREQUAL "nested")
        target_compile_definitions(vflash_panic_${mode} PRIVATE VFLASH_PANIC_INJECT_NESTED=1)
    endif()
    vflash_add_disc(panic_${mode}_disc_image vflash_panic_${mode} panic/${mode})
    math(EXPR panic_mode_index "${panic_mode_index} + 1")
endforeach()
