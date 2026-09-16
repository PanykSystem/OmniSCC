cmake_minimum_required(VERSION 3.13)
project(OmniSCCDos NONE)

# Also builds on its own, without the Pico SDK or an ARM compiler.
if(NOT SJASMPLUS_EXECUTABLE)
    if(DEFINED ENV{SJASMPLUS})
        set(SJASMPLUS_EXECUTABLE "$ENV{SJASMPLUS}" CACHE FILEPATH "Z80 assembler")
    else()
        find_program(SJASMPLUS_EXECUTABLE NAMES sjasmplus)
    endif()
endif()
if(NOT SJASMPLUS_EXECUTABLE)
    message(FATAL_ERROR "Install sjasmplus or set SJASMPLUS_EXECUTABLE to its path")
endif()

set(dos_outputs)
foreach(command mount eject disk odir odel ocopy onew)
    string(TOUPPER "${command}" dos_name)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${dos_name}.COM")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${SJASMPLUS_EXECUTABLE}" "--raw=${output}" "${command}.asm"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${command}.asm"
                "${CMAKE_CURRENT_SOURCE_DIR}/common.inc"
                "${CMAKE_CURRENT_SOURCE_DIR}/files.inc"
                "${SJASMPLUS_EXECUTABLE}"
        COMMENT "Building ${dos_name}.COM for MSX-DOS"
        VERBATIM
    )
    list(APPEND dos_outputs "${output}")
endforeach()
add_custom_target(dos_commands ALL DEPENDS ${dos_outputs})
