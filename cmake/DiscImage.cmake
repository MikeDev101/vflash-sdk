function(vflash_add_disc target executable directory)
    get_filename_component(destination "${CMAKE_CURRENT_BINARY_DIR}/${directory}" ABSOLUTE)
    add_custom_command(OUTPUT "${destination}/BOOT.BIN"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${destination}/BOOT.BIN"
        COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/pack_boot.py" "$<TARGET_FILE:${executable}>" "${destination}/BOOT.BIN"
        DEPENDS ${executable} "${tools_dir}/pack_boot.py" "${tools_dir}/elf_image.py" VERBATIM)
    add_custom_command(OUTPUT "${destination}/homebrew.iso"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${destination}/homebrew.iso"
        COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/make_iso.py" "${destination}/BOOT.BIN" "${destination}/homebrew.iso"
        DEPENDS "${destination}/BOOT.BIN" "${tools_dir}/make_iso.py" "${tools_dir}/inspect_boot.py" VERBATIM)
    add_custom_command(OUTPUT "${destination}/homebrew.cue" "${destination}/homebrew.bin"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${destination}/homebrew.cue" "${destination}/homebrew.bin"
        COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/raw_cd.py" "${destination}/homebrew.iso" "${destination}/homebrew.cue"
        DEPENDS "${destination}/homebrew.iso" "${tools_dir}/raw_cd.py" VERBATIM)
    add_custom_target(${target} DEPENDS "${destination}/homebrew.cue" "${destination}/homebrew.bin")
endfunction()
