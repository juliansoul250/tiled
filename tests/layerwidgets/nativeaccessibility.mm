// Native Cocoa accessibility-cache regression support. GPL-2.0-or-later.
#include "nativeaccessibility.h"
#include <QAccessible>
#include <QCoreApplication>
#include <QEvent>
#include <QWidget>
#include <set>
#import <AppKit/AppKit.h>

namespace {
void visit(id node, NativeAXCounts &counts, std::set<void *> &seen, int depth)
{
    if (!node || depth > 24 || seen.size() > 4000
            || !seen.insert(reinterpret_cast<void *>(node)).second)
        return;
    if ([node respondsToSelector:@selector(accessibilityRole)]) {
        NSString *role = [node accessibilityRole];
        if ([role isEqualToString:NSAccessibilityOutlineRole]
                || [role isEqualToString:NSAccessibilityTableRole]) {
            ++counts.tables;
            // These queries populate Qt Cocoa's native row/column/cell cache;
            // Qt's generic QAccessibleInterface alone does not exercise it.
            NSArray *rows = [node accessibilityRows];
            counts.rows += int(rows.count);
            for (id row in rows)
                visit(row, counts, seen, depth + 1);
            for (id column in [node accessibilityColumns])
                visit(column, counts, seen, depth + 1);
        }
        if ([role isEqualToString:NSAccessibilityCellRole]) {
            ++counts.cells;
            (void)[node accessibilityValue];
            (void)[node accessibilityTitle];
        }
    }
    if ([node respondsToSelector:@selector(accessibilityChildren)])
        for (id child in [node accessibilityChildren])
            visit(child, counts, seen, depth + 1);
}

NativeAXCounts query(QWidget *window)
{
    NativeAXCounts counts;
    std::set<void *> seen;
    NSView *native = reinterpret_cast<NSView *>(window->winId());
    // QNSView's public legacy AX entry point activates the Cocoa bridge. No
    // external AX client, trusted-process setting or security prompt is needed.
    NSArray *children = [native accessibilityAttributeValue:NSAccessibilityChildrenAttribute];
    for (id child in children)
        visit(child, counts, seen, 0);
    return counts;
}
}

NativeAXCounts queryNativeAXThenMutate(QWidget *window, const std::function<void()> &mutate)
{
    NativeAXCounts before;
    @autoreleasepool {
        QAccessible::setActive(true);
        before = query(window);
        mutate();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        // Reacquire current elements: old AX objects may legitimately have
        // become invalid. Never send a message to a stale saved element.
        query(window);
    } // Exercise the array/element destruction path in the supplied crash.
    return before;
}
