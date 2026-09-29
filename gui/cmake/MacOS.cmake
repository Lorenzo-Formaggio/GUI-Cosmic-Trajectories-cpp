# ── macOS toolchain and dependency discovery ─────────────────────────────────
# Included by CMakeLists.txt before project(), so that a plain `cmake ..` (or
# the VS Code CMake extension) configures on any Mac without extra flags:
#
#   1. SDK. Clang builds against the newest SDK that the Command Line Tools or
#      Xcode provide. After a partial update that SDK can be newer than the
#      linker -- e.g. a macOS 27 SDK next to the 26.x ld, which fails every
#      link with "tapi error: unknown architecture arm64e.x1" -- and then
#      nothing links at all. A test program is linked against the default SDK;
#      if that fails, CMAKE_OSX_SYSROOT is pinned to the newest installed SDK
#      that does link. The pin is dropped again as soon as the default SDK
#      links (i.e. once the Command Line Tools are updated).
#
#   2. Dependencies. Qt6 and GSL are searched for in Homebrew (/opt/homebrew
#      on Apple Silicon, /usr/local on Intel), MacPorts (/opt/local) and the Qt
#      online installer (~/Qt/6.x.y/macos), after anything the user passes in
#      CMAKE_PREFIX_PATH or Qt6_DIR.

# Does a small C++ program link with compiler `cxx` against SDK `sysroot`
# (empty = the compiler's default)? The error output goes to `out_err`.
function(_ctg_sdk_links out out_err cxx sysroot)
    set(dir "${CMAKE_BINARY_DIR}/CMakeFiles/ctg_sdk_probe")
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${dir}/probe.cpp"
         "#include <string>\nint main() { return std::string(\"ok\").size() == 2 ? 0 : 1; }\n")
    set(args)
    if(sysroot)
        list(APPEND args -isysroot "${sysroot}")
    endif()
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        list(APPEND args -arch ${arch})
    endforeach()
    execute_process(COMMAND ${cxx} ${args} "${dir}/probe.cpp" -o "${dir}/probe"
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
    if(rc EQUAL 0)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
    set(${out_err} "${err}" PARENT_SCOPE)
endfunction()

function(_ctg_pick_macos_sdk)
    # A sysroot chosen by the user (or by an older CMake) is left alone; only
    # an empty one, or one this file pinned earlier, is (re)checked.
    if(CMAKE_OSX_SYSROOT AND NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "${_CTG_AUTO_SYSROOT}")
        return()
    endif()

    # The compiler project() will pick up.
    if(CMAKE_CXX_COMPILER)
        set(cxx "${CMAKE_CXX_COMPILER}")
    elseif(NOT "$ENV{CXX}" STREQUAL "")
        separate_arguments(cxx UNIX_COMMAND "$ENV{CXX}")
    else()
        set(cxx c++)
    endif()

    _ctg_sdk_links(ok default_err "${cxx}" "")
    if(ok)
        if(_CTG_AUTO_SYSROOT)
            message(STATUS "macOS SDK: the default SDK links again, "
                           "dropping the pin to ${_CTG_AUTO_SYSROOT}")
            set(CMAKE_OSX_SYSROOT "" CACHE PATH "macOS SDK" FORCE)
            unset(_CTG_AUTO_SYSROOT CACHE)
        endif()
        return()
    endif()

    # The default SDK does not link: try every installed macOS SDK.
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
                    OUTPUT_VARIABLE default_sdk
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND xcode-select -p
                    OUTPUT_VARIABLE devdir
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(globs)
    if(default_sdk)
        get_filename_component(d "${default_sdk}" DIRECTORY)
        list(APPEND globs "${d}/MacOSX*.sdk")
    endif()
    if(devdir)
        list(APPEND globs
             "${devdir}/SDKs/MacOSX*.sdk"
             "${devdir}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX*.sdk")
    endif()
    set(candidates)
    if(globs)
        file(GLOB candidates LIST_DIRECTORIES true ${globs})
    endif()

    set(seen)
    set(best "")
    set(best_version "0")
    foreach(sdk IN LISTS candidates)
        get_filename_component(real "${sdk}" REALPATH)
        if(real IN_LIST seen OR NOT IS_DIRECTORY "${real}")
            continue()
        endif()
        list(APPEND seen "${real}")
        set(version "0")
        if(EXISTS "${real}/SDKSettings.json")
            file(READ "${real}/SDKSettings.json" settings)
            if(settings MATCHES "\"Version\"[ \t]*:[ \t]*\"([0-9.]+)\"")
                set(version "${CMAKE_MATCH_1}")
            endif()
        endif()
        if(NOT version VERSION_GREATER best_version AND best)
            continue()
        endif()
        _ctg_sdk_links(ok err "${cxx}" "${real}")
        if(ok)
            set(best "${real}")
            set(best_version "${version}")
        endif()
    endforeach()

    if(NOT best)
        # Not an SDK/linker mismatch (or nothing to fall back on): let
        # project() run its own compiler checks and report the problem.
        message(WARNING "macOS SDK: a test program does not link with the "
                        "default SDK and no installed SDK works either. "
                        "Compiler output:\n${default_err}")
        return()
    endif()

    if(NOT "${best}" STREQUAL "${_CTG_AUTO_SYSROOT}")
        message(WARNING
            "The default macOS SDK (${default_sdk}) does not link with the "
            "installed linker: the SDK is newer than the Command Line Tools. "
            "Building against ${best} (macOS ${best_version}) instead. To fix "
            "the toolchain itself, install the pending Command Line Tools "
            "update (System Settings > General > Software Update, or "
            "`softwareupdate --list` then `softwareupdate -i <label>`); this "
            "workaround switches itself off afterwards.")
    else()
        message(STATUS "macOS SDK: default SDK still does not link, "
                       "keeping ${best}")
    endif()
    set(CMAKE_OSX_SYSROOT "${best}" CACHE PATH "macOS SDK" FORCE)
    set(_CTG_AUTO_SYSROOT "${best}" CACHE INTERNAL
        "macOS SDK pinned by gui/cmake/MacOS.cmake")
endfunction()

function(_ctg_add_macos_prefixes)
    set(prefixes)

    # Homebrew: the prefix `brew shellenv` exports, else the default for this
    # CPU (a Rosetta shell reports x86_64 and should use the Intel install).
    execute_process(COMMAND uname -m OUTPUT_VARIABLE arch
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(arch STREQUAL "arm64")
        set(brew_candidates /opt/homebrew /usr/local)
    else()
        set(brew_candidates /usr/local /opt/homebrew)
    endif()
    if(NOT "$ENV{HOMEBREW_PREFIX}" STREQUAL "")
        list(INSERT brew_candidates 0 "$ENV{HOMEBREW_PREFIX}")
    endif()
    foreach(p IN LISTS brew_candidates)
        if(EXISTS "${p}/bin/brew")
            foreach(keg qt qt@6 gsl)
                if(EXISTS "${p}/opt/${keg}")
                    list(APPEND prefixes "${p}/opt/${keg}")
                endif()
            endforeach()
            list(APPEND prefixes "${p}")
            message(STATUS "Homebrew: ${p}")
            break()
        endif()
    endforeach()

    # MacPorts.
    if(EXISTS /opt/local/bin/port)
        if(EXISTS /opt/local/libexec/qt6)
            list(APPEND prefixes /opt/local/libexec/qt6)
        endif()
        list(APPEND prefixes /opt/local)
    endif()

    # Qt online installer: the newest ~/Qt/6.x.y/macos.
    file(GLOB qt_installs LIST_DIRECTORIES true "$ENV{HOME}/Qt/6.*/macos")
    set(qt_best "")
    set(qt_best_version "0")
    foreach(q IN LISTS qt_installs)
        if(q MATCHES "/Qt/([0-9.]+)/macos$"
           AND CMAKE_MATCH_1 VERSION_GREATER qt_best_version)
            set(qt_best "${q}")
            set(qt_best_version "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(qt_best)
        list(APPEND prefixes "${qt_best}")
    endif()

    list(APPEND CMAKE_PREFIX_PATH ${prefixes})
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
endfunction()

if(CMAKE_HOST_APPLE)
    _ctg_pick_macos_sdk()
    _ctg_add_macos_prefixes()
endif()
