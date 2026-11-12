foreach(mode IN ITEMS identity undefined)
    vflash_add_program(vflash_mmu_${mode} firmware-disc.ld
        main.c
        ${CMAKE_SOURCE_DIR}/examples/common/demo.c
        ${CMAKE_SOURCE_DIR}/src/platform/clock.c
        ${CMAKE_SOURCE_DIR}/src/platform/mmu.c
        ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
    if(mode STREQUAL "undefined")
        target_compile_definitions(vflash_mmu_${mode} PRIVATE VFLASH_MMU_UDF=1)
    endif()
    vflash_add_disc(mmu_${mode}_disc_image vflash_mmu_${mode} mmu/${mode})
endforeach()
