// Native diagnostic executable shares the real application's installed resources.
TiledTest {
    name: "test_unityui"
    condition: project.nativeWidgetTests
               && (qbs.targetOS.contains("macos") || qbs.targetOS.contains("windows"))
    Depends { name: "libtilededitor" }
    Depends { name: "Qt.widgets" }
    Properties {
        condition: qbs.targetOS.contains("macos")
        bundle.isBundle: false
    }
    install: true
    installDir: qbs.targetOS.contains("windows") ? "" : "Tiled.app/Contents/MacOS"
    // Direct installed execution does not receive qbs run's SDK environment.
    // QtTest is diagnostic-only and must resolve from the same selected Qt kit.
    cpp.rpaths: qbs.targetOS.contains("macos") ? ["@loader_path/../Frameworks", Qt.core.libPath] : []
    cpp.cxxFlags: qbs.toolchain.contains("msvc") ? base.concat(["/utf-8"]) : base
    files: ["test_unityui.cpp"]
}
