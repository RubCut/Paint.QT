cmake_minimum_required(VERSION 3.21)
project(PaintNetQt VERSION 1.0.2 LANGUAGES CXX)

# One place for the facts every package format needs.
set(PNQ_APP_ID "io.github.RubCut.Paint.QT")
set(PNQ_HOMEPAGE "https://github.com/RubCut/Paint.QT")
set(PNQ_ISSUES "https://github.com/RubCut/Paint.QT/issues")
set(PNQ_LICENSE "GPL-3.0-or-later")
set(PNQ_SUMMARY "A raster image editor for Linux desktops, with the tool set of Paint.NET")
set(PNQ_DESCRIPTION
    "Paint.QT is a raster image editor written in C++20 on Qt 6. The tools and the palette\nlayout are the ones people know from Paint.NET, and so are two further things: it reads and\nwrites the .pdq project format, and its history treats one stroke as one undo step. It\ncontains no Paint.NET code. The window draws its own light chrome, so it looks the same on\nevery desktop.")
set(PNQ_MAINTAINER "Paint.QT contributors")
set(PNQ_PACKAGE_NAME "paint-qt")
set(PNQ_DEB_DEPENDS "libqt6core6t64, libqt6gui6, libqt6widgets6, libqt6svg6")
set(PNQ_RPM_DEPENDS "qt6-qtbase, qt6-qtsvg")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(Qt6 QUIET COMPONENTS PrintSupport Svg)
find_package(Qt6 QUIET COMPONENTS Test)

# The mark is drawn procedurally in src/resources/Icons.cpp and additionally
# shipped as an SVG that is rasterised at build time. Without Qt's Svg module
# the shipped files cannot be produced, so the packaging switches themselves off
# rather than emitting a package with no icon in it.
if(Qt6Svg_FOUND)
  set(PNQ_HAVE_SVG ON)
else()
  set(PNQ_HAVE_SVG OFF)
  message(STATUS "Qt6 Svg not found: the shipped icon files will not be generated. "
                 "The application still draws its own icons at runtime. Install "
                 "libqt6svg6-dev (Debian) or qt6-svg (Fedora, Arch) to package it.")
endif()

# --------------------------------------------------------------------------
# pnq_core - everything that does not depend on the GUI.
# --------------------------------------------------------------------------
add_library(pnq_core STATIC
  src/core/BlendMode.cpp
  src/core/Brush.cpp
  src/core/ColorUtils.cpp
  src/core/Document.cpp
  src/core/Gradient.cpp
  src/core/History.cpp
  src/core/ImageMath.cpp
  src/core/ImageOps.cpp
  src/core/Layer.cpp
  src/core/Renderer.cpp
  src/core/Selection.cpp
  src/core/Surface.cpp
  src/effects/Adjustments.cpp
  src/effects/BlurEffects.cpp
  src/effects/Effects.cpp
  src/effects/NoiseEffects.cpp
  src/effects/StylizeEffects.cpp
  src/io/FileFormats.cpp
  src/io/Pdn3Reader.cpp
  src/io/PdqFile.cpp
  src/resources/Icons.cpp
  src/resources/Palettes.cpp
  src/core/BlendMode.h
  src/core/Brush.h
  src/core/ColorUtils.h
  src/core/Document.h
  src/core/Gradient.h
  src/core/History.h
  src/core/ImageMath.h
  src/core/ImageOps.h
  src/core/Layer.h
  src/core/Renderer.h
  src/core/Selection.h
  src/core/Surface.h
  src/effects/Effects.h
  src/io/FileFormats.h
  src/io/Pdn3Reader.h
  src/io/PdqFile.h
  src/resources/Icons.h
  src/resources/Palettes.h
)
target_include_directories(pnq_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# zlib is used only to inflate the pixel blocks in Paint.NET project files, which
# are gzipped. Qt links it privately, so it has to be named here as well.
find_package(ZLIB REQUIRED)
target_link_libraries(pnq_core PUBLIC Qt6::Core Qt6::Gui ZLIB::ZLIB)
set_target_properties(pnq_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --------------------------------------------------------------------------
# pnq_tools - the tools and their registry.
# --------------------------------------------------------------------------
add_library(pnq_tools STATIC
  src/tools/DrawingTools.cpp
  src/tools/FillTools.cpp
  src/tools/SelectionTools.cpp
  src/tools/ShapeTools.cpp
  src/tools/TextTool.cpp
  src/tools/Tool.cpp
  src/tools/ToolManager.cpp
  src/tools/ToolSupport.cpp
  src/tools/TransformTools.cpp
  src/tools/Tool.h
  src/tools/ToolManager.h
  src/tools/ToolSupport.h
)
target_include_directories(pnq_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pnq_tools PUBLIC pnq_core Qt6::Widgets)
set_target_properties(pnq_tools PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --------------------------------------------------------------------------
# pnq_ui - palettes, dialogs and the main window.
# --------------------------------------------------------------------------
add_library(pnq_ui STATIC
  src/io/RecentFiles.cpp
  src/io/RecentFiles.h
  src/ui/AdjustmentsDialog.cpp
  src/ui/AdjustmentsDialog.h
  src/ui/BlurDialog.cpp
  src/ui/BlurDialog.h
  src/ui/BrushesPane.cpp
  src/ui/BrushesPane.h
  src/ui/CanvasView.cpp
  src/ui/CanvasView.h
  src/ui/ClipboardBridge.cpp
  src/ui/ClipboardBridge.h
  src/ui/ColorPane.cpp
  src/ui/ColorWheel.cpp
  src/ui/ColorWheel.h
  src/ui/ColorPane.h
  src/ui/EffectsDialog.cpp
  src/ui/EffectsDialog.h
  src/ui/GradientPane.cpp
  src/ui/GradientPane.h
  src/ui/HistoryPane.cpp
  src/ui/HistoryPane.h
  src/ui/LayersPane.cpp
  src/ui/LayersPane.h
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
  src/ui/NoiseDialog.cpp
  src/ui/NoiseDialog.h
  src/ui/PropertyPane.cpp
  src/ui/PropertyPane.h
  src/ui/ShapesPane.cpp
  src/ui/ShapesPane.h
  src/ui/StatusPane.cpp
  src/ui/PalettePanel.cpp
  src/ui/PalettePanel.h
  src/ui/Theme.cpp
  src/ui/Theme.h
  src/ui/StatusPane.h
  src/ui/StylizeDialog.cpp
  src/ui/StylizeDialog.h
  src/ui/TextPane.cpp
  src/ui/TextPane.h
  src/ui/ToolOptions.cpp
  src/ui/ToolOptions.h
  src/ui/dialogs/AboutDialog.cpp
  src/ui/dialogs/AboutDialog.h
  src/ui/dialogs/BrushEditorDialog.cpp
  src/ui/dialogs/CanvasSizeDialog.cpp
  src/ui/dialogs/ColorPickerDialog.cpp
  src/ui/dialogs/Dialogs.h
  src/ui/dialogs/GradientEditorDialog.cpp
  src/ui/dialogs/GradientEditorDialog.h
  src/ui/dialogs/LayerPropertiesDialog.cpp
  src/ui/dialogs/NewImageDialog.cpp
  src/ui/dialogs/PreferencesDialog.cpp
  src/ui/dialogs/PreferencesDialog.h
  src/ui/dialogs/ResizeDialog.cpp
  src/ui/dialogs/ShortcutDisplayWidget.cpp
  src/ui/dialogs/ShortcutDisplayWidget.h
  src/ui/dialogs/ShortcutEditorDialog.cpp
  src/ui/dialogs/ShortcutEditorDialog.h
)
target_include_directories(pnq_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pnq_ui PUBLIC pnq_tools Qt6::Widgets)
set_target_properties(pnq_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(Qt6PrintSupport_FOUND)
  target_link_libraries(pnq_ui PUBLIC Qt6::PrintSupport)
  target_compile_definitions(pnq_ui PUBLIC PNQ_PRINT_SUPPORT)
endif()

# --------------------------------------------------------------------------
# Generated version header. Single source of truth for the version string, so
# the About box, --version and every package cannot disagree about it.
# --------------------------------------------------------------------------
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/packaging/paint-qt.version.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pnq_version.h"
  @ONLY)
target_sources(pnq_ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pnq_version.h")

# --------------------------------------------------------------------------
# Icon files, generated from the packaged SVG.
#
# Rendering happens with the same Qt SVG rasteriser the icon test compares
# against, so the files that ship cannot drift from the mark the window draws.
# --------------------------------------------------------------------------
include(GNUInstallDirs)

option(PNQ_PACKAGE_ICONS "Generate the PNG/ICO/ICNS icon files" ON)
if(PNQ_PACKAGE_ICONS AND PNQ_HAVE_SVG)
  add_executable(pnq_make_icons tools/make_icons.cpp)
  target_link_libraries(pnq_make_icons PRIVATE Qt6::Svg Qt6::Gui)

  set(PNQ_ICON_DIR "${CMAKE_BINARY_DIR}/generated/icons")
  set(PNQ_ICON_SIZES 16 24 32 48 64 128 256 512)
  set(PNQ_ICON_OUTPUTS)
  foreach(size IN LISTS PNQ_ICON_SIZES)
    list(APPEND PNQ_ICON_OUTPUTS "${PNQ_ICON_DIR}/paint-qt-${size}.png")
  endforeach()
  list(APPEND PNQ_ICON_OUTPUTS "${PNQ_ICON_DIR}/paint-qt.ico" "${PNQ_ICON_DIR}/paint-qt.icns")

  # One call produces every file, so a single custom command covers them all.
  add_custom_command(
    OUTPUT ${PNQ_ICON_OUTPUTS}
    COMMAND pnq_make_icons "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/paint-qt.svg"
            "${PNQ_ICON_DIR}"
    DEPENDS pnq_make_icons "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/paint-qt.svg"
    COMMENT "Rendering the application mark to PNG, ICO and ICNS"
    VERBATIM)
  add_custom_target(pnq_icons_generated ALL DEPENDS ${PNQ_ICON_OUTPUTS})

  # hicolor wants one file per size per directory, named after the icon. That is
  # how a desktop finds the 32 px one for a panel and the 256 px one for a
  # file manager's large view without either being scaled from the other.
  # Installed under the application id rather than under the binary name. Flatpak
  # only exports the files whose names match the id, and it said so: "Not
  # exporting share/icons/hicolor/48x48/apps/paint-qt.png, non-allowed export
  # filename". The result was a Flatpak with no icon at all. Flathub requires the
  # same naming, and so does the desktop entry.
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/paint-qt.svg"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
    RENAME "${PNQ_APP_ID}.svg")
  install(FILES "${PNQ_ICON_DIR}/paint-qt.ico"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor"
    RENAME "${PNQ_APP_ID}.ico")
  foreach(size IN LISTS PNQ_ICON_SIZES)
    install(FILES "${PNQ_ICON_DIR}/paint-qt-${size}.png"
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps"
      RENAME "${PNQ_APP_ID}.png")
  endforeach()
endif()

# --------------------------------------------------------------------------
# The application.
# --------------------------------------------------------------------------
add_executable(paint-qt MACOSX_BUNDLE src/main.cpp)
target_link_libraries(paint-qt PRIVATE pnq_ui Qt6::Concurrent)
if(Qt6Svg_FOUND)
  target_link_libraries(paint-qt PRIVATE Qt6::Svg)
  target_compile_definitions(paint-qt PRIVATE PNQ_SVG_SUPPORT)
endif()

# ---- macOS: the bundle gets the .icns as its document icon -----------------
if(APPLE AND PNQ_PACKAGE_ICONS AND PNQ_HAVE_SVG)
  target_sources(paint-qt PRIVATE
    "${PNQ_ICON_DIR}/paint-qt.icns")
  set_source_files_properties("${PNQ_ICON_DIR}/paint-qt.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set_target_properties(paint-qt PROPERTIES
    MACOSX_BUNDLE_ICON_FILE "paint-qt.icns"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")
  add_dependencies(paint-qt pnq_icons_generated)
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
option(PNQ_BUILD_TESTS "Build the test suite" ON)
if(PNQ_BUILD_TESTS AND Qt6Test_FOUND)
  enable_testing()

  add_executable(pnq_tests tests/test_main.cpp)
  target_link_libraries(pnq_tests PRIVATE pnq_core Qt6::Test)
  add_test(NAME core_tests COMMAND pnq_tests)

  # GUI smoke test: builds the real window, renders it and drives the tools.
  add_executable(pnq_smoke tests/smoke_test.cpp)
  target_link_libraries(pnq_smoke PRIVATE pnq_ui)
  add_test(NAME gui_smoke COMMAND pnq_smoke)

  # Drives the pencil through real Qt mouse events so that a broken event
  # path (a drag painting nothing) cannot hide behind the tool-level tests.
  add_executable(pnq_drag tests/drag_probe.cpp)
  target_link_libraries(pnq_drag PRIVATE pnq_ui)
  add_test(NAME drag_paint COMMAND pnq_drag)

  # Exercises every tool through real mouse events. A tool that does not paint
  # cannot hide behind a unit test that only checks it can be constructed.
  add_executable(pnq_tool_audit tests/tool_audit.cpp)
  target_link_libraries(pnq_tool_audit PRIVATE pnq_ui)
  add_test(NAME tool_audit COMMAND pnq_tool_audit)

  # Developer helper: draws with a spread of tools and saves /tmp/pnq_draw.png so
  # the result can be looked at rather than only asserted about.
  add_executable(pnq_draw_shot tests/draw_shot.cpp)
  target_link_libraries(pnq_draw_shot PRIVATE pnq_ui)

  # Checks that every tool and command has its own artwork and that the mark
  # exists at every size a desktop asks for, then writes /tmp/pnq_icons.png.
  add_executable(pnq_icons tests/icon_sheet.cpp)
  target_link_libraries(pnq_icons PRIVATE pnq_ui)
  # The SVG and the drawn mark have to agree, which means comparing a file on
  # disk against the drawing code.
  target_compile_definitions(pnq_icons PRIVATE PNQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(TARGET Qt6::Svg)
    target_link_libraries(pnq_icons PRIVATE Qt6::Svg)
  endif()
  add_test(NAME icons COMMAND pnq_icons)
  set_tests_properties(icons PROPERTIES TIMEOUT 120)

  # Renders the application screenshots the AppStream metainfo references, so
  # they cannot drift from the interface they show.
  add_executable(pnq_screenshots tests/screenshots.cpp)
  target_link_libraries(pnq_screenshots PRIVATE pnq_ui)

  # Checks every file the packaging reads: the desktop entry, the AppStream
  # metainfo, the icon files, the flatpak manifest and the PKGBUILD. A release
  # nobody looked at is the failure this prevents.
  add_executable(pnq_packages tests/package_audit.cpp)
  # Qt6::Gui for QImage, which is how the shipped PNGs are read back.
  target_link_libraries(pnq_packages PRIVATE Qt6::Core Qt6::Gui)
  target_compile_definitions(pnq_packages PRIVATE PNQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME packages COMMAND pnq_packages)
  set_tests_properties(packages PROPERTIES TIMEOUT 120)

  # Every suite runs headless. Five of the six build real windows, and without
  # this they abort with "could not connect to display" on a build machine, which
  # is how two of them were failing on CI while passing locally by hand.
  set_tests_properties(core_tests gui_smoke drag_paint tool_audit icons packages
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------
install(TARGETS paint-qt
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE  DESTINATION .)

# ---- Linux desktop integration -------------------------------------------
# Named after the id, not the binary, for the same reason as the icons.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/paint-qt.desktop"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
  RENAME "${PNQ_APP_ID}.desktop")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/io.github.RubCut.Paint.QT.metainfo.xml"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
# The screenshots are referenced from the metainfo by URL, not shipped inside the
# package: Flathub requires a direct link to the image, and appstreamcli rejects
# a relative path with "web-url-expected". They stay in the repository because
# that URL points at them.

# The licence goes to licenses/<package>, which is where Arch and RPM expect it.
# It used to go to doc/<package>, but CPACK_PACKAGE_NAME was still undefined at
# this point in the file, so the destination collapsed to a bare doc and the
# package shipped /usr/share/doc/LICENSE with no package directory in it. Debian
# gets its own copy as doc/<package>/copyright, which CPack writes from
# CPACK_RESOURCE_FILE_LICENSE further down.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PNQ_PACKAGE_NAME}")
  # Debian policy wants the licence at doc/<package>/copyright, under a name dpkg
  # and lintian look for, and CPack has no setting that produces it. The rule
  # cannot be limited to the DEB generator: install() is expanded when CMake is
  # configured, and CPACK_GENERATOR is empty until cpack runs, so a guard around
  # it is always false. The licence therefore ships at both paths, which is
  # duplication the other formats do not need but neither of them objects to.
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PNQ_PACKAGE_NAME}"
    RENAME copyright)
endif()

# --------------------------------------------------------------------------
# CPack. One configuration, many formats, all reading the same variables above,
# so a version bump cannot update the tarball and forget the .deb.
# --------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "${PNQ_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "Paint.QT contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PNQ_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PNQ_HOMEPAGE}")
set(CPACK_PACKAGE_CONTACT "Paint.QT contributors <boldbalabol@gmail.com>")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Paint.QT")
set(CPACK_PACKAGE_EXECUTABLES "paint-qt;Paint.QT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Debian and RPM each want their own field names.
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PNQ_HOMEPAGE}")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libqt6svg6")
# SHLIBDEPS reads the real library versions out of the linked binary. It needs
# the dpkg tools, which a build machine does not always have, so an explicit
# dependency list is the fallback rather than a package with no dependencies at
# all: CPack warns "the package will have no dependencies" without it.
find_program(CPACK_DPKG_DEB dpkg-deb)
if(CPACK_DPKG_DEB)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
else()
  message(STATUS "dpkg-deb not found: using the declared Debian dependencies "
                 "instead of resolving them from the binary")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "${PNQ_DEB_DEPENDS}")
endif()
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# CPack asks dpkg for the architecture and writes an empty one when dpkg is
# absent, which produces a .deb no installer will accept. Derive it from the
# compiler's target when the query cannot be answered.
if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
  else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endif()

set(CPACK_RPM_PACKAGE_GROUP "Applications/Graphics")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_URL "${PNQ_HOMEPAGE}")
# The SVG module is optional in the build but the .desktop file and the About box
# use it, so it is a real dependency rather than a suggestion.
set(CPACK_RPM_PACKAGE_REQUIRES "${PNQ_RPM_DEPENDS}")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

# Only the build outputs and the version control metadata are left out; the
# packaging definitions and the prebuilt icon files are part of what a release
# tarball has to contain, because the flatpak manifest and the PKGBUILD both
# build from it.
set(CPACK_SOURCE_IGNORE_FILES "/build/;/\\.git/;\\.gitignore;\\.github/;/packaging/flatpak/flatpak-repo/")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_VERBATIM_VARIABLES ON)

# macOS packages only make sense when built there; Linux gets TGZ, DEB and RPM.
if(APPLE)
  set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
  # rpmbuild is part of rpm and is not installed everywhere. Listing RPM as a
  # generator on a machine without it makes `cpack` fail after it has already
  # produced the other formats, which looks like a packaging bug. Offer it only
  # where it can actually run, and say so.
  find_program(CPACK_RPMBUILD rpmbuild)
  if(CPACK_RPMBUILD)
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
  else()
    set(CPACK_GENERATOR "TGZ;DEB")
    message(STATUS "rpmbuild not found: `cpack` will produce the tarball and the .deb, "
                   "but not an .rpm. Install rpm (Debian/Ubuntu) or rpm-build "
                   "(Fedora/Arch) to package it.")
  endif()
endif()

include(CPack)
