cmake_minimum_required(VERSION 3.21)

project(
	DragonbornsCodex
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

# Only flat Skyrim (SE/AE) - we target 1.6.1170 via Address Library
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

find_package(imgui CONFIG REQUIRED)

add_commonlibsse_plugin(
	${PROJECT_NAME}
	NAME "CheatMenu"
	AUTHOR "lnocriesl"
	USE_ADDRESS_LIBRARY
	SOURCES
		src/main.cpp
		src/Console.cpp
		src/Text.cpp
		src/Catalog.cpp
		src/Cheats.cpp
		src/FastTravel.cpp
		src/Enchanting.cpp
		src/Archery.cpp
		src/Puzzles.cpp
		src/Killmoves.cpp
		src/Health.cpp
		src/Enchanter.cpp
		src/SkillCap.cpp
		src/Input.cpp
		src/Overlay.cpp
		src/Menu.cpp
		src/LockpickHud.cpp
		src/Settings.cpp
		src/Hotkeys.cpp
		src/Esp.cpp
		src/Magic.cpp
		src/ItemIcons.cpp
		src/Textures.cpp
		src/Lang.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

# The test bridge (tools/skytest.py drives the game through it) is for dev builds only; the public build
# (preset "dist", tools/package.py) leaves it out entirely.
option(CODEX_TEST_BRIDGE "Build the test bridge into the plugin" ON)
if(CODEX_TEST_BRIDGE)
	target_sources(${PROJECT_NAME} PRIVATE src/TestBridge.cpp)
	target_compile_definitions(${PROJECT_NAME} PRIVATE CODEX_TEST_BRIDGE)
endif()
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /utf-8 /W3 /permissive-)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui d3d11.lib dxgi.lib)

# Copy the built DLL straight into an MO2 mod folder
set(CHEATMENU_DEPLOY_DIR "G:/Games/Neverland/Neverland/mods/Cheat Menu" CACHE PATH "Where to deploy the plugin")
if(CHEATMENU_DEPLOY_DIR)
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CHEATMENU_DEPLOY_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${CHEATMENU_DEPLOY_DIR}/SKSE/Plugins/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${CHEATMENU_DEPLOY_DIR}/SKSE/Plugins/"
		# icons, the game's key glyphs and the fonts (assets/DragonbornsCodex, see tools/make_icons.py)
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/DragonbornsCodex" "${CHEATMENU_DEPLOY_DIR}/SKSE/Plugins/DragonbornsCodex"
	)
endif()
