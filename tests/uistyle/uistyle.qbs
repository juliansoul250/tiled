TiledTest {
    name: "test_uistyle"

    Depends { name: "Qt.widgets" }
    Depends { name: "libtilededitor" }

    cpp.includePaths: ["../../src/tiled"]

    files: [
        "test_uistyle.cpp",
        "../../src/tiled/tiledproxystyle.cpp",
        "../../src/tiled/tiledproxystyle.h",
        "../../src/tiled/iconcheckdelegate.cpp",
        "../../src/tiled/iconcheckdelegate.h",
        "../../src/tiled/utils.cpp",
    ]
}
