// Native-window coverage: deliberately complements, not replaces, model tests.
#include "grouplayer.h"
#include "layerdock.h"
#include "layermodel.h"
#include "map.h"
#include "mapdocument.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QUndoStack>
#include <QtTest>

using namespace Tiled;

class test_LayerWidgets : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void renameAndSwitchDocument();
    void deleteNestedGroupsWithLiveView();
};

static std::unique_ptr<Map> makeMap()
{
    Map::Parameters p;
    p.orientation = Map::Orthogonal;
    p.width = p.height = 16;
    p.tileWidth = p.tileHeight = 32;
    return std::make_unique<Map>(p);
}

static void capture(QWidget &window, const QString &name)
{
    const QString directory = qEnvironmentVariable("TILED_WIDGET_ARTIFACTS");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        window.grab().save(QDir(directory).filePath(name + QStringLiteral(".png")));
    }
}

void test_LayerWidgets::initTestCase()
{
    qInfo() << "Native platform:" << QGuiApplication::platformName()
            << "Qt:" << qVersion();
    if (qEnvironmentVariableIsSet("TILED_REQUIRE_COCOA"))
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("cocoa"));
}

void test_LayerWidgets::renameAndSwitchDocument()
{
    // Documents outlive the window; detach before destroying the current one.
    auto first = std::make_unique<MapDocument>(makeMap());
    MapDocument second(makeMap());
    QMainWindow window;
    auto dock = new QDockWidget(QStringLiteral("Layers"), &window);
    auto view = new LayerView(dock);
    dock->setWidget(view);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    window.resize(640, 480);
    view->setMapDocument(first.get());
    window.show();
    QVERIFY2(QTest::qWaitForWindowExposed(&window), "Real window was not exposed; no offscreen fallback allowed");
    window.activateWindow();
    QVERIFY2(QTest::qWaitForWindowActive(&window), "Native window did not become active");

    for (int cycle = 0; cycle < 10; ++cycle) {
        auto layer = first->addLayer(Layer::TileLayerType);
        view->editLayerModelIndex(first->layerModel()->index(layer));
        QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
        QPointer<QLineEdit> editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        const QString name = QStringLiteral("Native renamed %1").arg(cycle);
        editor->selectAll();
        QTest::keyClicks(editor, name);
        capture(window, QStringLiteral("rename-active"));
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_COMPARE(layer->name(), name);

        view->editLayerModelIndex(first->layerModel()->index(layer));
        QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
        editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        editor->selectAll();
        QTest::keyClicks(editor, QStringLiteral("cancelled"));
        QTest::keyClick(editor, Qt::Key_Escape);
        QCOMPARE(layer->name(), name);

        // Switch with an active inline editor, exercising commit/close callbacks.
        view->editLayerModelIndex(first->layerModel()->index(layer));
        QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
        view->setMapDocument(&second);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QCOMPARE(view->model()->rowCount(), 0);
        view->setMapDocument(first.get());
        QCOMPARE(view->model()->rowCount(), cycle + 1);
    }
    view->editLayerModelIndex(first->layerModel()->index(first->currentLayer()));
    QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
    view->setMapDocument(nullptr);
    first.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCOMPARE(view->model()->rowCount(), 0);
    capture(window, QStringLiteral("document-detached"));
}

void test_LayerWidgets::deleteNestedGroupsWithLiveView()
{
    MapDocument document(makeMap());
    LayerView view;
    view.resize(480, 400);
    view.setMapDocument(&document);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    for (int cycle = 0; cycle < 20; ++cycle) {
        auto layer = document.addLayer(Layer::ObjectGroupType);
        document.groupLayers({layer});
        document.groupLayers({layer->parentLayer()});
        view.expandAll();
        QCoreApplication::processEvents();
        QCOMPARE(view.model()->rowCount(), 1);
        document.switchSelectedLayers(document.map()->layers());
        capture(view, QStringLiteral("nested-before-delete"));
        QTest::keyClick(&view, Qt::Key_Delete);
        QCOMPARE(document.map()->layerCount(), 0);
        document.undoStack()->undo();
        QCOMPARE(view.model()->rowCount(), 1);
        view.expandAll();
        document.undoStack()->redo();
        document.undoStack()->clear();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QCOMPARE(view.model()->rowCount(), 0);
    }
    view.setMapDocument(nullptr);
}

QTEST_MAIN(test_LayerWidgets)
#include "test_layerwidgets.moc"
