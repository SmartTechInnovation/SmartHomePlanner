cmake_minimum_required(VERSION 3.21)
project(SmartHomePlanner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Xml PrintSupport LinguistTools)
find_package(Qt6 6.8 QUIET COMPONENTS Pdf)   # optional: import PDF floor plans as background

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/core/Library.h              src/core/Library.cpp
    src/core/Geometry.h             src/core/Geometry.cpp
    src/core/ProjectInfo.h
    src/core/Project.h              src/core/Project.cpp
    src/core/IconCache.h            src/core/IconCache.cpp
    src/scene/ItemTypes.h
    src/scene/BackgroundItem.h      src/scene/BackgroundItem.cpp
    src/scene/HandleItem.h          src/scene/HandleItem.cpp
    src/scene/WallItem.h            src/scene/WallItem.cpp
    src/scene/RoomItem.h            src/scene/RoomItem.cpp
    src/scene/NoteItem.h            src/scene/NoteItem.cpp
    src/scene/DeviceItem.h          src/scene/DeviceItem.cpp
    src/scene/EffectItem.h          src/scene/EffectItem.cpp
    src/scene/StripItem.h           src/scene/StripItem.cpp
    src/scene/GroupItem.h           src/scene/GroupItem.cpp
    src/scene/PanelItems.h          src/scene/PanelItems.cpp
    src/scene/PlanScene.h           src/scene/PlanScene.cpp
    src/scene/PlanView.h            src/scene/PlanView.cpp
    src/scene/SceneSerializer.h     src/scene/SceneSerializer.cpp
    src/ui/MainWindow.h             src/ui/MainWindow.cpp
    src/ui/LibraryPanel.h           src/ui/LibraryPanel.cpp
    src/ui/PropertiesPanel.h        src/ui/PropertiesPanel.cpp
    src/ui/SectionPanel.h           src/ui/SectionPanel.cpp
    src/ui/CheckDelegate.h          src/ui/CheckDelegate.cpp
    src/ui/ProjectTree.h            src/ui/ProjectTree.cpp
    src/ui/ProjectInfoDialog.h      src/ui/ProjectInfoDialog.cpp
    src/ui/ExportDialog.h           src/ui/ExportDialog.cpp
    src/export/PdfExporter.h        src/export/PdfExporter.cpp
)

# everything except main.cpp goes in a static library, shared by the app and the tests
set(CORE_SOURCES ${SOURCES})
list(REMOVE_ITEM CORE_SOURCES src/main.cpp)
qt_add_library(shp_core STATIC ${CORE_SOURCES})
target_include_directories(shp_core PUBLIC src)
target_link_libraries(shp_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Xml Qt6::PrintSupport)
if(Qt6Pdf_FOUND)
    target_link_libraries(shp_core PUBLIC Qt6::Pdf)
    target_compile_definitions(shp_core PUBLIC SHP_HAVE_QTPDF)
endif()
# Development builds read Libraries/ straight from the source tree (XML edits apply at once).
# Release packages (tools/package_*.sh) turn this off, so they only use the libraries in the app.
option(SHP_DEV_LIBRARY_PATH "Read Libraries/ from the source tree" ON)
target_compile_definitions(shp_core PUBLIC SHP_VERSION="${PROJECT_VERSION}")
if(SHP_DEV_LIBRARY_PATH)
    target_compile_definitions(shp_core PUBLIC SHP_SOURCE_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Libraries")
endif()

qt_add_executable(SmartHomePlanner WIN32 MACOSX_BUNDLE src/main.cpp)

# ---- application identity (bundle, icon, version)
if(APPLE)
    set(SHP_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.icns)
    set_source_files_properties(${SHP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(SmartHomePlanner PRIVATE ${SHP_ICNS})
    set_target_properties(SmartHomePlanner PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Smart Home Planner"
        MACOSX_BUNDLE_GUI_IDENTIFIER "md.smartinnovation.smarthomeplanner"
        MACOSX_BUNDLE_ICON_FILE app.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "Smart Tech Innovation")
elseif(WIN32)
    target_sources(SmartHomePlanner PRIVATE resources/app.rc)
endif()
file(GLOB SHP_ICONS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/icons/*.svg)
qt_add_resources(shp_core "app" PREFIX "/" FILES resources/app.svg ${SHP_ICONS})

target_link_libraries(SmartHomePlanner PRIVATE shp_core)
# Note: SHP_SOURCE_LIBRARY_DIR lets a development build always find Libraries/ from the
# source tree, so editing Libraries/*/*.xml takes effect without rebuilding.

# Translations (Romanian). Refresh .ts from sources:  cmake --build . --target update_translations
qt_add_translations(SmartHomePlanner
    TS_FILES translations/SmartHomePlanner_ro.ts
    RESOURCE_PREFIX /i18n)

# Copy the libraries next to the executable (or into the macOS bundle)
if(APPLE)
    set(SHP_LIB_DEST "$<TARGET_BUNDLE_CONTENT_DIR:SmartHomePlanner>/Resources/Libraries")
else()
    set(SHP_LIB_DEST "$<TARGET_FILE_DIR:SmartHomePlanner>/Libraries")
endif()
# runs on every build (not only when the app is relinked), so XML edits always reach the bundle
add_custom_target(shp_libraries ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Libraries ${SHP_LIB_DEST}
    COMMENT "Copying Libraries")
add_dependencies(shp_libraries SmartHomePlanner)

# Tests:  cmake --build . --target selftest && ctest
option(SHP_BUILD_TESTS "Build the interaction tests" ON)
find_package(Qt6 6.8 QUIET COMPONENTS Test)
if(SHP_BUILD_TESTS AND Qt6Test_FOUND)
    enable_testing()
    qt_add_executable(selftest tests/selftest.cpp)
    target_link_libraries(selftest PRIVATE shp_core Qt6::Test)
    add_test(NAME selftest COMMAND selftest -platform offscreen)
endif()

include(GNUInstallDirs)
install(TARGETS SmartHomePlanner
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NOT APPLE)
    install(DIRECTORY Libraries DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# copies the Qt libraries and plugins into the installed app (macdeployqt / windeployqt)
qt_generate_deploy_app_script(
    TARGET SmartHomePlanner
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

# ---- packages:  cpack  (macOS: .dmg, Windows: .zip)
set(CPACK_PACKAGE_NAME "SmartHomePlanner")
set(CPACK_PACKAGE_VENDOR "Smart Tech Innovation")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Smart home installation planner")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Smart Home Planner")
    set(CPACK_SYSTEM_NAME "macOS")
else()
    set(CPACK_GENERATOR "ZIP")
endif()
include(CPack)
