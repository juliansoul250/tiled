TiledTest {
    name: "test_unitytransfer"
    cpp.includePaths: ["../../src/tiled"]
    cpp.cxxFlags: qbs.toolchain.contains("msvc") ? base.concat(["/utf-8"]) : base
    cpp.defines: ["TILED_EDITOR_LIBRARY"]
    files: ["test_unitytransfer.cpp", "../../src/tiled/unitytransfer.cpp", "../../src/tiled/unitytransfer.h"]
}
