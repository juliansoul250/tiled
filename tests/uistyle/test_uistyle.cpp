#include "tiledproxystyle.h"
#include "iconcheckdelegate.h"
#include "utils.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleOption>
#include <QtTest>

#include <cmath>

using namespace Tiled;

class test_UiStyle : public QObject
{
    Q_OBJECT

private slots:
    void paletteSwitching();
    void comfortableMetrics();
    void toolButtonStates_data();
    void toolButtonStates();
    void selectedTabAccent_data();
    void selectedTabAccent();
    void dockTitleRestoresPainter();
    void lockIconPalette_data();
    void lockIconPalette();
    void lockIconAlpha();
    void visibilityIconsUnchanged();
    void darkCheckboxOutline();
};

static QPalette editorPalette(bool dark)
{
    QPalette palette(dark ? QColor(42, 48, 56) : QColor(211, 211, 211));
    palette.setColor(QPalette::WindowText, dark ? QColor(232, 232, 232) : QColor(51, 51, 51));
    palette.setColor(QPalette::Text, palette.windowText().color());
    palette.setColor(QPalette::Base, dark ? QColor(40, 43, 46) : Qt::white);
    palette.setColor(QPalette::Highlight, QColor(55, 124, 116));
    return palette;
}

void test_UiStyle::paletteSwitching()
{
    TiledProxyStyle style(editorPalette(true), QStyleFactory::create(QStringLiteral("Fusion")));
    QVERIFY(style.isDark());
    style.setPalette(editorPalette(false));
    QVERIFY(!style.isDark());
    style.setPalette(editorPalette(true));
    QVERIFY(style.isDark());
}

void test_UiStyle::comfortableMetrics()
{
    TiledProxyStyle style(editorPalette(true), QStyleFactory::create(QStringLiteral("Fusion")));
    QStyleOptionViewItem item;
    item.font = QApplication::font();
    item.fontMetrics = QFontMetrics(item.font);
    item.features = QStyleOptionViewItem::HasDisplay;
    item.text = QStringLiteral("Foreground");
    const auto size = style.sizeFromContents(QStyle::CT_ItemViewItem, &item, QSize(), nullptr);
    QVERIFY(size.height() >= item.fontMetrics.height() + 6);

    // Custom fonts must not be clipped by a fixed row height.
    item.font.setPointSize(24);
    item.fontMetrics = QFontMetrics(item.font);
    const auto largeSize = style.sizeFromContents(QStyle::CT_ItemViewItem, &item, QSize(), nullptr);
    QVERIFY(largeSize.height() >= item.fontMetrics.height() + 6);
    QVERIFY(largeSize.height() > size.height());

    QStyleOptionToolButton button;
    button.iconSize = QSize(16, 16);
    const QSize content(16, 16);
    const auto baseSize = style.baseStyle()->sizeFromContents(QStyle::CT_ToolButton, &button, content, nullptr);
    const auto buttonSize = style.sizeFromContents(QStyle::CT_ToolButton, &button, content, nullptr);
    QVERIFY(buttonSize.width() > baseSize.width());
    QVERIFY(buttonSize.height() > baseSize.height());
    QVERIFY(style.pixelMetric(QStyle::PM_ToolBarSeparatorExtent) > style.pixelMetric(QStyle::PM_ToolBarItemSpacing));
}

void test_UiStyle::toolButtonStates_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("dark") << true;
    QTest::newRow("light") << false;
}

void test_UiStyle::toolButtonStates()
{
    QFETCH(bool, dark);
    const QPalette palette = editorPalette(dark);
    TiledProxyStyle style(palette, QStyleFactory::create(QStringLiteral("Fusion")));

    const auto render = [&](QStyle::State state, bool autoRaise = true) {
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QStyleOptionToolButton option;
        option.rect = image.rect();
        option.palette = palette;
        option.state = state;
        if (autoRaise)
            option.state |= QStyle::State_AutoRaise;
        style.drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, nullptr);
        return image;
    };

    const QImage idle = render(QStyle::State_Enabled);
    const QImage hovered = render(QStyle::State_Enabled | QStyle::State_MouseOver);
    const QImage selected = render(QStyle::State_Enabled | QStyle::State_On);
    const QImage focused = render(QStyle::State_Enabled | QStyle::State_HasFocus);
    QCOMPARE(idle.pixelColor(16, 16).alpha(), 0);
    QVERIFY(hovered != idle);
    QVERIFY(selected != hovered);
    QVERIFY(focused != idle);
    QCOMPARE(render(QStyle::State_MouseOver), idle); // disabled hover is not actionable
    QVERIFY(render(QStyle::State_Enabled | QStyle::State_Raised, false) != idle);
}

void test_UiStyle::selectedTabAccent_data()
{
    QTest::addColumn<int>("shape");
    QTest::newRow("north") << int(QTabBar::RoundedNorth);
    QTest::newRow("south") << int(QTabBar::RoundedSouth);
    QTest::newRow("east") << int(QTabBar::RoundedEast);
    QTest::newRow("west") << int(QTabBar::RoundedWest);
}

void test_UiStyle::selectedTabAccent()
{
    QFETCH(int, shape);
    const QPalette palette = editorPalette(true);
    TiledProxyStyle style(palette, QStyleFactory::create(QStringLiteral("Fusion")));
    const bool vertical = shape == QTabBar::RoundedWest || shape == QTabBar::RoundedEast;
    const auto render = [&](bool selected) {
        QImage image(vertical ? QSize(32, 120) : QSize(120, 32), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QStyleOptionTab option;
        option.rect = image.rect();
        option.palette = palette;
        option.shape = QTabBar::Shape(shape);
        option.position = QStyleOptionTab::OnlyOneTab;
        option.state = QStyle::State_Enabled;
        if (selected)
            option.state |= QStyle::State_Selected;
        style.drawControl(QStyle::CE_TabBarTabShape, &option, &painter, nullptr);
        return image;
    };
    const auto accentPixels = [&](const QImage &image) {
        int pixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == palette.highlight().color())
                    ++pixels;
        return pixels;
    };
    const QImage selected = render(true);
    QVERIFY(accentPixels(selected) > 20);
    QCOMPARE(accentPixels(render(false)), 0);

    // The selected accent belongs to the outer edge, not the map/content edge.
    for (int y = 0; y < selected.height(); ++y) {
        for (int x = 0; x < selected.width(); ++x) {
            if (selected.pixelColor(x, y) != palette.highlight().color())
                continue;
            switch (QTabBar::Shape(shape)) {
            case QTabBar::RoundedNorth: QVERIFY(y < 4); break;
            case QTabBar::RoundedSouth: QVERIFY(y >= selected.height() - 4); break;
            case QTabBar::RoundedWest: QVERIFY(x < 4); break;
            case QTabBar::RoundedEast: QVERIFY(x >= selected.width() - 4); break;
            default: QFAIL("Unexpected tab shape");
            }
        }
    }
}

void test_UiStyle::dockTitleRestoresPainter()
{
    const QPalette palette = editorPalette(true);
    TiledProxyStyle style(palette, QStyleFactory::create(QStringLiteral("Fusion")));
    QImage image(240, 36, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const QFont font = painter.font();
    const QPen pen = painter.pen();
    const QBrush brush = painter.brush();
    QStyleOptionDockWidget option;
    option.rect = image.rect();
    option.palette = palette;
    option.title = QStringLiteral("Layers");
    style.drawControl(QStyle::CE_DockWidgetTitle, &option, &painter, nullptr);
    QCOMPARE(painter.font(), font);
    QCOMPARE(painter.pen(), pen);
    QCOMPARE(painter.brush(), brush);
}

// Expose only the paint hook; all actual icon loading and rendering stays in
// the production delegate. Resources are supplied by libtilededitor.
class TestIconCheckDelegate : public IconCheckDelegate
{
public:
    explicit TestIconCheckDelegate(IconType type) : IconCheckDelegate(type, true) {}
    using IconCheckDelegate::drawCheck;
};

static QImage renderCheck(TestIconCheckDelegate &delegate,
                          const QStyleOptionViewItem &option,
                          Qt::CheckState state)
{
    QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    delegate.drawCheck(&painter, option, option.rect, state);
    return image;
}

static QImage renderOriginalIcon(const QString &name, const QRect &rect)
{
    QIcon icon;
    for (int size : {14, 16, 24})
        icon.addFile(QStringLiteral(":/images/%1/%2.png").arg(size).arg(name));
    const QPixmap pixmap = icon.pixmap(Utils::smallIconSize());
    QImage image(rect.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QRect target(QPoint(), pixmap.size() / pixmap.devicePixelRatio());
    target.moveCenter(rect.center());
    painter.drawPixmap(target, pixmap);
    return image;
}

void test_UiStyle::lockIconPalette_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<int>("group");
    QTest::addColumn<bool>("selected");
    QTest::addColumn<bool>("locked");
    for (bool dark : {false, true}) {
        for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            for (bool selected : {false, true}) {
                for (bool locked : {false, true}) {
                    const QByteArray name = QByteArray(dark ? "dark" : "light")
                            + "-group" + QByteArray::number(group)
                            + (selected ? "-selected" : "-unselected")
                            + (locked ? "-locked" : "-unlocked");
                    QTest::newRow(name.constData()) << dark << int(group) << selected << locked;
                }
            }
        }
    }
}

void test_UiStyle::lockIconPalette()
{
    QFETCH(bool, dark);
    QFETCH(int, group);
    QFETCH(bool, selected);
    QFETCH(bool, locked);
    TestIconCheckDelegate delegate(IconCheckDelegate::LockedIcon);
    QStyleOptionViewItem option;
    option.rect = QRect(QPoint(), Utils::dpiScaled(QSize(24, 24)));
    option.palette = editorPalette(dark);
    option.palette.setColor(QPalette::Active, QPalette::Text, dark ? QColor(232, 232, 232) : QColor(51, 51, 51));
    option.palette.setColor(QPalette::Active, QPalette::HighlightedText, Qt::white);
    option.palette.setColor(QPalette::Inactive, QPalette::Text, QColor(192, 197, 203));
    option.palette.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor(239, 239, 239));
    option.palette.setColor(QPalette::Disabled, QPalette::Text, QColor(145, 150, 157));
    option.palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(178, 183, 189));
    option.state = QStyle::State_None;
    if (group != QPalette::Disabled)
        option.state |= QStyle::State_Enabled;
    if (group == QPalette::Active)
        option.state |= QStyle::State_Active;
    if (selected)
        option.state |= QStyle::State_Selected;

    const auto image = renderCheck(delegate, option, locked ? Qt::Checked : Qt::Unchecked);
    const QColor expected = option.palette.color(QPalette::ColorGroup(group),
                                                selected ? QPalette::HighlightedText : QPalette::Text);
    int opaquePixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 255) {
                ++opaquePixels;
                QCOMPARE(pixel, expected);
            }
        }
    }
    QVERIFY(opaquePixels > 20);  // Also fail if resources were not linked.
}

void test_UiStyle::lockIconAlpha()
{
    TestIconCheckDelegate delegate(IconCheckDelegate::LockedIcon);
    QStyleOptionViewItem option;
    option.rect = QRect(QPoint(), Utils::dpiScaled(QSize(24, 24)));
    option.state = QStyle::State_None; // disabled
    option.palette.setColor(QPalette::Disabled, QPalette::Text, QColor(232, 232, 232, 128));
    const auto original = renderOriginalIcon(QStringLiteral("locked"), option.rect);
    const auto tinted = renderCheck(delegate, option, Qt::Checked);
    for (int y = 0; y < tinted.height(); ++y) {
        for (int x = 0; x < tinted.width(); ++x) {
            const int expected = qRound(original.pixelColor(x, y).alpha() * 128.0 / 255.0);
            QVERIFY(qAbs(tinted.pixelColor(x, y).alpha() - expected) <= 1);
        }
    }
}

void test_UiStyle::visibilityIconsUnchanged()
{
    TestIconCheckDelegate delegate(IconCheckDelegate::VisibilityIcon);
    QStyleOptionViewItem option;
    option.rect = QRect(QPoint(), Utils::dpiScaled(QSize(24, 24)));
    for (auto state : {Qt::Checked, Qt::Unchecked}) {
        const QString name = state == Qt::Checked ? QStringLiteral("visible") : QStringLiteral("hidden");
        QCOMPARE(renderCheck(delegate, option, state), renderOriginalIcon(name, option.rect));
    }
}

void test_UiStyle::darkCheckboxOutline()
{
    const QPalette palette = editorPalette(true);
    TiledProxyStyle style(palette, QStyleFactory::create(QStringLiteral("Fusion")));
    const auto luminance = [](const QColor &color) {
        const auto channel = [](qreal value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF())
                + 0.0722 * channel(color.blueF());
    };
    for (bool focused : {false, true}) {
        QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(palette.window().color());
        QPainter painter(&image);
        QStyleOptionButton option;
        option.rect = QRect(4, 4, 16, 16);
        option.palette = palette;
        option.state = QStyle::State_Enabled | QStyle::State_Off;
        if (focused)
            option.state |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
        style.drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter, nullptr);
        painter.end();
        const qreal contrast = (luminance(image.pixelColor(8, 4)) + 0.05)
                / (luminance(palette.window().color()) + 0.05);
        QVERIFY2(contrast >= 3.0, "Unchecked dark checkbox edge must remain discoverable");
    }
}

QTEST_MAIN(test_UiStyle)
#include "test_uistyle.moc"
