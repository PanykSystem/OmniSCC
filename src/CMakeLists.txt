cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD_HEADER_DIRS ${CMAKE_SOURCE_DIR}/include/boards) 
set(PICO_BOARD omniscc CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(OmniSCC)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

set(OMNISCC_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")
file(MAKE_DIRECTORY "${OMNISCC_GENERATED_DIR}")

# The Z80 disk stub is assembled twice at different origins and diffed to
# derive its relocation table; see msx/build_stub.py.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    OUTPUT ${OMNISCC_GENERATED_DIR}/romdb.bin
           ${OMNISCC_GENERATED_DIR}/mapper_database.hxx
           ${OMNISCC_GENERATED_DIR}/mapper_tags.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/build_mapper_database.py
            --output ${OMNISCC_GENERATED_DIR}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/build_mapper_database.py
            ${CMAKE_CURRENT_LIST_DIR}/tools/mapperdb/softwaredb.xml
            ${CMAKE_CURRENT_LIST_DIR}/tools/mapperdb/custom_roms.txt
    VERBATIM)
add_custom_target(mapper_database ALL DEPENDS ${OMNISCC_GENERATED_DIR}/romdb.bin
    ${OMNISCC_GENERATED_DIR}/mapper_database.hxx ${OMNISCC_GENERATED_DIR}/mapper_tags.inc)

# sjasmplus runs on the host; WIN32 describes the Pico target after project().
find_program(SJASMPLUS_EXECUTABLE NAMES sjasmplus)
if(NOT SJASMPLUS_EXECUTABLE)
   message(FATAL_ERROR "Install sjasmplus or set SJASMPLUS_EXECUTABLE to its path")
endif()
add_custom_command(
    OUTPUT ${OMNISCC_GENERATED_DIR}/diskstub_rom.bin
           ${OMNISCC_GENERATED_DIR}/diskstub_ram.bin
           ${OMNISCC_GENERATED_DIR}/diskstub_win.bin
           ${OMNISCC_GENERATED_DIR}/diskstub_bas.bin
           ${OMNISCC_GENERATED_DIR}/diskstub_rom.rel
           ${OMNISCC_GENERATED_DIR}/diskstub_ram.rel
           ${OMNISCC_GENERATED_DIR}/diskstub_win.rel
           ${OMNISCC_GENERATED_DIR}/diskstub_bas.rel
    COMMAND ${CMAKE_COMMAND} -E env "SJASMPLUS=${SJASMPLUS_EXECUTABLE}"
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/msx/build_stub.py
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/msx/diskstub.asm
            ${CMAKE_CURRENT_LIST_DIR}/msx/build_stub.py
    COMMENT "Assembling and relocating the Z80 disk stub"
    VERBATIM
)
add_custom_target(diskstub ALL DEPENDS
    ${OMNISCC_GENERATED_DIR}/diskstub_rom.bin
    ${OMNISCC_GENERATED_DIR}/diskstub_rom.rel
    ${OMNISCC_GENERATED_DIR}/diskstub_ram.bin
    ${OMNISCC_GENERATED_DIR}/diskstub_ram.rel
    ${OMNISCC_GENERATED_DIR}/diskstub_win.bin
    ${OMNISCC_GENERATED_DIR}/diskstub_win.rel
    ${OMNISCC_GENERATED_DIR}/diskstub_bas.bin
    ${OMNISCC_GENERATED_DIR}/diskstub_bas.rel)

add_executable(OmniSCC
      ${CMAKE_CURRENT_LIST_DIR}/main.c
      ${CMAKE_CURRENT_LIST_DIR}/src/Cartridge.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/FlashAdapter.c
      ${CMAKE_CURRENT_LIST_DIR}/src/NextorCart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/NextorMemory.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/NextorMailbox.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/NextorVolume.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/VolumeAccess.c
      ${CMAKE_CURRENT_LIST_DIR}/src/FSAdapter.c
      ${CMAKE_CURRENT_LIST_DIR}/src/KonamiCart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KonamiSCCCart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/SCCICart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KUCMapper.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KUCFlash.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KUCSectorSave.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KUCSaveStorage.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KUCCart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/KUCMount.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/SHA256.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/SHA1.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/RomDatabase.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/RomPreparation.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/RomSelection.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/ASCII8Cart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/ASCII16Cart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/Loader.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/DiskCart.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/DiskImage.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/DiskFiles.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/RomPatcher.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/MSCAdapter.c
      ${CMAKE_CURRENT_LIST_DIR}/src/OmniSCC.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/RomStorageBuilder.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/SCCSound.cxx
      ${CMAKE_CURRENT_LIST_DIR}/src/USBDescriptiors.c
      ${CMAKE_CURRENT_LIST_DIR}/libs/pico_fatfs/fatfs/ff.c
      ${CMAKE_CURRENT_LIST_DIR}/libs/pico_fatfs/fatfs/ffsystem.c
      ${CMAKE_CURRENT_LIST_DIR}/libs/pico_fatfs/fatfs/ffunicode.c
    )

add_dependencies(OmniSCC diskstub mapper_database)

# Ensure DiskCart.cxx is recompiled when any of the four stub blobs change
# (the .incbin directives inside IMPORT_BIN's asm(...) embed all of them, but
# that embedding is invisible to Ninja's automatic header/include scanning,
# so it has to be declared explicitly here). Six files, not eight: rom and ram
# each carry a relocation table and so list a .bin and a .rel, but win and bas
# are assembled at their own fixed origin and need none, so only their .bin is
# below -- a .rel neither blob's IMPORT_BIN embeds would depend DiskCart.cxx.o
# on a file its object code never actually uses. add_dependencies() above only
# orders the diskstub target before OmniSCC links; it does not make
# DiskCart.cxx.o stale when the blobs it embeds change -- this does. Same
# belt-and-braces pairing as preloader_rom/Loader.cxx below.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/src/DiskCart.cxx
    PROPERTIES OBJECT_DEPENDS
    "${OMNISCC_GENERATED_DIR}/diskstub_rom.bin;${OMNISCC_GENERATED_DIR}/diskstub_rom.rel;${OMNISCC_GENERATED_DIR}/diskstub_ram.bin;${OMNISCC_GENERATED_DIR}/diskstub_ram.rel;${OMNISCC_GENERATED_DIR}/diskstub_win.bin;${OMNISCC_GENERATED_DIR}/diskstub_bas.bin"
)

target_include_directories(OmniSCC
    PRIVATE
    ${OMNISCC_GENERATED_DIR}
    libs/pico_fatfs/fatfs
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# 16megs of flash on purple pico clones
target_compile_options(OmniSCC PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-fstack-usage>)
target_compile_definitions(OmniSCC PRIVATE
    "OMNISCC_GENERATED_DIR=\"${OMNISCC_GENERATED_DIR}\""
    "PICO_FLASH_SIZE_BYTES=16777216"
    "PICO_STACK_SIZE=0x4000"
    PICO_CORE1_STACK_SIZE=0x2000
)

# Full newlib's allocator and reentrancy state consume over 1 KiB of the
# remaining RAM with GCC. Nano keeps enough heap for cartridge transitions;
# LLVM toolchains keep their own C library. Apply specs while compiling too,
# so newlib's headers and the linked reentrancy layout agree.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(OmniSCC PRIVATE --specs=nano.specs)
    target_link_options(OmniSCC PRIVATE --specs=nano.specs)
endif()

pico_set_linker_script(OmniSCC ${CMAKE_CURRENT_SOURCE_DIR}/memmap_default.ld)

target_link_libraries(OmniSCC PRIVATE
  hardware_flash
  hardware_sync
  pico_stdlib
  pico_multicore
  tinyusb_additions
  tinyusb_board
  tinyusb_device
  hardware_pio
  hardware_dma
)

pico_generate_pio_header(
    OmniSCC
    ${CMAKE_CURRENT_LIST_DIR}/src/scc_dac.pio
)

target_link_options(OmniSCC PRIVATE -Wl,--print-memory-usage)

add_custom_command(TARGET OmniSCC POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/build_checks/check_memory_layout.py $<TARGET_FILE:OmniSCC>
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/build_checks/check_core1_stack.py --elf $<TARGET_FILE:OmniSCC> --su-root ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/build_checks/check_rom_residency.py --elf $<TARGET_FILE:OmniSCC>
    VERBATIM)

pico_enable_stdio_usb(OmniSCC 1)
# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(OmniSCC)


# =============================================================================
# Z80 Preloader ROM Build
# =============================================================================
# Build the Z80 preloader assembly (msx/preloader.asm) into generated/preloader.rom
# The ROM is embedded into the firmware via IMPORT_BIN macro in Loader.cxx
# and must be built before the main firmware links.
add_custom_command(
    OUTPUT ${OMNISCC_GENERATED_DIR}/preloader.rom
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/msx
            ${SJASMPLUS_EXECUTABLE} preloader.asm
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/msx/preloader.asm
            ${SJASMPLUS_EXECUTABLE}
    COMMENT "Building Z80 preloader ROM..."
    VERBATIM
)

add_custom_target(preloader_rom ALL
    DEPENDS ${OMNISCC_GENERATED_DIR}/preloader.rom
)

# Ensure the main firmware depends on preloader ROM being up-to-date
add_dependencies(OmniSCC preloader_rom)

# Ensure Loader.cxx is recompiled when preloader.rom changes
# (The .incbin directive in Loader.cxx embeds preloader.rom contents)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/Loader.cxx
    PROPERTIES OBJECT_DEPENDS ${OMNISCC_GENERATED_DIR}/preloader.rom
)


# =============================================================================
# Z80 Loader ROM Build
# =============================================================================
# Build the MSX browser menu (msx/loader.asm) into generated/loader.rom.
# Unlike the preloader, this ROM is not embedded into the firmware; it lives on
# the internal FAT filesystem and is copied there via USB MSC. Building it here
# guarantees that ninja always produces an up-to-date loader.rom alongside the
# firmware.
add_custom_command(
    OUTPUT ${OMNISCC_GENERATED_DIR}/loader.rom
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/msx
            ${SJASMPLUS_EXECUTABLE} loader.asm
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/msx/nextor/nextor_menu.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader.asm
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/graphic_frame.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/graphic_list.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/graphic_status.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/font.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_font.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fw_help.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_title.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/menu_art.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/countdown_badge.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/vdp.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/disk_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/audio_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/audio_controls.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fav_volume_save.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rom_database.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/kuc_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/startup_screen.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/startup_logo.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rbsc_easter_egg.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rbsc_logo.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mapper_tags.inc
            ${OMNISCC_GENERATED_DIR}/mapper_tags.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mailbox.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mount_errors.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/joystick.inc
            ${SJASMPLUS_EXECUTABLE}
    COMMENT "Building Z80 browser loader ROM..."
    VERBATIM
)

# The other menu ROM: ten fixed favourites read from /loader.ini rather than a
# browser over the volume (msx/loader_fav.asm). Both are built every time,
# because they share vdp.inc and mailbox.inc and a change to either must break
# loudly here rather than on whichever ROM the user happens to have copied to the
# cartridge.
#
# They are mutually exclusive on the volume, and loader.rom is the only name the
# firmware loads: using the favourites menu means copying loader_fav.rom there
# under that name.
add_custom_command(
    OUTPUT ${OMNISCC_GENERATED_DIR}/loader_fav.rom
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/msx
            ${SJASMPLUS_EXECUTABLE} loader_fav.asm
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/msx/nextor/nextor_menu.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_fav.asm
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_fav_title.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fw_help.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/menu_art.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/font.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/countdown_badge.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/vdp.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fav_vdp.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fav_auto_ui.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fav_text.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/ui_detect.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/text_frame.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/disk_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/audio_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/audio_controls.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fav_volume_save.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rom_database.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/kuc_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/startup_screen.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/startup_logo.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rbsc_easter_egg.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rbsc_logo.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mapper_tags.inc
            ${OMNISCC_GENERATED_DIR}/mapper_tags.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mailbox.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mount_errors.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/joystick.inc
            ${SJASMPLUS_EXECUTABLE}
    COMMENT "Building Z80 favourites loader ROM..."
    VERBATIM
)

add_custom_command(
    OUTPUT ${OMNISCC_GENERATED_DIR}/loader_auto.rom
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/msx
            ${SJASMPLUS_EXECUTABLE} loader_auto.asm
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_auto.asm
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader.asm
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/auto_ui.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/ui_detect.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/auto_renderers.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/graphic_frame.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/graphic_list.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/graphic_status.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/text_frame.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/text_list.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/text_status.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fw_help.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/disk_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/audio_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/audio_controls.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/fav_volume_save.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rom_database.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/kuc_options.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/startup_screen.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/startup_logo.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rbsc_easter_egg.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/rbsc_logo.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mapper_tags.inc
            ${OMNISCC_GENERATED_DIR}/mapper_tags.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mailbox.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/mount_errors.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/joystick.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/nextor/nextor_menu.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/vdp.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/menu_art.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/countdown_badge.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_font.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/msx/loader_title.inc
            ${SJASMPLUS_EXECUTABLE}
    COMMENT "Building Z80 universal 16 KiB browser ROM..."
    VERBATIM
)

add_custom_target(loader_rom ALL
    DEPENDS ${OMNISCC_GENERATED_DIR}/loader.rom
            ${OMNISCC_GENERATED_DIR}/loader_fav.rom
            ${OMNISCC_GENERATED_DIR}/loader_auto.rom
)

# Build both loader ROMs as part of the default firmware build.
add_dependencies(OmniSCC loader_rom)


find_program(OPENOCD openocd)
if(OPENOCD)
  add_custom_target(flash
    COMMAND ${OPENOCD} -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program ${CMAKE_PROJECT_NAME}.elf verify reset exit"
    DEPENDS ${CMAKE_PROJECT_NAME}
  )
  add_custom_target(reset COMMAND ${OPENOCD} -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c init -c reset -c exit)

endif()

# Standalone MSX-DOS clients for the cartridge disk API.
add_subdirectory(msx/dos dos)


set_source_files_properties(src/KUCMapper.cxx src/KUCFlash.cxx src/KUCSectorSave.cxx
    src/KUCSaveStorage.cxx src/KUCCart.cxx
    PROPERTIES COMPILE_OPTIONS "-fno-jump-tables;-fno-tree-switch-conversion")
