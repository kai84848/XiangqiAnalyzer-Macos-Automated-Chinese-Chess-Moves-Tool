cmake_minimum_required(VERSION 3.21)
project(XiangqiAnalyzer VERSION 1.0.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent)

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH "ONNX Runtime native SDK")
set(RECOGNITION_MODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/theone1006" CACHE PATH "Xiangqi recognition ONNX models")
set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.23.2.dylib" CACHE FILEPATH "ONNX Runtime shared library")
if(NOT EXISTS "${ONNXRUNTIME_LIBRARY}")
    unset(ONNXRUNTIME_LIBRARY CACHE)
    find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime PATHS /opt/homebrew/lib /usr/local/lib)
endif()
if(NOT ONNXRUNTIME_LIBRARY OR NOT EXISTS "${ONNXRUNTIME_LIBRARY}")
    message(FATAL_ERROR "ONNX Runtime is required. Run tools/extract_onnxruntime.py or brew install onnxruntime.")
endif()
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include" CACHE PATH "ONNX Runtime headers")
if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    unset(ONNXRUNTIME_INCLUDE_DIR CACHE)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h PATHS /opt/homebrew/include /usr/local/include)
endif()
add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
)

set(PIKAFISH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikafish")
set(DEFAULT_PIKAFISH_PATH "${PIKAFISH_DIR}/Pikafish-MacOS-universal")
if(APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    # The supplied universal x86 slice dispatches to AVX-512 under Rosetta.
    # Use a dedicated AVX2 build for Intel Macs and translated execution.
    set(DEFAULT_PIKAFISH_PATH "${PIKAFISH_DIR}/Pikafish-MacOS-x86_64-avx2")
endif()

qt_add_executable(XiangqiAnalyzer MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/boardwidget.h
    src/boardwidget.cpp
    src/position.h
    src/position.cpp
    src/autoplaylogic.h
    src/autoplaylogic.cpp
    src/pikafishengine.h
    src/pikafishengine.cpp
    src/evaluationbar.h
    src/evaluationbar.cpp
    src/variationpreviewdialog.h
    src/variationpreviewdialog.cpp
    src/boardrecognizer.h
    src/boardrecognizer.cpp
    src/piecedetector.h
    src/piecedetector.cpp
    src/recognitiondialog.h
    src/recognitiondialog.cpp
    src/tiantiancontroller.h
    src/tiantiancontroller.cpp
    src/windowpickerdialog.h
    src/windowpickerdialog.cpp
)

target_include_directories(XiangqiAnalyzer PRIVATE src)
target_compile_definitions(XiangqiAnalyzer PRIVATE DEFAULT_PIKAFISH_PATH="${DEFAULT_PIKAFISH_PATH}"
    RECOGNITION_MODEL_DIR="${RECOGNITION_MODEL_DIR}"
    PIECE_DETECTION_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/large.onnx"
    RECOGNITION_STYLE_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/resources/recognition/wooden-initial.png")
target_link_libraries(XiangqiAnalyzer PRIVATE Qt6::Widgets Qt6::Concurrent onnxruntime)
if(APPLE)
    set(XIANGQI_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/XiangqiAnalyzer.icns")
    set_source_files_properties("${XIANGQI_APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(XiangqiAnalyzer PRIVATE src/macautomation.h src/macautomation.mm "${XIANGQI_APP_ICON}")
    target_link_libraries(XiangqiAnalyzer PRIVATE
        "-framework CoreGraphics"
        "-framework ApplicationServices"
        "-framework ScreenCaptureKit"
        "-framework AppKit"
    )
endif()
set_target_properties(XiangqiAnalyzer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "棋析"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.xiangqi.analyzer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "XiangqiAnalyzer.icns"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks"
)

if(APPLE)
    # Never produce a partially bundled application. Packaging must fail at
    # configure time if any analysis or recognition component is missing.
    foreach(required_file IN ITEMS
        "${DEFAULT_PIKAFISH_PATH}"
        "${PIKAFISH_DIR}/pikafish.nnue"
        "${PIKAFISH_DIR}/Copying.txt"
        "${PIKAFISH_DIR}/NNUE权重协议（使用视为同意本协议）_NNUE License.txt"
        "${ONNXRUNTIME_ROOT}/LICENSE"
        "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt"
        "${RECOGNITION_MODEL_DIR}/pose.onnx"
        "${RECOGNITION_MODEL_DIR}/layout_recognition.onnx"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/large.onnx"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/recognition/wooden-initial.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    )
        if(NOT EXISTS "${required_file}")
            message(FATAL_ERROR "Required bundled component is missing: ${required_file}")
        endif()
    endforeach()
    add_custom_command(TARGET XiangqiAnalyzer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/engine"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DEFAULT_PIKAFISH_PATH}" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/engine/Pikafish-MacOS-universal"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PIKAFISH_DIR}/pikafish.nnue" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/engine/pikafish.nnue"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PIKAFISH_DIR}/Copying.txt" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/engine/COPYING.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PIKAFISH_DIR}/NNUE权重协议（使用视为同意本协议）_NNUE License.txt" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/engine/NNUE-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/recognition"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RECOGNITION_MODEL_DIR}/pose.onnx" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/recognition/pose.onnx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RECOGNITION_MODEL_DIR}/layout_recognition.onnx" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/recognition/layout_recognition.onnx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/large.onnx" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/recognition/piece_detection.onnx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/LICENSE" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/recognition/atopx-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/resources/recognition/wooden-initial.png" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/recognition/wooden-initial.png"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Frameworks/libonnxruntime.1.23.2.dylib"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/onnxruntime"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_ROOT}/LICENSE" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/onnxruntime/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt" "$<TARGET_FILE_DIR:XiangqiAnalyzer>/../Resources/onnxruntime/ThirdPartyNotices.txt"
        # A linker-signed ad-hoc binary uses its changing cdhash as the designated
        # requirement. That invalidates macOS Screen Recording permission after
        # every rebuild. The user-approved local certificate keeps the requirement
        # stable without granting the application any additional privileges.
        COMMAND codesign --force --sign "XiangqiAnalyzer Local Development" --identifier com.xiangqi.analyzer --timestamp=none "$<TARGET_BUNDLE_DIR:XiangqiAnalyzer>"
        COMMENT "Bundling the Pikafish engine, NNUE weights, offline board recognizer, and stable app signature"
    )
endif()

qt_add_executable(autoplay_helper
    src/autoplay_helper.cpp
    src/boardrecognizer.h src/boardrecognizer.cpp src/piecedetector.h src/piecedetector.cpp
    src/pikafishengine.h src/pikafishengine.cpp
    src/position.h src/position.cpp
)
target_include_directories(autoplay_helper PRIVATE src)
target_compile_definitions(autoplay_helper PRIVATE
    DEFAULT_PIKAFISH_PATH="${DEFAULT_PIKAFISH_PATH}"
    RECOGNITION_MODEL_DIR="${RECOGNITION_MODEL_DIR}"
    PIECE_DETECTION_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/large.onnx"
    RECOGNITION_STYLE_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/resources/recognition/wooden-initial.png"
)
target_link_libraries(autoplay_helper PRIVATE Qt6::Widgets Qt6::Concurrent onnxruntime)

qt_add_executable(position_tests tests/position_test.cpp src/position.cpp src/position.h)
target_include_directories(position_tests PRIVATE src)
target_link_libraries(position_tests PRIVATE Qt6::Test)

qt_add_executable(autoplay_logic_test
    tests/autoplay_logic_test.cpp
    src/autoplaylogic.h src/autoplaylogic.cpp
    src/position.h src/position.cpp
)
target_include_directories(autoplay_logic_test PRIVATE src)
target_link_libraries(autoplay_logic_test PRIVATE Qt6::Test Qt6::Gui)

enable_testing()
add_test(NAME position_rules COMMAND position_tests)
add_test(NAME autoplay_logic COMMAND autoplay_logic_test)

if(APPLE)
    set(ENGINE_BUNDLE_PATH "${CMAKE_BINARY_DIR}/XiangqiAnalyzer.app/Contents/Resources/engine/Pikafish-MacOS-universal")
    qt_add_executable(engine_integration_test
        tests/engine_integration_test.cpp
        src/pikafishengine.h src/pikafishengine.cpp
        src/position.h src/position.cpp
    )
    target_include_directories(engine_integration_test PRIVATE src)
    target_compile_definitions(engine_integration_test PRIVATE
        DEFAULT_PIKAFISH_PATH="${DEFAULT_PIKAFISH_PATH}"
        ENGINE_TEST_PATH="${ENGINE_BUNDLE_PATH}"
    )
    target_link_libraries(engine_integration_test PRIVATE Qt6::Test Qt6::Widgets)
    add_dependencies(engine_integration_test XiangqiAnalyzer)
    add_test(NAME engine_integration COMMAND engine_integration_test)

    qt_add_executable(recognition_test
        tests/recognition_test.cpp
        src/boardrecognizer.h src/boardrecognizer.cpp src/piecedetector.h src/piecedetector.cpp
        src/boardwidget.h src/boardwidget.cpp
        src/position.h src/position.cpp
    )
    target_include_directories(recognition_test PRIVATE src)
    target_compile_definitions(recognition_test PRIVATE
        RECOGNITION_MODEL_DIR="${RECOGNITION_MODEL_DIR}"
    PIECE_DETECTION_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/large.onnx"
        RECOGNITION_STYLE_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/resources/recognition/wooden-initial.png"
        RECOGNITION_TEST_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/board-popup.png"
        RECOGNITION_EDGE_CANNON_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiantian-edge-cannon.png"
        RECOGNITION_MIDGAME_TYPES_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiantian-midgame-types.png"
        RECOGNITION_EMPTY_EDGE_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiantian-empty-edge.png"
        RECOGNITION_WOODEN_STYLE_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/wooden-initial.png"
        RECOGNITION_LATEST_FAILURE_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiantian-latest-failure.png"
        RECOGNITION_RED_TOP_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiantian-red-top-black-turn.png"
        RECOGNITION_JJ_MIDGAME_1_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/jj-midgame-1.png"
        RECOGNITION_JJ_MIDGAME_2_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/jj-midgame-2.png"
    )
    target_link_libraries(recognition_test PRIVATE Qt6::Test Qt6::Widgets Qt6::Concurrent onnxruntime)
    set_target_properties(recognition_test PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/XiangqiAnalyzer.app/Contents/Frameworks"
    )
    add_dependencies(recognition_test XiangqiAnalyzer)
    add_test(NAME board_recognition COMMAND recognition_test)
    set_tests_properties(board_recognition PROPERTIES TIMEOUT 75 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(ui_smoke_test
        tests/ui_smoke_test.cpp
        src/mainwindow.h src/mainwindow.cpp
        src/boardwidget.h src/boardwidget.cpp
        src/position.h src/position.cpp
        src/autoplaylogic.h src/autoplaylogic.cpp
        src/pikafishengine.h src/pikafishengine.cpp
        src/evaluationbar.h src/evaluationbar.cpp
        src/variationpreviewdialog.h src/variationpreviewdialog.cpp
        src/boardrecognizer.h src/boardrecognizer.cpp src/piecedetector.h src/piecedetector.cpp
        src/recognitiondialog.h src/recognitiondialog.cpp
        src/tiantiancontroller.h src/tiantiancontroller.cpp
        src/windowpickerdialog.h src/windowpickerdialog.cpp
        src/macautomation.h src/macautomation.mm
    )
    target_include_directories(ui_smoke_test PRIVATE src)
    target_compile_definitions(ui_smoke_test PRIVATE
        DEFAULT_PIKAFISH_PATH="${DEFAULT_PIKAFISH_PATH}"
        ENGINE_TEST_PATH="${ENGINE_BUNDLE_PATH}"
        RECOGNITION_MODEL_DIR="${RECOGNITION_MODEL_DIR}"
    PIECE_DETECTION_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/atopx/large.onnx"
        RECOGNITION_STYLE_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/resources/recognition/wooden-initial.png"
        UI_SCREENSHOT_PATH="${CMAKE_BINARY_DIR}/xiangqi-analyzer-ui.png"
    )
    target_link_libraries(ui_smoke_test PRIVATE Qt6::Test Qt6::Widgets Qt6::Concurrent onnxruntime
        "-framework CoreGraphics" "-framework ApplicationServices" "-framework ScreenCaptureKit" "-framework AppKit")
    set_target_properties(ui_smoke_test PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/XiangqiAnalyzer.app/Contents/Frameworks"
    )
    add_dependencies(ui_smoke_test XiangqiAnalyzer)
    add_test(NAME ui_smoke COMMAND ui_smoke_test)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)

    # A complete, redistributable local bundle: analysis engine, NNUE weights,
    # recognition models, ONNX Runtime, Qt frameworks/plugins and licenses.
    find_program(MACDEPLOYQT_EXECUTABLE NAMES macdeployqt
        HINTS "${Qt6_DIR}/../../../bin" /opt/homebrew/opt/qt/bin /opt/homebrew/bin /usr/local/bin
        REQUIRED)
    get_filename_component(QT_LIBRARY_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    set(XIANGQI_CODESIGN_IDENTITY "XiangqiAnalyzer Local Development" CACHE STRING
        "Identity used to sign the packaged macOS application")
    set(XIANGQI_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
    set(XIANGQI_PACKAGE_APP "${XIANGQI_PACKAGE_DIR}/XiangqiAnalyzer.app")
    set(XIANGQI_PACKAGE_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT XIANGQI_PACKAGE_ARCH)
        set(XIANGQI_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(XIANGQI_DMG "${CMAKE_BINARY_DIR}/XiangqiAnalyzer-${PROJECT_VERSION}-macOS-${XIANGQI_PACKAGE_ARCH}.dmg")
    add_custom_target(package
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${XIANGQI_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${XIANGQI_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:XiangqiAnalyzer>" "${XIANGQI_PACKAGE_APP}"
        COMMAND /usr/bin/find "${XIANGQI_PACKAGE_APP}" -name "._\\*" -delete
        COMMAND ${MACDEPLOYQT_EXECUTABLE} "${XIANGQI_PACKAGE_APP}" -always-overwrite -no-strip -verbose=1
            "-libpath=${QT_LIBRARY_DIR}"
        COMMAND codesign --force --sign "${XIANGQI_CODESIGN_IDENTITY}"
            --identifier com.xiangqi.analyzer.engine --timestamp=none
            "${XIANGQI_PACKAGE_APP}/Contents/Resources/engine/Pikafish-MacOS-universal"
        COMMAND codesign --force --deep --sign "${XIANGQI_CODESIGN_IDENTITY}"
            --identifier com.xiangqi.analyzer --timestamp=none "${XIANGQI_PACKAGE_APP}"
        COMMAND codesign --verify --deep --strict --verbose=2 "${XIANGQI_PACKAGE_APP}"
        COMMAND ${CMAKE_COMMAND} -E create_symlink /Applications "${XIANGQI_PACKAGE_DIR}/Applications"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${XIANGQI_DMG}"
        COMMAND /usr/bin/hdiutil create -volname "棋析" -srcfolder "${XIANGQI_PACKAGE_DIR}"
            -ov -format UDZO "${XIANGQI_DMG}"
        DEPENDS XiangqiAnalyzer position_tests autoplay_logic_test
            engine_integration_test recognition_test ui_smoke_test
        COMMENT "Running tests and creating a self-contained macOS DMG"
        USES_TERMINAL
    )
endif()
