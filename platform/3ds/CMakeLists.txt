cmake_minimum_required(VERSION 3.20)
# The version comes from git (tools/version.sh: X.Y.Z on a tag, X.Y.Z-dev.N+hash
# on a release branch). build.sh passes it on every configure so a new commit is
# picked up without a clean; a bare configure works it out itself.
if(NOT DEFINED TMC_3DS_VERSION OR TMC_3DS_VERSION STREQUAL "")
    execute_process(
        COMMAND sh "${CMAKE_CURRENT_LIST_DIR}/tools/version.sh"
        OUTPUT_VARIABLE TMC_3DS_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
# The upstream port this fork builds on (EstebanPdN/zelda-tmc-3ds), shown next
# to our own version. Bumped by hand when upstream changes are merged.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/upstream_version.txt" TMC_3DS_UPSTREAM_VERSION LIMIT_COUNT 1)
# CMake's project VERSION accepts numeric components only; keep the complete
# string for filenames and metadata, and give project() its numeric prefix.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" TMC_3DS_PROJECT_VERSION "${TMC_3DS_VERSION}")
if(NOT TMC_3DS_PROJECT_VERSION)
    message(FATAL_ERROR "Invalid 3DS version: ${TMC_3DS_VERSION}")
endif()
project(tmc_3ds VERSION "${TMC_3DS_PROJECT_VERSION}" LANGUAGES C CXX)

# CHANNEL=beta|release is set by the release workflow (empty for local builds):
# a beta shows "vX.Y.Z BETA" and the updater treats it as older than the stable
# X.Y.Z. DEBUG_TOOLS=1 compiles in the DEBUG tab and its tools (docs/debug-tools.md).
set(TMC_3DS_CHANNEL "" CACHE STRING "Release channel baked into the binary: beta, release or empty")
option(TMC_3DS_DEBUG_TOOLS "Compile in the on-device debug tools" OFF)
if(TMC_3DS_CHANNEL STREQUAL "beta")
    set(TMC_3DS_IS_BETA 1)
    set(TMC_3DS_VERSION_LABEL "${TMC_3DS_VERSION} BETA")
else()
    set(TMC_3DS_IS_BETA 0)
    set(TMC_3DS_VERSION_LABEL "${TMC_3DS_VERSION}")
endif()
if(TMC_3DS_DEBUG_TOOLS)
    set(TMC_3DS_DEBUG_TOOLS_VALUE 1)
else()
    set(TMC_3DS_DEBUG_TOOLS_VALUE 0)
endif()
# Generated headers instead of -D flags: configure_file leaves a header alone
# when its text is unchanged, so a new commit (new version) or a debug-tools
# switch rebuilds only the files that include it, not the whole game.
set(TMC_3DS_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TMC_3DS_GENERATED}")
file(WRITE "${TMC_3DS_GENERATED}/tmc3ds_version.h.in"
"#pragma once
#define TMC_PORT_VERSION \"@TMC_3DS_VERSION@\"
#define TMC_PORT_VERSION_LABEL \"@TMC_3DS_VERSION_LABEL@\"
#define TMC_UPSTREAM_VERSION \"@TMC_3DS_UPSTREAM_VERSION@\"
#define TMC_PORT_IS_BETA @TMC_3DS_IS_BETA@
")
configure_file("${TMC_3DS_GENERATED}/tmc3ds_version.h.in" "${TMC_3DS_GENERATED}/tmc3ds_version.h" @ONLY)
file(WRITE "${TMC_3DS_GENERATED}/tmc3ds_build_config.h.in"
"#pragma once
#define TMC3DS_DEBUG_TOOLS @TMC_3DS_DEBUG_TOOLS_VALUE@
")
configure_file("${TMC_3DS_GENERATED}/tmc3ds_build_config.h.in" "${TMC_3DS_GENERATED}/tmc3ds_build_config.h" @ONLY)
message(STATUS "Minish Cap 3DS v${TMC_3DS_VERSION_LABEL} (upstream ${TMC_3DS_UPSTREAM_VERSION}), debug tools ${TMC_3DS_DEBUG_TOOLS_VALUE}")

set(TMC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

file(GLOB TMC_ACTOR_SOURCES CONFIGURE_DEPENDS
    "${TMC_ROOT}/src/playerItem/*.c"
    "${TMC_ROOT}/src/enemy/*.c"
    "${TMC_ROOT}/src/npc/*.c"
    "${TMC_ROOT}/src/object/*.c"
    "${TMC_ROOT}/src/projectile/*.c"
    "${TMC_ROOT}/src/manager/*.c"
    "${TMC_ROOT}/src/item/*.c"
    "${TMC_ROOT}/src/subtask/*.c"
    "${TMC_ROOT}/src/menu/*.c"
    "${TMC_ROOT}/src/worldEvent/*.c"
)

file(GLOB AGBPLAY_SOURCES CONFIGURE_DEPENDS "${TMC_ROOT}/libs/agbplay_core/*.cpp")

set(TMC_ENGINE_SOURCES
    src/main.c src/common.c src/interrupts.c src/game.c src/title.c src/fileselect.c
    src/flags.c src/save.c src/fade.c src/color.c src/vram.c src/screenTileMap.c
    src/message.c src/text.c src/sound.c
    src/script.c src/scroll.c src/room.c src/roomInit.c src/entity.c src/physics.c
    src/collision.c src/movement.c src/affine.c src/sineTable.c src/ui.c
    src/player.c src/playerUtils.c src/playerHitbox.c src/playerItem.c
    src/playerItemUtils.c src/playerItemDefinitions.c src/enemy.c src/enemyUtils.c
    src/npc.c src/npcUtils.c src/npcFunctions.c src/npcDefinitions.c src/object.c
    src/objectUtils.c src/objectDefinitions.c src/enemyUpdate.c src/projectile.c
    src/projectileUtils.c src/projectileUpdate.c src/manager.c src/item.c
    src/itemUtils.c src/itemDefinitions.c src/itemMetaData.c src/subtask.c
    src/cutscene.c src/backgroundAnimations.c src/beanstalkSubtask.c
    src/enterPortalSubtask.c src/gameOverTask.c src/gameUtils.c src/gameData.c
    src/kinstone.c src/droptables.c src/demo.c src/debug.c src/flagDebug.c
    src/staffroll.c src/data/figurineMenuData.c src/data/hitbox.c
    src/data/areaMetadata.c src/data/caveBorderMapData.c src/data/data_080046A4.c
    src/data/mapActTileToSurfaceType.c src/data/objPalettes.c
    src/data/screenTransitions.c src/data/transitions.c src/code_08049CD4.c
    src/code_08049DF4.c src/code_0805EC04.c
)
list(TRANSFORM TMC_ENGINE_SOURCES PREPEND "${TMC_ROOT}/")

set(TMC_PORT_SOURCES
    port/port_bios.c port/port_debug_verbose.c port/port_inline_ptrs.c
    port/port_script_addrs.c port/port_offset_remap.c port/port_rom.c
    port/port_region_data.c
    port/port_bottle_compat.c
    port/port_bomb_compat.c
    port/port_cloud_tops_fight.c
    port/port_vaati_progress.c
    port/port_stubs.c port/stubs_autogen.c port/data_stubs_autogen.c
    port/data_const_stubs.c port/flag_remap_generated.c port/port_rom_tables.c
    port/port_linked_stubs.c port/port_figurines.c port/port_draw.c port/port_zone_tiles.c port/port_neighbour_rooms.c
    port/port_3ds_full_view_policy.c
    port/port_gba_mem.c port/port_hdma.c port/port_save.c port/port_softslots.c
    port/port_dump_state.c
    port/port_second_screen_state.c port/port_text_i18n.c
    port/port_second_screen_render.c port/port_second_screen_color.c port/port_second_screen_worldmap.c
    port/port_second_screen_dungeonmap.c port/port_second_screen_quest.c
    port/port_second_screen_theme.c port/port_roll_attack_macro.c
    port/port_filter.c port/port_animation.c port/port_math.c
    port/port_text_render.c port/port_gameplay_stubs.c port/port_m4a_stubs.c
    port/port_room_funcs.c port/port_script_funcs.c port/port_analog_movement.c
    port/port_m4a_backend.cpp port/port_m4a_mixdown.c port/generated_sounds_embed.cpp
    port/ppu/src/virtuappu.c port/ppu/src/mode1.c
)
list(TRANSFORM TMC_PORT_SOURCES PREPEND "${TMC_ROOT}/")

# rcheevos: the official RetroAchievements library, vendored under third_party/rcheevos
# (its VERSION.txt: the commit and the subset kept). Its own target, so it is built as
# plain C without the game's forced include and defines.
file(GLOB RCHEEVOS_SOURCES
    "${TMC_ROOT}/third_party/rcheevos/src/*.c"
    "${TMC_ROOT}/third_party/rcheevos/src/rapi/*.c"
    "${TMC_ROOT}/third_party/rcheevos/src/rcheevos/*.c")
add_library(rcheevos STATIC ${RCHEEVOS_SOURCES} "${TMC_ROOT}/third_party/rcheevos/src/rhash/md5.c")
target_include_directories(rcheevos
    PUBLIC "${TMC_ROOT}/third_party/rcheevos/include" "${TMC_ROOT}/third_party/rcheevos/src/rhash"
    PRIVATE "${TMC_ROOT}/third_party/rcheevos/src")
target_compile_options(rcheevos PRIVATE -Os -ffunction-sections -fdata-sections -w)

ctr_add_shader_library(ppu_gpu_3ds_shader source/ppu_gpu_3ds.v.pica)
dkp_add_embedded_binary_library(ppu_gpu_3ds_shader_bin ppu_gpu_3ds_shader)

add_executable(tmc-3ds
    ${TMC_ENGINE_SOURCES}
    ${TMC_ACTOR_SOURCES}
    ${TMC_PORT_SOURCES}
    ${AGBPLAY_SOURCES}
    source/updater.c
    source/update_manifest.c
    source/main_3ds.c
    source/arm11_fast_mem.c
    source/ndsp_pcm_offload.c
    source/ndsp_psg_offload.c
    source/platform_3ds.c
    source/old3ds_frame_pacer.c
    source/top_view_3ds.c
    source/platform_gpu_3ds.c
    source/bottom_frame_state_3ds.c
    source/bottom_map_anim_3ds.c
    source/speaker_eq_3ds.c
    source/port_second_screen_3ds.c
    source/ui_font_3ds.c
    source/upstream_import_3ds.c
    source/boot_text_3ds.c
    source/debug_files_3ds.c
    source/debug_log_3ds.c
    source/debug_perf_3ds.c
    source/scene_rec_3ds.c
    source/scene_pictures_3ds.c
    source/stereo_3ds.c
    source/zone_depth_3ds.c
    source/title_rays_3ds.c
    source/net_3ds.c
    source/ftp_server_3ds.c
    source/debug_link_3ds.c
    source/save_state_3ds.c
    source/retro_ach.c
    source/retro_ach_map_3ds.c
    source/state_symbols_3ds.c
    source/state_blob_3ds.c
    source/state_pointers_3ds.c
    source/cia_install_3ds.c
    source/port_second_screen_sync_3ds.c
    source/port_ppu_3ds.c
    source/hud_xy_3ds.c
    source/fade_gpu_3ds.c
    source/gba_look_3ds.c
    source/port_ppu_gpu_3ds_model.c
    source/port_ppu_gpu_3ds.c
    source/port_audio_3ds.c
    source/port_config_3ds.c
    source/port_asset_stubs_3ds.c
    source/port_optional_stubs_3ds.c
)

target_include_directories(tmc-3ds PRIVATE
    "${TMC_ROOT}" "${TMC_ROOT}/include" "${TMC_ROOT}/port"
    "${TMC_ROOT}/port/ppu/include" "${TMC_ROOT}/libs/agbplay_core"
    "${TMC_ROOT}/build/USA" "${CMAKE_CURRENT_LIST_DIR}/source"
    "${TMC_3DS_GENERATED}"
)

target_compile_definitions(tmc-3ds PRIVATE
    PC_PORT NON_MATCHING USE_HDMA TMC_3DS MULTI_REGION USA ENGLISH REVISION=0
    MODE1_GBA_WIDTH=400 MODE1_GBA_HEIGHT=240 PPU_GPU3DS_PROFILE
)
target_compile_features(tmc-3ds PRIVATE c_std_11 cxx_std_20)
set_target_properties(tmc-3ds PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)
target_compile_options(tmc-3ds PRIVATE
    -include region.h
    -O3 -fomit-frame-pointer -ffunction-sections -fdata-sections
    -fno-strict-aliasing -fwrapv -fno-strict-overflow -w
    -Wno-unused-parameter -Wno-missing-field-initializers
)
set_source_files_properties(
    "${TMC_ROOT}/port/ppu/src/virtuappu.c"
    "${TMC_ROOT}/port/ppu/src/mode1.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_second_screen_3ds.c"
    "${TMC_ROOT}/port/port_second_screen_render.c"
    "${TMC_ROOT}/port/port_second_screen_color.c"
    "${TMC_ROOT}/port/port_second_screen_worldmap.c"
    "${TMC_ROOT}/port/port_second_screen_dungeonmap.c"
    "${TMC_ROOT}/port/port_second_screen_quest.c"
    "${TMC_ROOT}/port/port_second_screen_theme.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/platform_gpu_3ds.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_ppu_3ds.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_ppu_gpu_3ds_model.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/port_ppu_gpu_3ds.c"
    PROPERTIES COMPILE_OPTIONS "-flto;-fno-semantic-interposition"
)
# Neither is hot (a few calls a frame): built for size, so the read-only segment stays
# under its pin (3dsx.ld).
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/source/retro_ach.c" PROPERTIES COMPILE_OPTIONS "-Os")
set_source_files_properties(
    "${TMC_ROOT}/port/port_m4a_backend.cpp"
    # The mixdown was carved out of port_m4a_backend.cpp, so it must keep that
    # file's flags. Compiled without -ffast-math it would differ from the code
    # it replaced in both rounding and speed.
    "${TMC_ROOT}/port/port_m4a_mixdown.c"
    ${AGBPLAY_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-flto;-fno-semantic-interposition;-ffast-math"
)
target_link_options(tmc-3ds PRIVATE -O3 -flto -Wl,--gc-sections
    # -specs=3dsx.specs links with `-T 3dsx.ld%s`; -B makes that find our copy,
    # which lays the game out at fixed addresses for save states (see the file).
    "-B${CMAKE_CURRENT_LIST_DIR}" "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/tmc-3ds.map")
set_property(TARGET tmc-3ds APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/3dsx.ld")
# The names of the game's globals and functions go into the ELF after the link
# (save states that survive a rebuild, tools/gen_state_symbols.py); the 3DSX
# and the CIA are made from the patched ELF.
find_program(TMC_PYTHON NAMES python3 python REQUIRED)
add_custom_command(TARGET tmc-3ds POST_BUILD
    COMMAND "${TMC_PYTHON}" "${TMC_ROOT}/tools/gen_state_symbols.py" "$<TARGET_FILE:tmc-3ds>"
    COMMENT "Patching the save state symbol table into the ELF")
set(UPDATE_DEPS_ROOT "$ENV{DEVKITPRO}/portlibs/3ds" CACHE PATH "3DS updater dependencies")
target_include_directories(tmc-3ds PRIVATE "${UPDATE_DEPS_ROOT}/include")
target_link_directories(tmc-3ds PRIVATE "${UPDATE_DEPS_ROOT}/lib")
# Official devkitPro curl enables gzip/deflate; the pinned minimal build does
# not. Link its optional static dependency when supplied by the selected SDK.
# The scene pictures are zlib streams (scene_pictures_3ds.c), so zlib is required (3ds-zlib, in the CI's devkitarm
# image); tools/scene-extend/to_console.py --lz77 makes files the game reads without it.
find_library(TMC_UPDATE_ZLIB NAMES z PATHS "${UPDATE_DEPS_ROOT}/lib"
    "$ENV{DEVKITPRO}/portlibs/3ds/lib" NO_DEFAULT_PATH REQUIRED)
if(TMC_UPDATE_ZLIB)
    target_link_libraries(tmc-3ds PRIVATE rcheevos citro2d citro3d curl jansson mbedtls mbedx509 mbedcrypto "${TMC_UPDATE_ZLIB}" ctru m ppu_gpu_3ds_shader_bin)
else()
    target_link_libraries(tmc-3ds PRIVATE rcheevos citro2d citro3d curl jansson mbedtls mbedx509 mbedcrypto ctru m ppu_gpu_3ds_shader_bin)
endif()

set(TMC_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${TMC_ROMFS}")
# Include redistribution notices with each installable package.
file(MAKE_DIRECTORY "${TMC_ROMFS}/licenses")
configure_file("${TMC_ROOT}/LICENSE" "${TMC_ROMFS}/licenses/GPL-3.0.txt" COPYONLY)
configure_file("${TMC_ROOT}/COPYRIGHT.md" "${TMC_ROMFS}/licenses/COPYRIGHT.md" COPYONLY)
configure_file("${TMC_ROOT}/THIRD-PARTY-LICENSES.md" "${TMC_ROMFS}/licenses/THIRD-PARTY-LICENSES.md" COPYONLY)
configure_file("${TMC_ROOT}/libs/agbplay_core/LICENSE" "${TMC_ROMFS}/licenses/agbplay-NOTICE.txt" COPYONLY)
configure_file("${TMC_ROOT}/libs/agbplay_core/COPYING.LESSER" "${TMC_ROMFS}/licenses/LGPL-3.0.txt" COPYONLY)
configure_file("${TMC_ROOT}/third_party/rcheevos/LICENSE" "${TMC_ROMFS}/licenses/rcheevos-MIT.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/romfs/update-ca.pem" "${TMC_ROMFS}/update-ca.pem" COPYONLY)
# The RetroAchievements unlock sound (retro_ach.c): mono s16le at 32000 Hz, from ../sm-3ds.
configure_file("${CMAKE_CURRENT_LIST_DIR}/romfs/ra-unlock.pcm" "${TMC_ROMFS}/ra-unlock.pcm" COPYONLY)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/romfs/splash.rgb565"
    "${TMC_ROMFS}/splash.rgb565"
    COPYONLY
)
# Extensions of scenes the GBA drew at 240x160, for Full View (docs/wide-scene-extension.md):
# made by tools/scene-extend/to_console.py from platform/3ds/assets/wide-scenes/.
file(MAKE_DIRECTORY "${TMC_ROMFS}/wide-scenes")
foreach(scene crenel-rocks-ground portal-cutscene-strips portal-rock-strips crenel-paths-ground crenel-paths-ledges crenel-paths-shadows minish-paths-leaves minish-paths-shadows minish-water-ground minish-water-leaves minish-water-shadows minish-town-leaves minish-town-shadows minish-ranch-shadows minish-cabin-leaves minish-cabin-shadows title-forest fileselect-cave)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/romfs/wide-scenes/${scene}.bin"
                   "${TMC_ROMFS}/wide-scenes/${scene}.bin" COPYONLY)
endforeach()

# The GBA frame of gba_look_3ds.c: the owner's artwork as ../mzm made it (512x256 GPU_RGBA8 tiled,
# LZ4 block, 524288 bytes once unpacked). Loaded on first use, so it takes no room in the binary.
configure_file("${CMAKE_CURRENT_LIST_DIR}/romfs/gba-look-frame.lz4"
               "${TMC_ROMFS}/gba-look-frame.lz4" COPYONLY)

set(TMC_SMDH "${CMAKE_CURRENT_BINARY_DIR}/tmc-3ds.smdh")
ctr_generate_smdh(
    OUTPUT "${TMC_SMDH}"
    NAME "Minish Cap 3DS v${TMC_3DS_VERSION_LABEL}"
    DESCRIPTION "Native Nintendo 3DS port"
    AUTHOR "tinaut1986 / Esteban PDN / Project Picori"
    ICON "${CMAKE_CURRENT_LIST_DIR}/assets/icon-48.png"
)

ctr_create_3dsx(tmc-3ds-3dsx
    TARGET tmc-3ds
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tmc-3ds-v${TMC_3DS_VERSION}.3dsx"
    SMDH "${TMC_SMDH}"
    ROMFS "${TMC_ROMFS}"
)
