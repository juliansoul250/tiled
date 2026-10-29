/* Regression coverage for the editor's layer lifecycle and model contract. */
#include "grouplayer.h"
#include "layer.h"
#include "layermodel.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobjectmodel.h"
#include "mapreader.h"
#include "mapwriter.h"

#include <QAbstractItemModelTester>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest/QtTest>

using namespace Tiled;

class test_Layers : public QObject
{
    Q_OBJECT
private slots:
    void rejectsOutOfRangeColumns();
    void groupChildrenBelongToFirstColumn();
    void modelContract();
    void objectModelContract();
    void removedGroupCacheDoesNotLeakIntoNewGroups();
    void replacingUndoneHistoryDoesNotLeakGroupChildren();
    void lifecycle_data();
    void lifecycle();
};

static std::unique_ptr<Map> makeMap(Map::Orientation orientation = Map::Orthogonal,
                                    bool infinite = false)
{
    Map::Parameters parameters;
    parameters.orientation = orientation;
    parameters.width = 16;
    parameters.height = 12;
    parameters.tileWidth = 32;
    parameters.tileHeight = orientation == Map::Isometric ? 16 : 32;
    parameters.infinite = infinite;
    if (orientation == Map::Hexagonal)
        parameters.hexSideLength = 16;
    return std::make_unique<Map>(parameters);
}

void test_Layers::rejectsOutOfRangeColumns()
{
    MapDocument document(makeMap());
    document.addLayer(Layer::TileLayerType);
    QAbstractItemModel *model = document.layerModel();
    QVERIFY(!model->index(0, model->columnCount()).isValid());
    QVERIFY(!model->index(-1, 0).isValid());
    QVERIFY(!model->index(model->rowCount(), 0).isValid());
    QVERIFY(!model->index(0, -1).isValid());
}

void test_Layers::groupChildrenBelongToFirstColumn()
{
    auto map = makeMap();
    auto group = std::make_unique<GroupLayer>(QStringLiteral("Group"), 0, 0);
    group->addLayer(std::make_unique<GroupLayer>(QStringLiteral("Child"), 0, 0));
    map->addLayer(std::move(group));
    MapDocument document(std::move(map));
    QAbstractItemModel *model = document.layerModel();
    QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    for (int column = 1; column < model->columnCount(); ++column) {
        const auto parent = model->index(0, column);
        QCOMPARE(model->rowCount(parent), 0);
        QVERIFY(!model->index(0, 0, parent).isValid());
    }
}

void test_Layers::modelContract()
{
    MapDocument document(makeMap());
    QAbstractItemModelTester tester(document.layerModel(),
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto tile = document.addLayer(Layer::TileLayerType);
    auto objects = document.addLayer(Layer::ObjectGroupType);
    document.groupLayers({tile, objects});
    document.undoStack()->undo();
    document.undoStack()->redo();
    document.removeLayers(document.map()->layers());
    document.undoStack()->undo();
    document.undoStack()->redo();
}

void test_Layers::objectModelContract()
{
    MapDocument document(makeMap());
    QAbstractItemModel *model = document.mapObjectModel();
    QAbstractItemModelTester tester(model,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto objects = document.addLayer(Layer::ObjectGroupType);
    document.groupLayers({objects});
    QVERIFY(!model->index(0, model->columnCount()).isValid());
    document.undoStack()->undo();
    document.undoStack()->redo();
    document.removeLayers(document.map()->layers());
    document.undoStack()->undo();
    document.undoStack()->redo();
}

void test_Layers::removedGroupCacheDoesNotLeakIntoNewGroups()
{
    MapDocument document(makeMap());
    QAbstractItemModel *objects = document.mapObjectModel();
    for (int cycle = 0; cycle < 200; ++cycle) {
        auto objectLayer = document.addLayer(Layer::ObjectGroupType);
        document.groupLayers({objectLayer});
        QCOMPARE(objects->rowCount(), 1);
        const auto groupIndex = objects->index(0, 0);
        QCOMPARE(objects->rowCount(groupIndex), 1); // Populate the child cache.
        document.groupLayers({objectLayer->parentLayer()});
        QCOMPARE(objects->rowCount(objects->index(0, 0)), 1);
        QCOMPARE(objects->rowCount(objects->index(0, 0, objects->index(0, 0))), 1);
        document.removeLayers(document.map()->layers());
        document.undoStack()->clear(); // Frees the removed group and its child.
        QCOMPARE(objects->rowCount(), 0);

        auto emptyGroup = document.addLayer(Layer::GroupLayerType);
        QCOMPARE(emptyGroup->asGroupLayer()->layerCount(), 0);
        QCOMPARE(objects->rowCount(), 1);
        QCOMPARE(objects->rowCount(objects->index(0, 0)), 0);
        document.removeLayers(document.map()->layers());
        document.undoStack()->clear();
    }
}

void test_Layers::replacingUndoneHistoryDoesNotLeakGroupChildren()
{
    MapDocument document(makeMap());
    QAbstractItemModel *objects = document.mapObjectModel();
    for (int cycle = 0; cycle < 200; ++cycle) {
        auto objectLayer = document.addLayer(Layer::ObjectGroupType);
        document.groupLayers({objectLayer});
        QCOMPARE(objects->rowCount(objects->index(0, 0)), 1);
        while (document.undoStack()->canUndo())
            document.undoStack()->undo();
        QCOMPARE(objects->rowCount(), 0);

        // This new edit discards the redo branch, freeing its detached groups.
        auto emptyGroup = document.addLayer(Layer::GroupLayerType);
        QVERIFY(!document.undoStack()->canRedo());
        QCOMPARE(emptyGroup->asGroupLayer()->layerCount(), 0);
        QCOMPARE(objects->rowCount(objects->index(0, 0)), 0);
        document.removeLayers(document.map()->layers());
        document.undoStack()->clear();
    }
}

void test_Layers::lifecycle_data()
{
    QTest::addColumn<int>("orientation");
    QTest::addColumn<bool>("infinite");
    for (const auto &[name, orientation] : {
             std::pair{"square", Map::Orthogonal},
             std::pair{"isometric", Map::Isometric},
             std::pair{"hexagonal", Map::Hexagonal}}) {
        QTest::newRow(name) << int(orientation) << false;
        QTest::newRow(qPrintable(QString::fromLatin1(name) + QStringLiteral("-infinite")))
                << int(orientation) << true;
    }
}

static void compareLayers(const QList<Layer *> &actual, const QList<Layer *> &expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(actual[i]->name(), expected[i]->name());
        QCOMPARE(actual[i]->layerType(), expected[i]->layerType());
        QCOMPARE(actual[i]->id(), expected[i]->id());
        QCOMPARE(actual[i]->isVisible(), expected[i]->isVisible());
        QCOMPARE(actual[i]->isLocked(), expected[i]->isLocked());
        if (auto expectedGroup = expected[i]->asGroupLayer())
            compareLayers(actual[i]->asGroupLayer()->layers(), expectedGroup->layers());
    }
}

void test_Layers::lifecycle()
{
    QFETCH(int, orientation);
    QFETCH(bool, infinite);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MapDocument document(makeMap(Map::Orientation(orientation), infinite));

    for (int cycle = 0; cycle < 40; ++cycle) {
        auto tile = document.addLayer(Layer::TileLayerType);
        auto objects = document.addLayer(Layer::ObjectGroupType);
        auto image = document.addLayer(Layer::ImageLayerType);
        document.addLayer(Layer::GroupLayerType);
        QCOMPARE(document.map()->layerCount(), 4);

        // Nested groups and selecting both ancestors and children exercise the
        // removal ordering used by the Layers dock.
        document.groupLayers({tile, objects});
        QCOMPARE(document.map()->layerCount(), 3);
        auto group = tile->parentLayer();
        QVERIFY(group);
        document.switchSelectedLayers({group, tile, objects});
        document.removeLayers(document.selectedLayers());
        QCOMPARE(document.map()->layerCount(), 2);
        QVERIFY(!document.currentLayer() || document.currentLayer()->map() == document.map());
        for (auto selected : document.selectedLayers())
            QCOMPARE(selected->map(), document.map());
        document.undoStack()->undo();
        QCOMPARE(document.map()->layerCount(), 3);
        document.undoStack()->redo();
        QCOMPARE(document.map()->layerCount(), 2);
        document.undoStack()->undo();
        QCOMPARE(document.map()->layerCount(), 3);
        QCOMPARE(image->map(), document.map());

        const QString path = directory.filePath(QStringLiteral("cycle-%1.tmx").arg(cycle));
        MapWriter writer;
        QVERIFY2(writer.writeMap(document.map(), path), qPrintable(writer.errorString()));
        MapReader reader;
        const auto reopened = reader.readMap(path);
        QVERIFY2(reopened, qPrintable(reader.errorString()));
        QCOMPARE(reopened->orientation(), document.map()->orientation());
        QCOMPARE(reopened->infinite(), document.map()->infinite());
        QCOMPARE(reopened->size(), document.map()->size());
        QCOMPARE(reopened->tileWidth(), document.map()->tileWidth());
        QCOMPARE(reopened->tileHeight(), document.map()->tileHeight());
        compareLayers(reopened->layers(), document.map()->layers());

        document.removeLayers(document.map()->layers());
        QCOMPARE(document.map()->layerCount(), 0);
        QVERIFY(document.selectedLayers().isEmpty());
        QVERIFY(!document.currentLayer());
        document.undoStack()->undo();
        QCOMPARE(document.map()->layerCount(), 3);
        document.undoStack()->redo();
        QCOMPARE(document.map()->layerCount(), 0);
        document.undoStack()->clear();
        QCoreApplication::processEvents();
    }
}

QTEST_MAIN(test_Layers)
#include "test_layers.moc"
