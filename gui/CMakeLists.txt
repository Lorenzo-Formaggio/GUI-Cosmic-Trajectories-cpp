cmake_minimum_required(VERSION 3.16)
project(CosmicTrajectoryGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts DataVisualization)

# ── GSL ──────────────────────────────────────────────────────────────────────
find_package(GSL REQUIRED)

# ── Thermal-FIST (QvdW-HRG seam model of the entropy-contour EoS) ────────────
# HRG.cpp is the quantum van der Waals HRG that supplies the low-temperature
# boundary of EntrCont / EntrContParam, mirroring scontours::QvdWHRG. It needs
# the Thermal-FIST library, which is found in this order:
#
#   1. -DTHERMALFIST_ROOT=<path>       an existing checkout you point at
#   2. <repo>/external/Thermal-FIST    committed in this repository
#   3. downloaded at configure time    FetchContent, needs network once
#
# Path 2 is how this repository ships: the Thermal-FIST source tree is committed
# under external/, so a clone builds offline with no submodule step and no
# network. Paths 1 and 3 are there for anyone who would rather not carry the
# vendored copy. Only the ThermalFIST library target is built; EXCLUDE_FROM_ALL
# keeps Thermal-FIST's own GUI and command-line tools out of our build.
set(THERMALFIST_ROOT "" CACHE PATH
    "Thermal-FIST source tree (QvdW-HRG seam model); empty = auto-detect")
option(CTG_WITH_THERMALFIST
       "Build the QvdW-HRG seam model against Thermal-FIST" ON)
option(CTG_FETCH_THERMALFIST
       "Download Thermal-FIST at configure time if it is not present" ON)
set(THERMALFIST_GIT_TAG "master" CACHE STRING
    "Thermal-FIST revision to download when CTG_FETCH_THERMALFIST is ON")

set(_ctg_tf_dir "")
set(_ctg_tf_added FALSE)
if(CTG_WITH_THERMALFIST)
    if(THERMALFIST_ROOT AND EXISTS "${THERMALFIST_ROOT}/CMakeLists.txt")
        set(_ctg_tf_dir "${THERMALFIST_ROOT}")
        message(STATUS "Thermal-FIST: using THERMALFIST_ROOT=${_ctg_tf_dir}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../external/Thermal-FIST/CMakeLists.txt")
        set(_ctg_tf_dir "${CMAKE_CURRENT_SOURCE_DIR}/../external/Thermal-FIST")
        message(STATUS "Thermal-FIST: using ${_ctg_tf_dir}")
    elseif(CTG_FETCH_THERMALFIST)
        message(STATUS "Thermal-FIST: not found locally, downloading "
                       "(${THERMALFIST_GIT_TAG}) -- set "
                       "-DCTG_FETCH_THERMALFIST=OFF to disable")
        include(FetchContent)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
            # EXCLUDE_FROM_ALL on Declare (3.28+) lets MakeAvailable do the
            # add_subdirectory for us without pulling in Thermal-FIST's own
            # GUI/CLI targets.
            FetchContent_Declare(thermalfist
                GIT_REPOSITORY https://github.com/vlvovch/Thermal-FIST.git
                GIT_TAG ${THERMALFIST_GIT_TAG}
                GIT_SHALLOW TRUE
                EXCLUDE_FROM_ALL)
            FetchContent_MakeAvailable(thermalfist)
            set(_ctg_tf_added TRUE)
        else()
            # Older CMake: populate and add the subdirectory by hand.
            FetchContent_Declare(thermalfist
                GIT_REPOSITORY https://github.com/vlvovch/Thermal-FIST.git
                GIT_TAG ${THERMALFIST_GIT_TAG}
                GIT_SHALLOW TRUE)
            FetchContent_GetProperties(thermalfist)
            if(NOT thermalfist_POPULATED)
                FetchContent_Populate(thermalfist)
            endif()
            set(_ctg_tf_dir "${thermalfist_SOURCE_DIR}")
        endif()
    endif()
endif()

if(_ctg_tf_dir)
    add_subdirectory(${_ctg_tf_dir}
                     ${CMAKE_CURRENT_BINARY_DIR}/Thermal-FIST EXCLUDE_FROM_ALL)
    set(_ctg_tf_added TRUE)
endif()
if(NOT _ctg_tf_added)
    set(CTG_WITH_THERMALFIST OFF)
    message(WARNING
        "Thermal-FIST not available: the QvdW-HRG seam model is not built, so "
        "the Entropy Contour equations of state (EoS 3 and 4) will report the "
        "missing dependency at start-up. The other models are unaffected. "
        "Point -DTHERMALFIST_ROOT=<path> at a checkout, place one under "
        "external/Thermal-FIST, or allow -DCTG_FETCH_THERMALFIST=ON.")
endif()

# ── Source files ─────────────────────────────────────────────────────────────
# Physics library (existing code)
set(PHYSICS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/GetEq.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/InterpolatedEoS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/JEL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/LatticeQCD.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ParameterReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/QCDTherm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/leptonTherm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ContourEoSCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/EntrCont.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/EntrContParam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/HRG.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Metropolis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TRangeManager.cpp
)

# GUI sources
set(GUI_SOURCES
    main.cpp
    MainWindow.cpp
    SimulationWorker.cpp
    CompareWidget.cpp
    EosExplorerWidget.cpp
    RunFromFileWidget.cpp
)

set(GUI_HEADERS
    MainWindow.h
    SimulationWorker.h
    CompareWidget.h
    TooltipChartView.h
    EosExplorerWidget.h
    RunFromFileWidget.h
)

# Apply the Thermal-FIST dependency of the physics sources to a target.
function(ctg_link_thermalfist target)
    if(CTG_WITH_THERMALFIST)
        target_link_libraries(${target} PRIVATE ThermalFIST)
        target_compile_definitions(${target} PRIVATE HRG_WITH_THERMALFIST)
    endif()
endfunction()

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(CosmicTrajectoryGUI
    ${GUI_SOURCES}
    ${GUI_HEADERS}
    ${PHYSICS_SOURCES}
)

target_include_directories(CosmicTrajectoryGUI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GSL_INCLUDE_DIRS}
)

target_link_libraries(CosmicTrajectoryGUI PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::DataVisualization
    ${GSL_LIBRARIES}
)

target_link_directories(CosmicTrajectoryGUI PRIVATE
    ${GSL_LIBRARY_DIRS}
)
ctg_link_thermalfist(CosmicTrajectoryGUI)

add_executable(calc_densities calc_densities.cpp ${PHYSICS_SOURCES})
target_include_directories(calc_densities PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/.. ${GSL_INCLUDE_DIRS})
target_link_libraries(calc_densities PRIVATE ${GSL_LIBRARIES})
target_link_directories(calc_densities PRIVATE ${GSL_LIBRARY_DIRS})
ctg_link_thermalfist(calc_densities)

# ── Cross-check against s_contours_c-dev4-tristan ────────────────────────────
# Prints the contour EoS (P/T^4, s/T^3, eps/T^4, n_B/n_Q/n_S in fm^-3) along a
# line of fixed mu_B, in the same layout as the reference's `eos_line`, so the
# two can be diffed column by column.
add_executable(eos_check eos_check.cpp ${PHYSICS_SOURCES})
target_include_directories(eos_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/.. ${GSL_INCLUDE_DIRS})
target_link_libraries(eos_check PRIVATE ${GSL_LIBRARIES})
target_link_directories(eos_check PRIVATE ${GSL_LIBRARY_DIRS})
ctg_link_thermalfist(eos_check)
