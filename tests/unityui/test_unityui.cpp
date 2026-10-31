/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the actual MainWindow action and modal workflow, not a backend proxy.
 */
#include "document.h"
#include "documentmanager.h"
#include "mainwindow.h"
#include "pluginmanager.h"
#include "preferences.h"
#include "properties.h"
#include "session.h"
#include "stylehelper.h"
#include "tiledapplication.h"
#include "tmxmapformat.h"

#include <QAction>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QXmlStreamReader>
#include <QtTest>

using namespace Tiled;
namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> result;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = QDir(root).relativeFilePath(path);
        result.insert(relative + (it.fileInfo().isDir() ? QStringLiteral("/") : QString()),
                      it.fileInfo().isDir() ? QByteArray() : readFile(path));
    }
    return result;
}
}

class TestUnityUi : public QObject
{
    Q_OBJECT
public:
    TestUnityUi(MainWindow &window, QString root)
        : mWindow(window), mRoot(std::move(root)) {}
private:
    enum Flow { CancelChooser, CancelPreview, ConfirmImport, DirtyWarning };
    QString drive(QAction *action, Flow flow, const QString &project, const QString &captureStem);
    bool capture(QWidget &widget, const QString &name);
    MainWindow &mWindow;
    QString mRoot;
private slots:
    void nativeActionWorkflow();
};

bool TestUnityUi::capture(QWidget &widget, const QString &name)
{
    const QString directory = qEnvironmentVariable("TILED_WIDGET_ARTIFACTS");
    return !directory.isEmpty() && QDir().mkpath(directory)
            && widget.grab().save(QDir(directory).filePath(name + QStringLiteral(".png")));
}

QString TestUnityUi::drive(QAction *action, Flow flow, const QString &project,
                         const QString &captureStem)
{
    QString error;
    int stage = 0;
    bool complete = false;
    QPointer<QFileDialog> activeChooser;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll; // Stack ownership stops all callbacks on every return path.
    poll.setInterval(25);
    connect(&poll, &QTimer::timeout, this, [&] {
        QWidget *modal = QApplication::activeModalWidget();
        if (elapsed.elapsed() > 20000) {
            error = QStringLiteral("The actual Unity action exceeded its 20 second modal deadline");
            if (auto dialog = qobject_cast<QDialog *>(modal))
                dialog->reject();
            return;
        }
        if (!modal)
            return;
        if (auto chooser = qobject_cast<QFileDialog *>(modal)) {
            if (flow == DirtyWarning || stage > 1 || (stage == 1 && activeChooser != chooser)) {
                error = QStringLiteral("Unexpected or repeated file chooser");
                chooser->reject();
                return;
            }
            if (stage == 1) // Await the queued accept after the directory model settles.
                return;
            stage = 1;
            activeChooser = chooser;
            if (flow == CancelChooser) {
                complete = true;
                chooser->reject();
            } else {
                chooser->setDirectory(QFileInfo(project).absolutePath());
                chooser->selectFile(project);
                const QPointer<QFileDialog> guarded = chooser;
                QTimer::singleShot(150, chooser, [guarded, project] {
                    if (!guarded)
                        return;
                    const auto selected = guarded->selectedFiles();
                    if (selected.size() != 1 || QFileInfo(selected.first()).canonicalFilePath() != QFileInfo(project).canonicalFilePath()) {
                        qWarning() << "File chooser did not select the actual Unity project:" << selected;
                        guarded->reject();
                        return;
                    }
                    QMetaObject::invokeMethod(guarded, "accept", Qt::QueuedConnection);
                });
            }
            return;
        }
        auto message = qobject_cast<QMessageBox *>(modal);
        if (!message) {
            error = QStringLiteral("Unexpected non-message modal dialog");
            if (auto dialog = qobject_cast<QDialog *>(modal))
                dialog->reject();
            return;
        }
        if (flow == DirtyWarning && stage == 0 && message->icon() == QMessageBox::Warning) {
            if (!message->text().contains(QStringLiteral("保存")))
                error = QStringLiteral("Dirty-map warning is not the expected Chinese save warning");
            stage = 1;
            complete = true;
            if (!capture(*message, captureStem + QStringLiteral("-warning")))
                error = QStringLiteral("Failed to capture the dirty-map warning");
            message->button(QMessageBox::Ok)->click();
            return;
        }
        if (stage == 1 && message->icon() == QMessageBox::Question) {
            if (message->defaultButton() != message->button(QMessageBox::Cancel)
                    || message->detailedText().isEmpty()
                    || !message->detailedText().contains(QStringLiteral("Assets/TiledImports"))) {
                error = QStringLiteral("Preview is missing its safe default or actual managed-file list");
                message->reject();
                return;
            }
            if (!capture(*message, captureStem + QStringLiteral("-preview")))
                error = QStringLiteral("Failed to capture the real preview");
            stage = 2;
            if (flow == CancelPreview) {
                complete = true;
                message->button(QMessageBox::Cancel)->click();
            } else {
                message->button(QMessageBox::Ok)->click();
            }
            return;
        }
        if (stage == 2 && flow == ConfirmImport && message->icon() == QMessageBox::Information) {
            if (!message->text().contains(QStringLiteral("Unity 2022.3")))
                error = QStringLiteral("Unexpected success message");
            if (!capture(*message, captureStem + QStringLiteral("-success")))
                error = QStringLiteral("Failed to capture the actual completion message");
            stage = 3;
            complete = true;
            message->button(QMessageBox::Ok)->click();
            return;
        }
        error = QStringLiteral("Unexpected message during actual Unity action: ") + message->text();
        message->reject();
    });
    poll.start();
    action->trigger(); // Enters the production QFileDialog/QMessageBox nested loops.
    poll.stop();
    if (!complete && error.isEmpty())
        error = QStringLiteral("The action returned without completing the expected modal sequence");
    qInfo() << "Unity UI flow" << int(flow) << "stage" << stage << "completed" << complete
            << "elapsed" << elapsed.elapsed() << "error" << error;
    return error;
}

void TestUnityUi::nativeActionWorkflow()
{
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("cocoa"));
    QVERIFY(QTest::qWaitForWindowExposed(&mWindow));
    mWindow.raise();
    mWindow.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&mWindow));
    QVERIFY(QDir::cleanPath(QCoreApplication::applicationDirPath()).endsWith(QStringLiteral("Tiled.app/Contents/MacOS")));
    const QString project = mRoot + QStringLiteral("/中文 Unity 项目");
    const QString source = mRoot + QStringLiteral("/地图 源文件");
    const QString mapPath = source + QStringLiteral("/测试地图.tmx");
    QVERIFY(QDir().mkpath(project + QStringLiteral("/Assets")));
    QVERIFY(writeFile(project + QStringLiteral("/Packages/manifest.json"), "{\"dependencies\":{}}"));
    QVERIFY(writeFile(project + QStringLiteral("/ProjectSettings/ProjectVersion.txt"), "m_EditorVersion: 2022.3.62f1\n"));
    QVERIFY(QDir().mkpath(source + QStringLiteral("/images")));
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(source + QStringLiteral("/images/tile.png")));
    QVERIFY(writeFile(source + QStringLiteral("/tiles.tsx"), "<?xml version=\"1.0\"?><tileset version=\"1.10\" name=\"UI Test\" tilewidth=\"16\" tileheight=\"16\" tilecount=\"1\" columns=\"1\"><image source=\"images/tile.png\" width=\"16\" height=\"16\"/></tileset>"));
    QVERIFY(writeFile(mapPath, "<?xml version=\"1.0\"?><map version=\"1.10\" orientation=\"orthogonal\" renderorder=\"right-down\" width=\"1\" height=\"1\" tilewidth=\"16\" tileheight=\"16\" infinite=\"0\"><tileset firstgid=\"1\" source=\"tiles.tsx\"/><layer id=\"1\" name=\"Ground\" width=\"1\" height=\"1\"><data encoding=\"csv\">1</data></layer></map>"));
    QVERIFY(mWindow.openFile(mapPath));
    QCoreApplication::processEvents();
    auto action = mWindow.findChild<QAction *>(QStringLiteral("actionImportToUnity"));
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QVERIFY(action->text().contains(QStringLiteral("导入")));
    const auto unchanged = snapshot(project);
    QString error = drive(action, CancelChooser, project, QStringLiteral("unity-chooser-cancel"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(snapshot(project), unchanged);
    error = drive(action, CancelPreview, project, QStringLiteral("unity-preview-cancel"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(snapshot(project), unchanged);

    auto document = DocumentManager::instance()->currentDocument();
    QVERIFY(document);
    document->undoStack()->push(new QUndoCommand(QStringLiteral("Test-only dirty marker")));
    QVERIFY(document->isModified());
    error = drive(action, DirtyWarning, project, QStringLiteral("unity-dirty"));
    document->undoStack()->undo(); // Restore clean state before any assertion exit.
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!document->isModified());
    QCOMPARE(snapshot(project), unchanged);

    error = drive(action, ConfirmImport, project, QStringLiteral("unity-confirm"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString packageRoot = project + QStringLiteral("/Packages/com.seanba.super-tiled2unity");
    const QString bundledRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/unity");
    const auto provenance = QJsonDocument::fromJson(readFile(bundledRoot + QStringLiteral("/SOURCE.json"))).object();
    const auto hashes = provenance.value(QStringLiteral("files")).toObject();
    QCOMPARE(hashes.size(), 582);
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        const QString path = packageRoot + QLatin1Char('/') + it.key();
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex()), it.value().toString());
    }
    const QString identity = QString::fromLatin1(QCryptographicHash::hash(QFileInfo(mapPath).canonicalFilePath().toUtf8(), QCryptographicHash::Sha256).toHex()).left(24);
    const QString importRoot = project + QStringLiteral("/Assets/TiledImports/") + identity;
    const QString installedMap = importRoot + QStringLiteral("/map/测试地图.tmx");
    QVERIFY(QFileInfo::exists(installedMap));
    QVERIFY(QFileInfo::exists(importRoot + QStringLiteral("/.tiled-import.json")));
    const auto ownership = QJsonDocument::fromJson(readFile(importRoot + QStringLiteral("/.tiled-import.json"))).object();
    QCOMPARE(ownership.value(QStringLiteral("source")).toString(), QFileInfo(mapPath).canonicalFilePath());
    const auto assetHashes = ownership.value(QStringLiteral("files")).toObject();
    QCOMPARE(assetHashes.size(), 3); // Actual map, external TSX and PNG.
    for (auto it = assetHashes.begin(); it != assetHashes.end(); ++it) {
        const QString path = importRoot + QLatin1Char('/') + it.key();
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        const auto bytes = readFile(path);
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()), it.value().toString());
        if (path.endsWith(QStringLiteral(".png")))
            continue;
        QXmlStreamReader xml(bytes);
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() || !xml.attributes().hasAttribute(QStringLiteral("source")))
                continue;
            const QString reference = xml.attributes().value(QStringLiteral("source")).toString();
            const QString resolved = QDir::cleanPath(QDir(QFileInfo(path).absolutePath()).absoluteFilePath(reference));
            QVERIFY(!QDir::isAbsolutePath(reference));
            QVERIFY2(resolved.startsWith(importRoot + QLatin1Char('/')) && QFileInfo::exists(resolved), qPrintable(resolved));
        }
        QVERIFY(!xml.hasError());
    }

    const auto imported = snapshot(project);
    error = drive(action, ConfirmImport, project, QStringLiteral("unity-repeat"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(snapshot(project), imported);
    QVERIFY(capture(mWindow, QStringLiteral("unity-main-window")));
    qInfo() << "PASS actual MainWindow Unity action: chooser cancel, preview cancel, dirty rejection,"
               " official 582-file installation and repeated transfer; Unity Editor execution was not tested.";
}

int main(int argc, char **argv)
{
    QTemporaryDir isolation;
    if (!isolation.isValid())
        return 2;
    const QString root = QFileInfo(isolation.path()).canonicalFilePath();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); // Test fixture only.
    QCoreApplication::setOrganizationName(QStringLiteral("TiledUnityUiTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
    QStandardPaths::setTestModeEnabled(true);
    TiledApplication application(argc, argv);
    if (!Preferences::instance()->fileName().startsWith(root + QLatin1Char('/')))
        return 3;
    initializeMetatypes();
    TmxMapFormat mapFormat;
    TsxTilesetFormat tilesetFormat;
    XmlObjectTemplateFormat templateFormat;
    PluginManager::addObject(&mapFormat);
    PluginManager::addObject(&tilesetFormat);
    PluginManager::addObject(&templateFormat);
    Preferences::setStartupSession(root + QStringLiteral("/ui-test.tiled-session"));
    Preferences::instance()->setRestoreSessionOnStartup(false);
    Session::initialize();
    StyleHelper::initialize();
    int result;
    {
        MainWindow window;
        window.resize(1100, 760);
        window.show();
        application.setActivationWindow(&window);
        PluginManager::instance()->loadPlugins();
        window.initializeSession();
        TestUnityUi test(window, root);
        result = QTest::qExec(&test, argc, argv);
        application.setActivationWindow(nullptr, false);
    }
    PluginManager::removeObject(&templateFormat);
    PluginManager::removeObject(&tilesetFormat);
    PluginManager::removeObject(&mapFormat);
    return result;
}
#include "test_unityui.moc"
