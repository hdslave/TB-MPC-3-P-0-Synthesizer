cmake_minimum_required(VERSION 3.20)
project(tb_mpc_3p0_mac_clap LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLAP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../third_party/clap/include")
set(DESKTOP_DIR "${CMAKE_SOURCE_DIR}/desktop")
set(TARGET_NAME "Slink")
set(SLUG "tb_mpc_3p0")

# Compile inner plugin DSP/screen with clap_entry renamed to hdslave_inner_clap_entry
add_library(${SLUG}_inner OBJECT
    "${CMAKE_SOURCE_DIR}/src/tbmpc.cpp"
)
target_compile_definitions(${SLUG}_inner PRIVATE clap_entry=hdslave_inner_clap_entry)
target_include_directories(${SLUG}_inner PRIVATE
    "${CLAP_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${DESKTOP_DIR}"
)
set_target_properties(${SLUG}_inner PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MINGW)
    target_compile_definitions(${SLUG}_inner PRIVATE _USE_MATH_DEFINES)
endif()

if(MSVC)
    target_include_directories(${SLUG}_inner PRIVATE "${DESKTOP_DIR}/win32/compat")
    target_compile_options(${SLUG}_inner PRIVATE /O2 /EHsc /W2 /FI"${DESKTOP_DIR}/win32/compat/msvc_compat.h" /wd4244 /wd4267 /wd4305 /wd4996)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${SLUG}_inner PRIVATE -Wno-c++11-narrowing -Wno-deprecated-declarations -Wno-unused-command-line-argument)
    endif()
else()
    target_compile_options(${SLUG}_inner PRIVATE -O3 -ffp-contract=fast -fno-math-errno -fno-trapping-math -fvisibility=hidden)
endif()

# Native desktop editor
set(SHELL_SRCS
    "${DESKTOP_DIR}/desktop_wrapper.cpp"
    "${DESKTOP_DIR}/panel_ui.cpp"
    "${DESKTOP_DIR}/acid_motion.cpp"
    "${DESKTOP_DIR}/thorn_art.cpp"
)
set(THORN_BANK "${CMAKE_SOURCE_DIR}/assets/thorn/panel.bank")
if(APPLE)
    enable_language(ASM)
    set(BANK_SRC "${CMAKE_BINARY_DIR}/thorn_bank.S")
    configure_file("${DESKTOP_DIR}/thorn_bank.S.in" "${BANK_SRC}" @ONLY)
    list(APPEND SHELL_SRCS "${DESKTOP_DIR}/gui_macos.mm")
elseif(WIN32)
    enable_language(RC)
    set(BANK_SRC "${CMAKE_BINARY_DIR}/thorn_bank.rc")
    configure_file("${DESKTOP_DIR}/thorn_bank.rc.in" "${BANK_SRC}" @ONLY)
    list(APPEND SHELL_SRCS "${DESKTOP_DIR}/gui_win32.cpp" "${DESKTOP_DIR}/win32/clap.def")
endif()
set_source_files_properties("${BANK_SRC}" PROPERTIES OBJECT_DEPENDS "${THORN_BANK}")
list(APPEND SHELL_SRCS "${BANK_SRC}")

add_library(${TARGET_NAME} MODULE
    $<TARGET_OBJECTS:${SLUG}_inner>
    ${SHELL_SRCS}
)
target_include_directories(${TARGET_NAME} PRIVATE
    "${CLAP_INCLUDE_DIR}"
    "${DESKTOP_DIR}"
)

if(APPLE)
    # The bundle and the CLAP descriptor advertise the same desktop version.
    set(VERSION_HEADER "${CMAKE_SOURCE_DIR}/src/params.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VERSION_HEADER}")
    file(STRINGS "${VERSION_HEADER}" VERSION_LINE REGEX "^#define TBMPC_VERSION")
    string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" DESKTOP_VERSION "${VERSION_LINE}")
    set(BUNDLE_NAME "${TARGET_NAME}")
    set(BUNDLE_SLUG "${SLUG}")
    set(PLIST_OUT "${CMAKE_BINARY_DIR}/plists/${TARGET_NAME}-Info.plist")
    configure_file("${DESKTOP_DIR}/macos/Info.plist.in" "${PLIST_OUT}" @ONLY)

    target_compile_definitions(${TARGET_NAME} PRIVATE
        HDSLAVE_OBJC_VIEW_CLASS=HdslavePanelView_${SLUG}
    )
    target_compile_options(${TARGET_NAME} PRIVATE -O3 -fvisibility=hidden)
    target_link_libraries(${TARGET_NAME} PRIVATE
        "-framework Cocoa"
        "-framework CoreGraphics"
        "-framework QuartzCore"
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_INFO_PLIST "${PLIST_OUT}"
        PREFIX ""
        OUTPUT_NAME "${TARGET_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap"
    )
elseif(WIN32)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        HDSLAVE_WIN32_CLASS_NAME=L"HdslaveDesktopClapView_${SLUG}"
        UNICODE
        _UNICODE
    )
    if(MSVC)
        target_include_directories(${TARGET_NAME} PRIVATE "${DESKTOP_DIR}/win32/compat")
        target_compile_options(${TARGET_NAME} PRIVATE /O2 /EHsc /W3 /wd4244 /wd4996)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -O3)
    endif()
    target_link_libraries(${TARGET_NAME} PRIVATE user32 gdi32)
    set_target_properties(${TARGET_NAME} PROPERTIES
        PREFIX ""
        SUFFIX ".clap"
        OUTPUT_NAME "${TARGET_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap"
    )
endif()

add_executable(host_test "${CMAKE_SOURCE_DIR}/tests/host_test.cpp")
target_include_directories(host_test PRIVATE "${CLAP_INCLUDE_DIR}")
if(APPLE)
    target_link_libraries(host_test PRIVATE dl)
endif()
