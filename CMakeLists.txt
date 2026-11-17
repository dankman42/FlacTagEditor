cmake_minimum_required(VERSION 3.15)
project(FlacTagEditor VERSION 4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Two front ends share the same TagLib-based tag code:
#   Windows -> main.cpp      (native Win32 API + GDI+)
#   Linux   -> main_gtk.cpp  (GTK 3 + GdkPixbuf)
#
# TagLib:
#   * Set TAGLIB_ROOT to the install prefix of a static TagLib build to link
#     it statically (this is how the release binaries are built; see README).
#   * Otherwise: vcpkg / CMake config package on Windows, pkg-config on Linux.
if (DEFINED TAGLIB_ROOT)
    add_library(TagLib::tag STATIC IMPORTED)
    set_target_properties(TagLib::tag PROPERTIES
        IMPORTED_LOCATION "${TAGLIB_ROOT}/lib/libtag.a"
        INTERFACE_INCLUDE_DIRECTORIES "${TAGLIB_ROOT}/include;${TAGLIB_ROOT}/include/taglib"
        INTERFACE_COMPILE_DEFINITIONS "TAGLIB_STATIC")
elseif (WIN32)
    find_package(TagLib CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
    add_library(TagLib::tag ALIAS PkgConfig::TAGLIB)
endif()

set(TAG_SOURCES
    TagIO.cpp
    TagIO.h
    FlacTagIO.cpp
    FlacTagIO.h
    Mp3AiffTagIO.cpp
    Mp3AiffTagIO.h
    TagFields.h
    PathUtil.h
)

if (WIN32)
    add_executable(FlacTagEditor WIN32
        main.cpp
        CoverArt.cpp
        CoverArt.h
        resource.h
        ${TAG_SOURCES}
    )

    target_compile_definitions(FlacTagEditor PRIVATE UNICODE=1 _UNICODE=1)

    if (MINGW)
        # Unicode entry point (wWinMain), and a fully static, stripped binary
        # so the .exe runs on any Windows machine with no DLLs alongside it.
        target_link_options(FlacTagEditor PRIVATE
            -municode -static -static-libgcc -static-libstdc++ -s)
    endif()

    target_link_libraries(FlacTagEditor PRIVATE
        TagLib::tag
        comctl32
        ole32
        shell32
        uuid
        gdiplus
        gdi32
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

    add_executable(FlacTagEditor
        main_gtk.cpp
        ${TAG_SOURCES}
    )

    # libstdc++/libgcc linked statically so the binary only depends on
    # GTK 3 and glibc from the host system.
    target_link_options(FlacTagEditor PRIVATE -static-libgcc -static-libstdc++ -s)

    target_link_libraries(FlacTagEditor PRIVATE
        TagLib::tag
        PkgConfig::GTK3
    )

    install(TARGETS FlacTagEditor RUNTIME DESTINATION bin)
    install(FILES packaging/flac-tag-editor.desktop DESTINATION share/applications)
endif()
