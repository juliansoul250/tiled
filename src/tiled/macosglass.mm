/* Native macOS sidebar material for this fork. GPL-2.0-or-later. */
#include "macosglass.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QListView>
#include <QPointer>
#include <QTableView>
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
        // An alpha backing store lets the AppKit sibling show through only the
        // chrome/sidebar pixels that we clear. Canvas and editor Base colors
        // remain opaque. Keep the normal native window frame and traffic lights.
        window->setAttribute(Qt::WA_TranslucentBackground);
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
        [mEffect removeFromSuperview];
        [mEffect release];
    }

    bool active() const
    {
        return mEffect && ![mEffect isHidden] && [mEffect superview]
                && [mEffect isKindOfClass:[NSVisualEffectView class]];
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
        if (!mWindow || !mWindow->isWindow() || !mWindow->isVisible()) {
            [mEffect removeFromSuperview];
            return;
        }
        // Re-fetch native handles after Show/WinIdChange; never retain a Qt view
        // or replace/reparent its contentView (docking can recreate its handle).
        NSView *qtView = reinterpret_cast<NSView *>(mWindow->winId());
        NSWindow *nativeWindow = [qtView window];
        NSView *parent = [qtView superview];
        if (!nativeWindow || !parent)
            return;

        const bool reduce = [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceTransparency];
        const bool enabled = !reduce && !qEnvironmentVariableIsSet("TILED_DISABLE_NATIVE_GLASS");
        if (enabled) {
            if (!mEffect) {
                mEffect = [[TiledGlassEffectView alloc] initWithFrame:[qtView frame]];
                [mEffect setMaterial:NSVisualEffectMaterialSidebar];
                [mEffect setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
                [mEffect setState:NSVisualEffectStateFollowsWindowActiveState];
                [mEffect setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
            }
            // Sibling BELOW Qt, not a child overlay on top of its raster text.
            if ([mEffect superview] != parent) {
                [mEffect removeFromSuperview];
                [parent addSubview:mEffect positioned:NSWindowBelow relativeTo:qtView];
            }
            [mEffect setFrame:[qtView frame]];
            [mEffect setHidden:NO];
            const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
            [mEffect setAppearance:[NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua]];
        } else {
            [mEffect setHidden:YES];
        }
        [nativeWindow setOpaque:!enabled];
        [nativeWindow setBackgroundColor:enabled ? [NSColor clearColor] : [NSColor windowBackgroundColor]];
        [nativeWindow setTitlebarAppearsTransparent:enabled];
        applyPalette(mWindow, enabled);

        for (auto dock : mWindow->findChildren<QDockWidget *>()) {
            if (!dock->property("tiledGlassDockConnected").toBool()) {
                dock->setProperty("tiledGlassDockConnected", true);
                connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
                    // Native floating handles already exist here. Never try to
                    // retrofit an alpha surface: use the readable opaque path.
                    if (floating)
                        applyPalette(dock, false);
                    schedule();
                });
            }
            applyPalette(dock, enabled && !dock->isFloating());
        }
        mWindow->update();
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::Resize:
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
    NSVisualEffectView *mEffect = nil; // owned; Qt native views are not owned
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
    // Qt does not support enabling translucent backing stores after show.
    // Floating panels intentionally keep their existing opaque native surface.
    if (window->isVisible() && !window->testAttribute(Qt::WA_TranslucentBackground))
        return;
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
