// Native diagnostic executable shares the real application's installed resources.
TiledTest {
    name: "test_unityui"
    condition: project.nativeWidgetTests && qbs.targetOS.contains("macos")
    Depends { name: "libtilededitor" }
    Depends { name: "Qt.widgets" }
    bundle.isBundle: false
    install: true
    installDir: "Tiled.app/Contents/MacOS"
    cpp.rpaths: ["@loader_path/../Frameworks"]
    files: ["test_unityui.cpp"]
}
