/* Native macOS sidebar material for this fork. GPL-2.0-or-later. */
#include "macosglass.h"

#include <QApplication>
#include <QDockWidget>
#include <QDebug>
#include <QWindow>
#include <QEvent>
#include <QListView>
#include <QPointer>
#include <QPainter>
#include <QPaintEvent>
#include <QTableView>
#include <QSurfaceFormat>
#include <QScopedValueRollback>
#include <vector>
#include <QTimer>
#include <QTreeView>
#include <QWidget>
#include <QVariant>

#import <AppKit/AppKit.h>

// The material is decorative. It must never take a click away from Qt.
@interface TiledGlassEffectView : NSVisualEffectView
@end
@implementation TiledGlassEffectView
- (NSView *)hitTest:(NSPoint)point
{
    Q_UNUSED(point)
    return nil;
}
@end

namespace Tiled::MacGlass {
namespace {
const char controllerName[] = "tiledNativeGlassController";

class Controller final : public QObject
{
public:
    explicit Controller(QWidget *window) : QObject(window), mWindow(window)
    {
        setObjectName(QLatin1String(controllerName));
        report(QStringLiteral("installed-before-show=%1 visible=%2 is-window=%3 platform=%4")
               .arg(!window->isVisible()).arg(window->isVisible()).arg(window->isWindow())
               .arg(QGuiApplication::platformName()));
        // Native child QWidget textures are cropped from the root backing
        // store. Keep alpha there, but paint opaque everywhere except material
        // docks below; never replace/reparent the Qt native content view.
        window->setAttribute(Qt::WA_TranslucentBackground);
        window->setAutoFillBackground(false);
        window->installEventFilter(this);
        connect(qApp, &QApplication::paletteChanged, this, [this] { schedule(); });
        const QPointer<Controller> guard(this);
        mObserver = [[[NSWorkspace sharedWorkspace] notificationCenter]
            addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
            object:nil queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification *) {
                if (guard)
                    guard->schedule();
            }];
        [mObserver retain];
        schedule();
    }

    ~Controller() override
    {
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:mObserver];
        [mObserver release];
        for (auto &entry : mDocks) {
            [entry.effect removeFromSuperview];
            [entry.effect release];
        }
    }

    bool active() const
    {
        for (const auto &entry : mDocks)
            if (entry.dock && entry.effect && ![entry.effect isHidden]
                    && [entry.effect superview])
                return true;
        return false;
    }

    void schedule()
    {
        if (mScheduled)
            return;
        mScheduled = true;
        QTimer::singleShot(0, this, [this] {
            mScheduled = false;
            update();
        });
    }

    void update()
    {
        if (mUpdating)
            return;
        QScopedValueRollback<bool> updating(mUpdating, true);
        const bool reduce = [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceTransparency];
        const bool enabled = !reduce && !qEnvironmentVariableIsSet("TILED_DISABLE_NATIVE_GLASS");
        if (!mWindow)
            return;

        for (auto dock : mWindow->findChildren<QDockWidget *>()) {
            bool known = false;
            for (const auto &entry : mDocks)
                known |= entry.dock == dock;
            if (known)
                continue;
            mDocks.push_back({dock, nil});
            dock->installEventFilter(this);
            connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
                if (floating)
                    applyPalette(dock, false);
                schedule();
            });
            connect(dock, &QDockWidget::visibilityChanged, this, [this] { schedule(); });
        }

        int attached = 0;
        for (auto &entry : mDocks) {
            auto dock = entry.dock.data();
            bool visible = enabled && mWindow->isVisible() && dock
                    && !dock->isFloating() && dock->isVisible();
            if (visible && !dock->internalWinId()) {
                // Native children have their own alpha surface. Request its
                // format BEFORE creating its handle, not after a float/show.
                // Restore the application default synchronously; other windows
                // retain their normal formats. No event loop runs in this scope.
                const QSurfaceFormat original = QSurfaceFormat::defaultFormat();
                QSurfaceFormat alpha = original;
                alpha.setAlphaBufferSize(8);
                QSurfaceFormat::setDefaultFormat(alpha);
                dock->setAttribute(Qt::WA_DontCreateNativeAncestors);
                dock->setAttribute(Qt::WA_TranslucentBackground);
                dock->winId();
                QSurfaceFormat::setDefaultFormat(original);
            }
            if (visible) {
                NSView *view = reinterpret_cast<NSView *>(dock->winId());
                NSView *parent = [view superview];
                NSWindow *nativeWindow = [view window];
                // Preserve Qt's root contentView invariant. Material is ONLY a
                // sibling of a native dock inside Qt's own content hierarchy,
                // never a child of AppKit's private NSThemeFrame.
                const bool inContent = parent && nativeWindow
                        && (parent == [nativeWindow contentView]
                            || [parent isDescendantOf:[nativeWindow contentView]]);
                visible = inContent && dock->windowHandle()
                        && dock->windowHandle()->format().alphaBufferSize() > 0;
                if (visible) {
                    if (!entry.effect) {
                        entry.effect = [[TiledGlassEffectView alloc] initWithFrame:[view frame]];
                        [entry.effect setMaterial:NSVisualEffectMaterialSidebar];
                        [entry.effect setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
                        [entry.effect setState:NSVisualEffectStateFollowsWindowActiveState];
                    }
                    if ([entry.effect superview] != parent) {
                        [entry.effect removeFromSuperview];
                        [parent addSubview:entry.effect positioned:NSWindowBelow relativeTo:view];
                    }
                    [entry.effect setFrame:[view frame]];
                    [entry.effect setHidden:NO];
                    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
                    [entry.effect setAppearance:[NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua]];
                    ++attached;
                }
            }
            if (!visible)
                [entry.effect removeFromSuperview];
            if (dock)
                applyPalette(dock, visible);
        }
        report(QStringLiteral("enabled=%1 reduce-transparency=%2 env-disabled=%3 attached-docks=%4 root-content-unchanged=1")
               .arg(enabled).arg(reduce)
               .arg(qEnvironmentVariableIsSet("TILED_DISABLE_NATIVE_GLASS")).arg(attached));
        mWindow->update(); // Refill old holes and clear newly material-backed docks.
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == mWindow && event->type() == QEvent::Paint) {
            // Cocoa clears the alpha backing store's dirty region before paint.
            // Retain the normal opaque palette (toolbars/canvas inherit it),
            // filling the root except actual attached material dock rectangles.
            QRegion opaque = static_cast<QPaintEvent *>(event)->region();
            for (const auto &entry : mDocks) {
                if (entry.dock && entry.dock->isVisible() && !entry.dock->isFloating()
                        && entry.effect && [entry.effect superview] && ![entry.effect isHidden])
                    opaque -= QRect(entry.dock->mapTo(mWindow, QPoint()), entry.dock->size());
            }
            QPainter painter(mWindow);
            painter.setClipRegion(opaque);
            painter.fillRect(mWindow->rect(), mWindow->palette().brush(QPalette::Window));
            // Let normal QMainWindow/child painting proceed after painter ends.
            return false;
        }
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::ParentChange:
        case QEvent::WinIdChange:
        case QEvent::ChildAdded:
        case QEvent::WindowStateChange:
            schedule();
            break;
        default:
            break;
        }
        return false;
    }

private:
    void report(const QString &state)
    {
        if (mWindow->property("tiledNativeGlassState").toString() == state)
            return;
        mWindow->setProperty("tiledNativeGlassState", state);
        if (qEnvironmentVariableIsSet("TILED_WIDGET_ARTIFACTS"))
            qInfo() << "Native glass:" << state;
    }

    static QPalette originalPalette(QWidget *widget)
    {
        if (!widget->property("tiledGlassSavedPalette").isValid()) {
            widget->setProperty("tiledGlassSavedPalette", widget->palette());
            widget->setProperty("tiledGlassHadCustomPalette", widget->testAttribute(Qt::WA_SetPalette));
        }
        const QPalette applicationPalette = QApplication::palette(widget);
        if (!widget->property("tiledGlassHadCustomPalette").toBool())
            return applicationPalette;
        return widget->property("tiledGlassSavedPalette").value<QPalette>().resolve(applicationPalette);
    }

    static void applyPalette(QWidget *window, bool enabled)
    {
        QPalette palette = originalPalette(window);
        if (enabled)
            palette.setColor(QPalette::Window, Qt::transparent);
        window->setPalette(palette);
        window->setAutoFillBackground(!enabled);
        window->setAttribute(Qt::WA_NoSystemBackground, enabled);

        // Scope transparency to dock content, not every view/dialog descended
        // from the main window. Never touch the canvas or document editors.
        auto dock = qobject_cast<QDockWidget *>(window);
        if (!dock || !dock->widget())
            return;
        auto views = dock->widget()->findChildren<QAbstractItemView *>();
        if (auto ownView = qobject_cast<QAbstractItemView *>(dock->widget()))
            views.append(ownView);
        for (auto view : views) {
            if (!qobject_cast<QTreeView *>(view) && !qobject_cast<QListView *>(view)
                    && !qobject_cast<QTableView *>(view))
                continue;
            QPalette itemPalette = originalPalette(view);
            if (enabled) {
                itemPalette.setColor(QPalette::Base, Qt::transparent);
                QColor alternate = itemPalette.color(QPalette::AlternateBase);
                alternate.setAlpha(30);
                itemPalette.setColor(QPalette::AlternateBase, alternate);
            }
            view->setPalette(itemPalette);
            auto viewport = view->viewport();
            if (!viewport->property("tiledGlassSavedAutoFill").isValid())
                viewport->setProperty("tiledGlassSavedAutoFill", viewport->autoFillBackground());
            viewport->setAutoFillBackground(enabled ? false : viewport->property("tiledGlassSavedAutoFill").toBool());
            viewport->update();
        }
    }

    QPointer<QWidget> mWindow;
    struct DockMaterial {
        QPointer<QDockWidget> dock;
        NSVisualEffectView *effect; // owned; Qt native views are not owned
    };
    std::vector<DockMaterial> mDocks;
    bool mUpdating = false;
    id mObserver = nil;
    bool mScheduled = false;
};

Controller *controller(QWidget *window)
{
    if (!window)
        return nullptr;
    return static_cast<Controller *>(window->findChild<QObject *>(
        QLatin1String(controllerName), Qt::FindDirectChildrenOnly));
}
}

void install(QWidget *window)
{
    if (!window || QGuiApplication::platformName() != QLatin1String("cocoa"))
        return;
    if (window->isVisible() && !window->testAttribute(Qt::WA_TranslucentBackground)) {
        window->setProperty("tiledNativeGlassState", QStringLiteral("installation-rejected: already-visible-without-alpha"));
        return;
    }
    if (!controller(window))
        new Controller(window);
}

void refresh(QWidget *window)
{
    if (auto c = controller(window))
        c->update();
}

bool isActive(QWidget *window)
{
    auto c = controller(window);
    return c && c->active();
}
}
