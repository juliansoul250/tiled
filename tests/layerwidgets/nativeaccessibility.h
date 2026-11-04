// Native Cocoa accessibility-cache regression support. GPL-2.0-or-later.
#pragma once
#include <functional>
#include <QStringList>
class QWidget;
struct NativeAXCounts { int tables = 0; int rows = 0; int cells = 0; };
NativeAXCounts queryNativeAXThenMutate(QWidget *, const std::function<void()> &);

// Reacquire the native table and ask the public selection getter.
QStringList queryNativeAXSelectedTitles(QWidget *);
