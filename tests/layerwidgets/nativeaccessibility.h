// Native Cocoa accessibility-cache regression support. GPL-2.0-or-later.
#pragma once
#include <functional>
class QWidget;
struct NativeAXCounts { int tables = 0; int rows = 0; int cells = 0; };
NativeAXCounts queryNativeAXThenMutate(QWidget *, const std::function<void()> &);
