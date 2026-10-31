/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "unitytransfer.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>
using namespace Tiled::UnityTransfer;
namespace {
bool put(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString &path) { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }
struct Fixture {
    QTemporaryDir temporary;
    QString project = QFileInfo(temporary.path()).canonicalFilePath() + QStringLiteral("/中文 Unity 项目");
    QString source = QFileInfo(temporary.path()).canonicalFilePath() + QStringLiteral("/源 地图");
    QString package = QFileInfo(temporary.path()).canonicalFilePath() + QStringLiteral("/bundled-package");
    QString map = source + QStringLiteral("/地图.tmx");
    Fixture() {
        QDir().mkpath(project + QStringLiteral("/Assets"));
        put(project + QStringLiteral("/Packages/manifest.json"), "{\"dependencies\":{}}");
        put(project + QStringLiteral("/ProjectSettings/ProjectVersion.txt"), "m_EditorVersion: 2022.3.62f1\n");
        put(package + QStringLiteral("/package.json"), "{\"name\":\"com.seanba.super-tiled2unity\",\"version\":\"2.4.0\"}");
        put(package + QStringLiteral("/LICENSE"), "fixture license");
        QJsonObject files;
        for (const QString &name : {QStringLiteral("package.json"), QStringLiteral("LICENSE")})
            files.insert(name, QString::fromLatin1(QCryptographicHash::hash(get(package + QLatin1Char('/') + name), QCryptographicHash::Sha256).toHex()));
        QJsonObject provenance; provenance.insert(QStringLiteral("commit"), QStringLiteral("d3deae415963807caef14c522ed8be9bc30e89e0")); provenance.insert(QStringLiteral("version"), QStringLiteral("2.4.0")); provenance.insert(QStringLiteral("files"), files);
        put(QFileInfo(package).absolutePath() + QStringLiteral("/SOURCE.json"), QJsonDocument(provenance).toJson());

        put(source + QStringLiteral("/images/tile.png"), "PNG fixture");
        put(source + QStringLiteral("/tiles/terrain.tsx"), "<?xml version=\"1.0\"?><tileset name=\"test\" tilewidth=\"16\" tileheight=\"16\"><image source=\"../images/tile.png\" width=\"16\" height=\"16\"/></tileset>");
        put(source + QStringLiteral("/templates/object.tx"), "<template><tileset firstgid=\"1\" source=\"../tiles/terrain.tsx\"/><object gid=\"1\"/></template>");
        put(map, "<map orientation=\"orthogonal\" width=\"1\" height=\"1\" tilewidth=\"16\" tileheight=\"16\"><tileset firstgid=\"1\" source=\"tiles/terrain.tsx\"/><objectgroup><object id=\"1\" template=\"templates/object.tx\"/></objectgroup></map>");
    }
    Plan preview() { return plan(map, project, package); }
};
}
class TestUnityTransfer : public QObject {
    Q_OBJECT
private slots:
    void recursiveDependenciesAndUnicode();
    void previewCancellationWritesNothing();
    void repeatsPreserveMetaAndUpdateDependencies();
    void refusesUserChangesAndUnknownFiles();
    void refusesChangedPreview();
    void rollbackAfterPackage();
    void rollbackAfterAssets();
    void rejectsUnsafeInputs_data();
    void rejectsUnsafeInputs();
    void rejectsPackageConflict();
    void rejectsExecutableFileProperty();
    void rejectsLegacyImporter();
    void rejectsSymlinks();
    void dependenciesWithSameNameAndParentPath();
    void projectChangesAfterPreview();
    void targetSymlinkAfterPreview();
    void removedDependenciesKeepOtherMetaSafe();
    void rejectsDeepDependencyChain();
};
void TestUnityTransfer::recursiveDependenciesAndUnicode() {
    Fixture f; const auto p = f.preview(); QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
    QCOMPARE(p.assets.size(), 5); // map, TSX, TX, image and ownership record
    QVERIFY(p.installPackage); QVERIFY(!p.preview.isEmpty());
    const QByteArray originalManifest = get(f.project + QStringLiteral("/Packages/manifest.json"));
    QVERIFY2(apply(p).isEmpty(), "transaction failed");
    QCOMPARE(get(f.project + QStringLiteral("/Packages/manifest.json")), originalManifest);
    QVERIFY(QFileInfo::exists(p.destinationMapPath));
    for (auto it = p.assets.cbegin(); it != p.assets.cend(); ++it) {
        if (!it.key().endsWith(QStringLiteral("tmx")) && !it.key().endsWith(QStringLiteral("tsx")) && !it.key().endsWith(QStringLiteral("tx"))) continue;
        QXmlStreamReader xml(it.value());
        while (!xml.atEnd()) {
            xml.readNext(); if (!xml.isStartElement()) continue;
            for (const auto &attribute : xml.attributes()) if (attribute.name() == QStringLiteral("source") || attribute.name() == QStringLiteral("template"))
                QVERIFY(QFileInfo::exists(QDir(QFileInfo(QDir(p.destinationPath).filePath(it.key())).absolutePath()).filePath(attribute.value().toString())));
        }
        QVERIFY(!xml.hasError());
    }
}
void TestUnityTransfer::previewCancellationWritesNothing() {
    Fixture f; auto p = f.preview(); QVERIFY(p.error.isEmpty());
    QVERIFY(!QFileInfo::exists(p.packagePath)); QVERIFY(!QFileInfo::exists(p.destinationPath));
    QVERIFY(!QFileInfo::exists(f.project + QStringLiteral("/.tiled-unity-import.lock")));
}
void TestUnityTransfer::repeatsPreserveMetaAndUpdateDependencies() {
    Fixture f; auto p = f.preview(); QVERIFY(p.error.isEmpty()); QVERIFY(apply(p).isEmpty());
    const QByteArray meta("fileFormatVersion: 2\nguid: 0123456789abcdef\n");
    QVERIFY(put(p.destinationMapPath + QStringLiteral(".meta"), meta));
    QVERIFY(put(p.destinationPath + QStringLiteral("/.tiled-import.json.meta"), meta));
    QVERIFY(put(f.source + QStringLiteral("/images/tile.png"), "new pixels"));
    auto update = f.preview(); QVERIFY2(update.error.isEmpty(), qPrintable(update.error)); QVERIFY(!update.installPackage);
    QVERIFY(apply(update).isEmpty()); QCOMPARE(get(p.destinationMapPath + QStringLiteral(".meta")), meta);
    auto repeated = f.preview(); QVERIFY2(repeated.error.isEmpty(), qPrintable(repeated.error)); QVERIFY(apply(repeated).isEmpty());
}
void TestUnityTransfer::refusesUserChangesAndUnknownFiles() {
    Fixture f; auto p = f.preview(); QVERIFY(apply(p).isEmpty());
    auto original = get(p.destinationMapPath); QVERIFY(put(p.destinationMapPath, "user edit")); QVERIFY(!f.preview().error.isEmpty());
    QVERIFY(put(p.destinationMapPath, original)); QVERIFY(put(p.destinationPath + QStringLiteral("/notes.txt"), "user notes")); QVERIFY(!f.preview().error.isEmpty());
    QVERIFY(QFile::remove(p.destinationPath + QStringLiteral("/notes.txt"))); QVERIFY(put(p.destinationPath + QStringLiteral("/unknown.meta"), "unknown")); QVERIFY(!f.preview().error.isEmpty());
}
void TestUnityTransfer::refusesChangedPreview() {
    Fixture f; auto p = f.preview(); QVERIFY(p.error.isEmpty());
    QVERIFY(put(f.source + QStringLiteral("/images/tile.png"), "changed after preview"));
    QVERIFY(!apply(p).isEmpty()); QVERIFY(!QFileInfo::exists(p.packagePath)); QVERIFY(!QFileInfo::exists(p.destinationPath));
}
void TestUnityTransfer::rollbackAfterPackage() {
    Fixture f; const auto p = f.preview(); QVERIFY(p.error.isEmpty());
    QVERIFY(!apply(p, true).isEmpty()); QVERIFY(!QFileInfo::exists(p.packagePath)); QVERIFY(!QFileInfo::exists(p.destinationPath));
    QVERIFY(!QFileInfo::exists(f.project + QStringLiteral("/Assets/TiledImports")));
    QVERIFY(apply(f.preview()).isEmpty());
}
void TestUnityTransfer::rollbackAfterAssets() {
    Fixture f; auto initial = f.preview(); QVERIFY(apply(initial).isEmpty());
    const QByteArray oldMap = get(initial.destinationMapPath);
    QVERIFY(put(f.map, "<map orientation=\"orthogonal\" width=\"2\"/>"));
    auto update = f.preview(); QVERIFY(update.error.isEmpty());
    QVERIFY(!apply(update, 2).isEmpty()); QCOMPARE(get(initial.destinationMapPath), oldMap);
    QVERIFY(QFileInfo::exists(initial.packagePath));
    QVERIFY(apply(f.preview()).isEmpty());
}
void TestUnityTransfer::rejectsUnsafeInputs_data() {
    QTest::addColumn<QString>("relative"); QTest::addColumn<QByteArray>("contents");
    QTest::newRow("invalid-manifest") << QStringLiteral("project/Packages/manifest.json") << QByteArray("broken");
    QTest::newRow("unity-open") << QStringLiteral("project/Temp/UnityLockfile") << QByteArray("locked");
    QTest::newRow("wrong-version") << QStringLiteral("project/ProjectSettings/ProjectVersion.txt") << QByteArray("m_EditorVersion: 6000.0.1f1");
    QTest::newRow("invalid-xml") << QStringLiteral("map") << QByteArray("<map");
    QTest::newRow("dtd") << QStringLiteral("map") << QByteArray("<!DOCTYPE map [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><map orientation=\"orthogonal\">&x;</map>");
    QTest::newRow("oblique") << QStringLiteral("map") << QByteArray("<map orientation=\"oblique\"/>");
    QTest::newRow("zstd") << QStringLiteral("map") << QByteArray("<map orientation=\"orthogonal\"><layer><data compression=\"zstd\"/></layer></map>");
    QTest::newRow("url") << QStringLiteral("map") << QByteArray("<map orientation=\"orthogonal\"><image source=\"https://host/image.png\"/></map>");
    QTest::newRow("missing-image") << QStringLiteral("map") << QByteArray("<map orientation=\"orthogonal\"><image source=\"missing.png\"/></map>");
    QTest::newRow("json-tileset") << QStringLiteral("map") << QByteArray("<map orientation=\"orthogonal\"><tileset source=\"tiles.tsj\"/></map>");
}
void TestUnityTransfer::rejectsUnsafeInputs() {
    QFETCH(QString, relative); QFETCH(QByteArray, contents); Fixture f;
    QString path = relative == QStringLiteral("map") ? f.map : f.project + relative.mid(7);
    QVERIFY(put(path, contents)); auto p = f.preview(); QVERIFY2(!p.error.isEmpty(), qPrintable(relative));
    QVERIFY(!QFileInfo::exists(f.project + QStringLiteral("/Assets/TiledImports")));
}
void TestUnityTransfer::rejectsPackageConflict() {
    Fixture f; QVERIFY(put(f.project + QStringLiteral("/Packages/manifest.json"), "{\"dependencies\":{\"com.seanba.super-tiled2unity\":\"2.4.0\"}}")); QVERIFY(!f.preview().error.isEmpty());
    QVERIFY(put(f.project + QStringLiteral("/Packages/manifest.json"), "{\"dependencies\":{}}"));
    QVERIFY(put(f.project + QStringLiteral("/Packages/com.seanba.super-tiled2unity/package.json"), "user package")); QVERIFY(!f.preview().error.isEmpty());
}
void TestUnityTransfer::rejectsExecutableFileProperty() {
    Fixture f; QVERIFY(put(f.source + QStringLiteral("/evil.cs"), "class AutoRun {}"));
    QVERIFY(put(f.map, "<map orientation=\"orthogonal\"><properties><property name=\"script\" type=\"file\" value=\"evil.cs\"/></properties></map>"));
    QVERIFY(!f.preview().error.isEmpty());
}
void TestUnityTransfer::rejectsLegacyImporter() {
    Fixture f; QVERIFY(put(f.project + QStringLiteral("/Assets/Plugins/SuperTiled2Unity/SuperMap.cs"), "old importer"));
    QVERIFY(!f.preview().error.isEmpty());
}
void TestUnityTransfer::dependenciesWithSameNameAndParentPath() {
    Fixture f;
    QVERIFY(put(f.source + QStringLiteral("/second/tile.png"), "second"));
    QVERIFY(put(QFileInfo(f.source).absolutePath() + QStringLiteral("/outside.png"), "outside"));
    QVERIFY(put(f.map, "<map orientation=\"orthogonal\"><imagelayer><image source=\"images/tile.png\"/></imagelayer><imagelayer><image source=\"second/tile.png\"/></imagelayer><properties><property name=\"reference\" type=\"file\" value=\"../outside.png\"/></properties></map>"));
    const auto p = f.preview(); QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
    QCOMPARE(p.assets.size(), 5); QVERIFY(apply(p).isEmpty());
    int sameNames = 0; for (auto it = p.assets.cbegin(); it != p.assets.cend(); ++it) if (it.key().endsWith(QStringLiteral("/tile.png"))) ++sameNames;
    QCOMPARE(sameNames, 2);
}
void TestUnityTransfer::projectChangesAfterPreview() {
    Fixture f; auto p = f.preview();
    const QString manifest = f.project + QStringLiteral("/Packages/manifest.json");
    QVERIFY(put(manifest, "{\"dependencies\":{},\"changed\":true}")); QVERIFY(!apply(p).isEmpty());
    QVERIFY(!QFileInfo::exists(p.packagePath));
    p = f.preview(); QVERIFY(p.error.isEmpty());
    QVERIFY(put(f.project + QStringLiteral("/Temp/UnityLockfile"), "open")); QVERIFY(!apply(p).isEmpty());
    QVERIFY(QFile::remove(f.project + QStringLiteral("/Temp/UnityLockfile")));
    QVERIFY(apply(f.preview()).isEmpty()); p = f.preview();
    QVERIFY(put(p.packagePath + QStringLiteral("/LICENSE"), "modified installed package")); QVERIFY(!apply(p).isEmpty());
}
void TestUnityTransfer::targetSymlinkAfterPreview() {
#ifdef Q_OS_WIN
    QSKIP("Windows symlink privilege is not guaranteed; Unix CI exercises write-boundary symlinks");
#else
    Fixture f; auto p = f.preview(); QVERIFY(p.error.isEmpty());
    const QString outside = QFileInfo(f.temporary.path()).canonicalFilePath() + QStringLiteral("/outside-assets"); QDir().mkpath(outside);
    QVERIFY(QFile::link(outside, f.project + QStringLiteral("/Assets/TiledImports")));
    QVERIFY(!apply(p).isEmpty()); QVERIFY(QDir(outside).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(!f.preview().error.isEmpty());
#endif
}
void TestUnityTransfer::removedDependenciesKeepOtherMetaSafe() {
    Fixture f; auto p = f.preview(); QVERIFY(apply(p).isEmpty());
    QString dependency; for (auto it = p.assets.cbegin(); it != p.assets.cend(); ++it) if (it.key().endsWith(QStringLiteral(".png"))) dependency = it.key();
    QVERIFY(put(QDir(p.destinationPath).filePath(dependency + QStringLiteral(".meta")), "known meta"));
    QVERIFY(put(f.map, "<map orientation=\"orthogonal\"/>"));
    auto next = f.preview(); QVERIFY2(next.error.isEmpty(), qPrintable(next.error)); QVERIFY(apply(next).isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(p.destinationPath).filePath(dependency)));
    QVERIFY(!QFileInfo::exists(QDir(p.destinationPath).filePath(dependency + QStringLiteral(".meta"))));
    QVERIFY(put(p.destinationPath + QStringLiteral("/stranger.meta"), "unowned")); QVERIFY(!f.preview().error.isEmpty());
}
void TestUnityTransfer::rejectsDeepDependencyChain() {
    Fixture f;
    for (int i = 0; i < 70; ++i) {
        const QString name = i == 0 ? f.map : f.source + QStringLiteral("/chain%1.tmx").arg(i);
        const QByteArray bytes = QStringLiteral("<map orientation=\"orthogonal\"><properties><property name=\"nested\" type=\"file\" value=\"chain%1.tmx\"/></properties></map>").arg(i + 1).toUtf8();
        QVERIFY(put(name, bytes));
    }
    QVERIFY(!f.preview().error.isEmpty());
}
void TestUnityTransfer::rejectsSymlinks() {
#ifdef Q_OS_WIN
    QSKIP("Windows ordinary account symlink privilege is not guaranteed; Unix CI exercises this gate");
#else
    Fixture f; const QString outside = QFileInfo(f.temporary.path()).canonicalFilePath() + QStringLiteral("/outside.png"); QVERIFY(put(outside, "outside"));
    const QString image = f.source + QStringLiteral("/images/tile.png"); QVERIFY(QFile::remove(image)); QVERIFY(QFile::link(outside, image));
    QVERIFY(!f.preview().error.isEmpty());
#endif
}
QTEST_GUILESS_MAIN(TestUnityTransfer)
#include "test_unitytransfer.moc"
