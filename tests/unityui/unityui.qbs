// Native diagnostic executable shares the real application's installed resources.
TiledTest {
    name: "test_unityui"
    condition: project.nativeWidgetTests && qbs.targetOS.contains("macos")
    Depends { name: "libtilededitor" }
    Depends { name: "Qt.widgets" }
    bundle.isBundle: false
    install: true
    installDir: "Tiled.app/Contents/MacOS"
    // Direct installed execution does not receive qbs run's SDK environment.
    // QtTest is diagnostic-only and must resolve from the same selected Qt kit.
    cpp.rpaths: ["@loader_path/../Frameworks", Qt.core.libPath]
    files: ["test_unityui.cpp"]
}
