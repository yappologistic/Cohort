cmake_minimum_required(VERSION 3.24)
project(Cohort VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Cohort drives Linux kernel interfaces, so it supports Linux only")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo)
if(ipo)
 set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# The helper is the only program that runs as root. It is built from the
# C++ library alone, so what decides a root write is small enough to audit.
set(COHORT_RULES src/controls.cpp src/controls.h src/keyboard.cpp src/keyboard.h)
add_executable(cohort-helper helper/main.cpp ${COHORT_RULES})
target_include_directories(cohort-helper PRIVATE src)
target_compile_options(cohort-helper PRIVATE -Wall -Wextra -Wpedantic "$<$<CONFIG:Release>:-O2>")
# Hardening for a program polkit runs as root.
target_compile_options(cohort-helper PRIVATE -fstack-protector-strong -fPIE "$<$<CONFIG:Release>:-D_FORTIFY_SOURCE=3>")
target_link_options(cohort-helper PRIVATE -pie -Wl,-z,relro,-z,now)

# Where the installed helper lives. The polkit policy names this exact path,
# and the window runs the helper from here once installed.
set(COHORT_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/cohort-helper")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Svg DBus)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

set(COHORT_SOURCES
 ${COHORT_RULES}
 src/sensors.cpp src/sensors.h
 src/machine.cpp src/machine.h
 src/appearance.cpp src/appearance.h
 src/m3color.cpp src/m3color.h
 src/m3motion.cpp src/m3motion.h
 src/desktoptheme.cpp src/desktoptheme.h)

qt_add_executable(cohort ${COHORT_SOURCES} src/main.cpp)
target_include_directories(cohort PRIVATE src)
target_compile_definitions(cohort PRIVATE COHORT_VERSION="${PROJECT_VERSION}"
                                          COHORT_HELPER_PATH="${COHORT_HELPER_PATH}")
target_link_libraries(cohort PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Svg Qt6::DBus ${CMAKE_DL_LIBS})
target_compile_options(cohort PRIVATE -Wall -Wextra -Wpedantic "$<$<CONFIG:Release>:-Oz;-ffunction-sections;-fdata-sections>")
target_link_options(cohort PRIVATE "$<$<CONFIG:Release>:-Oz;-Wl,--gc-sections;-Wl,-z,pack-relative-relocs>")
set_target_properties(cohort PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_target_properties(cohort PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")

file(GLOB COHORT_QML CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" qml/*.qml)
# The module is embedded where Qt looks for modules by default,
# :/qt/qml/CohortUi (QTP0001), so the installed binary finds it with nothing
# beside it on disk.
qt_add_qml_module(cohort URI CohortUi VERSION 1.0 NO_PLUGIN
 QML_FILES ${COHORT_QML})

file(GLOB icons CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" assets/icons/*.svg)
qt_add_resources(cohort assets PREFIX "/" FILES ${icons}
 OPTIONS --compress 19 --threshold 0)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
 add_custom_command(TARGET cohort POST_BUILD COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:cohort>)
 add_custom_command(TARGET cohort-helper POST_BUILD COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:cohort-helper>)
endif()
# The window looks for the helper beside itself when it has not been
# installed, so a build tree works without an install.
add_dependencies(cohort cohort-helper)

configure_file(assets/io.github.yappologistic.Cohort.policy.in
               "${CMAKE_CURRENT_BINARY_DIR}/io.github.yappologistic.Cohort.policy" @ONLY)

install(TARGETS cohort RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS cohort-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/io.github.yappologistic.Cohort.policy"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES assets/io.github.yappologistic.Cohort.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/io.github.yappologistic.Cohort.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES assets/io.github.yappologistic.Cohort.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# Distributions whose packaging installs licenses itself (RPM's %license)
# turn this off.
option(COHORT_INSTALL_LICENSES "Install the license texts" ON)
if(COHORT_INSTALL_LICENSES)
 install(FILES LICENSE NOTICE licenses/MaterialSymbols-LICENSE.txt licenses/Sung-MIT.txt
         DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cohort)
endif()

include(CTest)
if(BUILD_TESTING)
 find_package(Qt6 REQUIRED COMPONENTS Test)
 # The rules the helper enforces, against fixture trees laid out the way
 # each driver lays out sysfs.
 qt_add_executable(cohort-controls-tests tests/controls_test.cpp tests/fixture.h ${COHORT_RULES})
 target_include_directories(cohort-controls-tests PRIVATE src tests)
 target_link_libraries(cohort-controls-tests PRIVATE Qt6::Core Qt6::Test)
 add_test(NAME controls COMMAND cohort-controls-tests)

 # The machine layer as the window drives it, writing into a fixture.
 qt_add_executable(cohort-machine-tests tests/machine_test.cpp tests/fixture.h
                   ${COHORT_RULES} src/sensors.cpp src/sensors.h src/machine.cpp src/machine.h)
 target_include_directories(cohort-machine-tests PRIVATE src tests)
 target_compile_definitions(cohort-machine-tests PRIVATE COHORT_HELPER_PATH="/nonexistent")
 target_link_libraries(cohort-machine-tests PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Test ${CMAKE_DL_LIBS})
 add_test(NAME machine COMMAND cohort-machine-tests)
 set_tests_properties(machine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

 # Writes a fixture machine to disk, for scripts/capture.sh.
 qt_add_executable(cohort-fixture tests/make_fixture.cpp tests/fixture.h)
 target_include_directories(cohort-fixture PRIVATE tests)
 target_link_libraries(cohort-fixture PRIVATE Qt6::Core)

 qt_add_executable(cohort-m3motion-tests tests/m3motion_test.cpp src/m3motion.cpp src/m3motion.h)
 target_include_directories(cohort-m3motion-tests PRIVATE src)
 target_link_libraries(cohort-m3motion-tests PRIVATE Qt6::Core Qt6::Test)
 add_test(NAME m3motion COMMAND cohort-m3motion-tests)
endif()

# The simulated user and its captures. Never in the shipped binary.
option(COHORT_DIAGNOSTICS "Build the UI verification harness into cohort" OFF)
if(COHORT_DIAGNOSTICS)
 find_package(Qt6 REQUIRED COMPONENTS Test)
 target_sources(cohort PRIVATE tests/uitest.cpp tests/uitest.h)
 target_include_directories(cohort PRIVATE tests)
 target_compile_definitions(cohort PRIVATE COHORT_DIAGNOSTICS)
 target_link_libraries(cohort PRIVATE Qt6::Test)
endif()
