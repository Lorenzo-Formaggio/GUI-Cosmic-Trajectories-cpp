cmake_minimum_required(VERSION 3.16)
project(CosmicTrajectoryGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

# ── GSL ──────────────────────────────────────────────────────────────────────
find_package(GSL REQUIRED)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/EntrCont.cpp
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
)

set(GUI_HEADERS
    MainWindow.h
    SimulationWorker.h
    CompareWidget.h
    TooltipChartView.h
    EosExplorerWidget.h
)

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
    ${GSL_LIBRARIES}
)

target_link_directories(CosmicTrajectoryGUI PRIVATE
    ${GSL_LIBRARY_DIRS}
)
