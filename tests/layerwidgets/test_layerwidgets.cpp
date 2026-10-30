// Native-window coverage: deliberately complements, not replaces, model tests.
#include "actionmanager.h"
#include "mapdocumentactionhandler.h"
#include "grouplayer.h"
#include "layerdock.h"
#include "layermodel.h"
#include "macosglass.h"
#include "preferences.h"
#include "stylehelper.h"
#include "map.h"
#include "mapdocument.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QLineEdit>
#include <QImage>
#include <QStyle>
#include <QMainWindow>
#include <QScreen>
#include <QStandardItemModel>
#include <QTreeView>
#include <QMenu>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <QToolButton>
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
    void newLayerPopupThroughRealDock();
    void nativeMaterialAndOpaqueFallback();
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
    Preferences::instance()->setApplicationStyle(Preferences::TiledStyle);
    StyleHelper::initialize();
    qInfo() << "Editor theme:" << QApplication::style()->objectName();
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
    MacGlass::install(&window);
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

void test_LayerWidgets::newLayerPopupThroughRealDock()
{
    auto document = std::make_unique<MapDocument>(makeMap());
    MapDocument other(makeMap());
    MapDocumentActionHandler handler;
    QMainWindow window;
    // MainWindow normally registers this unrelated toolbar action. The fixture
    // does not test highlighting; all New Layer actions/slots are production.
    QAction highlight(&window);
    highlight.setCheckable(true);
    ActionManager::registerAction(&highlight, "HighlightCurrentLayer");
    const auto unregister = qScopeGuard([&] {
        ActionManager::unregisterAction(&highlight, "HighlightCurrentLayer");
    });
    auto dock = new LayerDock(&window);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    window.resize(640, 480);
    handler.setMapDocument(document.get());
    dock->setMapDocument(document.get());
    MacGlass::install(&window);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QToolButton *button = nullptr;
    for (auto candidate : dock->findChildren<QToolButton *>()) {
        // QToolBar also owns an InstantPopup overflow-extension button.
        // Identify the production New Layer button by its real menu actions,
        // not QObject child order (the extension menu may be empty/hidden).
        if (candidate->popupMode() == QToolButton::InstantPopup && candidate->menu()
                && candidate->menu()->actions().contains(handler.actionAddTileLayer())
                && candidate->menu()->actions().contains(handler.actionAddGroupLayer())) {
            button = candidate;
            break;
        }
    }
    QVERIFY(button);
    QMenu *menu = button->menu();
    const QList<QAction *> actions = {handler.actionAddTileLayer(),
                                      handler.actionAddObjectGroup(),
                                      handler.actionAddImageLayer(),
                                      handler.actionAddGroupLayer()};
    const QList<Layer::TypeFlag> types = {Layer::TileLayerType, Layer::ObjectGroupType,
                                         Layer::ImageLayerType, Layer::GroupLayerType};
    QSignalSpy renameRequests(document.get(), &MapDocument::editLayerNameRequested);
    QVERIFY(renameRequests.isValid());
    for (int cycle = 0; cycle < 8; ++cycle) {
        if (cycle == 4) {
            dock->setFloating(true);
            QVERIFY(dock->isFloating());
            QVERIFY(QTest::qWaitForWindowExposed(dock));
            dock->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(dock));
            QVERIFY(!MacGlass::isActive(dock));
            QTRY_COMPARE(dock->palette().color(QPalette::Window).alpha(), 255);
        }
        QTRY_VERIFY(button->isVisible());
        QVERIFY(button->isEnabled());
        QVERIFY(button->rect().isValid());
        qInfo() << "Popup cycle" << cycle << "button" << button->objectName()
                << "geometry" << button->geometry() << "actions" << menu->actions().size();
        capture(*dock, QStringLiteral("real-dock-before-popup"));
        QAction *action = actions.at(cycle % actions.size());
        QVERIFY(menu->actions().contains(action));
        QVERIFY(action->isEnabled());
        QSignalSpy triggered(action, &QAction::triggered);
        bool sawVisiblePopup = false;
        bool timerFired = false;
        QTimer selectionTimer;
        selectionTimer.setSingleShot(true);
        // InstantPopup enters a nested event loop. Select via the actual menu,
        // not QAction::trigger or direct MapDocument calls.
        // A stack-owned timer disconnects on every return path, so no callback
        // can retain references to this iteration after a failed assertion.
        connect(&selectionTimer, &QTimer::timeout, &selectionTimer, [&, action, menu] {
            sawVisiblePopup = menu->isVisible();
            capture(*menu, QStringLiteral("new-layer-popup"));
            menu->setActiveAction(action);
            QTest::keyClick(menu, Qt::Key_Return);
            menu->close(); // Bound dismissal even if a platform rejects Return.
            timerFired = true;
        });
        selectionTimer.start(50);
        QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, button->rect().center());
        QTRY_VERIFY(timerFired);
        QVERIFY2(sawVisiblePopup, "New Layer popup was not actually visible");
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(renameRequests.count(), cycle + 1);
        // addLayer inserts beside currentLayer (using its parentLayer), not
        // inside a selected group. All menu-created layers here remain roots.
        QVERIFY(!document->currentLayer()->parentLayer());
        QCOMPARE(document->map()->layerCount(), cycle + 1);
        QCOMPARE(document->currentLayer()->layerType(), types.at(cycle % types.size()));
        QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
        auto editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        const QString originalName = document->currentLayer()->name();
        const QString name = QStringLiteral("Menu layer %1").arg(cycle);
        editor->selectAll();
        QTest::keyClicks(editor, name);
        capture(*dock, QStringLiteral("real-dock-inline-rename"));
        if (cycle == 7) {
            // Switch away while the popup-triggered rename editor is active.
            handler.setMapDocument(&other);
            dock->setMapDocument(&other);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            QCOMPARE(other.map()->layerCount(), 0);
            handler.setMapDocument(nullptr);
            dock->setMapDocument(nullptr);
            document.reset();
        } else if (cycle % 2) {
            QTest::keyClick(editor, Qt::Key_Escape);
            QCOMPARE(document->currentLayer()->name(), originalName);
        } else {
            QTest::keyClick(editor, Qt::Key_Return);
            QTRY_COMPARE(document->currentLayer()->name(), name);
        }
    }
    dock->setFloating(false);
    QVERIFY(!dock->isFloating());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QTRY_VERIFY(MacGlass::isActive(&window));
    capture(window, QStringLiteral("real-dock-detached"));
}

// Use compositor pixels: QWidget::grab alone cannot capture an AppKit material
// behind Qt. A changed backdrop must visibly change the empty sidebar while
// the deliberately opaque canvas stays the same.
void test_LayerWidgets::nativeMaterialAndOpaqueFallback()
{
    QWidget backdrop;
    backdrop.setAutoFillBackground(true);
    auto backdropPalette = backdrop.palette();
    backdropPalette.setColor(QPalette::Window, QColor(210, 65, 45));
    backdrop.setPalette(backdropPalette);
    backdrop.resize(900, 700);
    backdrop.move(80, 80);
    backdrop.show();
    QVERIFY(QTest::qWaitForWindowExposed(&backdrop));

    QMainWindow window;
    auto canvas = new QWidget(&window);
    canvas->setAutoFillBackground(true);
    QPalette canvasPalette = QApplication::palette();
    canvasPalette.setColor(QPalette::Window, QColor(35, 42, 49));
    canvas->setPalette(canvasPalette);
    window.setCentralWidget(canvas);
    auto dock = new QDockWidget(QStringLiteral("Native material"), &window);
    auto view = new QTreeView(dock);
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("Readable native sidebar")));
    view->setModel(&model);
    view->setHeaderHidden(true);
    dock->setWidget(view);
    dock->setMinimumWidth(260);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    window.resize(640, 480);
    window.move(180, 160);
    MacGlass::install(&window);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    capture(window, QStringLiteral("native-material-initial-widget"));
    // A separate process in CI verifies the real system preference, before the
    // isolated runner switches it off to exercise visible native material.
    if (qEnvironmentVariableIsSet("TILED_EXPECT_SYSTEM_REDUCED_TRANSPARENCY")) {
        QTRY_VERIFY(window.property("tiledNativeGlassState").toString().contains(
            QStringLiteral("reduce-transparency=1")));
        QVERIFY(!MacGlass::isActive(&window));
        QCOMPARE(window.palette().color(QPalette::Window).alpha(), 255);
        QCOMPARE(view->palette().color(QPalette::Base).alpha(), 255);
        QCOMPARE(canvas->palette().color(QPalette::Window).alpha(), 255);
        capture(window, QStringLiteral("native-material-system-opaque"));
        return;
    }
    QTRY_VERIFY2(MacGlass::isActive(&window), qPrintable(window.property("tiledNativeGlassState").toString()));
    QCOMPARE(canvas->palette().color(QPalette::Window).alpha(), 255);
    // Exercise native-child geometry and preserve the normal framed root window.
    window.resize(680, 500);
    window.move(170, 150);
    QTest::qWait(100);
    QTRY_VERIFY(MacGlass::isActive(&window));

    const auto screenshot = [&](const QString &name) {
        const QPoint position = window.mapToGlobal(QPoint());
        QPixmap pixels = window.screen()->grabWindow(0, position.x(), position.y(),
                                                     window.width(), window.height());
        const QString directory = qEnvironmentVariable("TILED_WIDGET_ARTIFACTS");
        if (!directory.isEmpty())
            pixels.save(QDir(directory).filePath(name + QStringLiteral(".png")));
        return pixels.toImage();
    };
    const auto sample = [&](const QImage &image, QWidget *target) {
        const QPoint point = target->mapTo(&window, target->rect().center());
        const int x = qBound(0, point.x() * image.width() / window.width(), image.width() - 1);
        const int y = qBound(0, point.y() * image.height() / window.height(), image.height() - 1);
        return image.pixelColor(x, y);
    };
    QTest::qWait(400); // Allow AppKit's asynchronous backdrop compositor to settle.
    const auto red = screenshot(QStringLiteral("native-material-red-backdrop"));
    QVERIFY2(!red.isNull(), "Compositor capture unavailable; native material not visually verified");
    backdropPalette.setColor(QPalette::Window, QColor(45, 95, 215));
    backdrop.setPalette(backdropPalette);
    backdrop.repaint();
    QTest::qWait(400);
    const auto blue = screenshot(QStringLiteral("native-material-blue-backdrop"));
    QVERIFY(!blue.isNull());
    const QColor redSidebar = sample(red, view->viewport());
    const QColor blueSidebar = sample(blue, view->viewport());
    qInfo() << "Native material backdrop samples" << redSidebar << blueSidebar;
    const int difference = qAbs(redSidebar.red() - blueSidebar.red())
            + qAbs(redSidebar.green() - blueSidebar.green())
            + qAbs(redSidebar.blue() - blueSidebar.blue());
    QVERIFY2(difference > 3, "AppKit material is installed but no visible backdrop response was captured");
    QCOMPARE(sample(red, canvas), sample(blue, canvas));

    const QByteArray previous = qgetenv("TILED_DISABLE_NATIVE_GLASS");
    const bool wasSet = qEnvironmentVariableIsSet("TILED_DISABLE_NATIVE_GLASS");
    const auto restore = qScopeGuard([&] {
        if (wasSet)
            qputenv("TILED_DISABLE_NATIVE_GLASS", previous);
        else
            qunsetenv("TILED_DISABLE_NATIVE_GLASS");
        MacGlass::refresh(&window);
    });
    qputenv("TILED_DISABLE_NATIVE_GLASS", "1");
    MacGlass::refresh(&window);
    QVERIFY(!MacGlass::isActive(&window));
    QCOMPARE(window.palette().color(QPalette::Window).alpha(), 255);
    QCOMPARE(view->palette().color(QPalette::Base).alpha(), 255);
    QTest::qWait(200);
    screenshot(QStringLiteral("native-material-opaque-fallback"));
    qunsetenv("TILED_DISABLE_NATIVE_GLASS");
    MacGlass::refresh(&window);
    QTRY_VERIFY(MacGlass::isActive(&window));
}

QTEST_MAIN(test_LayerWidgets)
#include "test_layerwidgets.moc"
