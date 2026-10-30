// Diagnostic build exposes internal editor symbols. This is not a byte-identical
// packaged production artifact; normal builds leave this product disabled.
TiledTest {
    name: "test_layerwidgets"
    condition: project.nativeWidgetTests && qbs.targetOS.contains("macos")
    Depends { name: "libtilededitor" }
    Depends { name: "Qt.widgets" }
    cpp.frameworks: ["AppKit"]
    files: ["test_layerwidgets.cpp", "nativeaccessibility.h", "nativeaccessibility.mm"]
}
