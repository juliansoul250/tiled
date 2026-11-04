// Identify the actually loaded native plugin, not an unrelated file on disk.
// GPL-2.0-or-later.
#include <QDebug>
#include <QGuiApplication>
#include <QLibrary>
#include <QMessageBox>
#include <QString>
#include <dlfcn.h>
#include <objc/runtime.h>

bool verifyCocoaAccessibilityFix()
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return true; // Preserve non-GUI/offscreen command-line export use.
    const QString expected = QStringLiteral("6.11.3:QTBUG-149612:8148c48deb8e4345bacb797cb75c011df6f797fa");
    Class element = objc_getClass("QMacAccessibilityElement");
    Method getter = element ? class_getInstanceMethod(element, sel_registerName("accessibilitySelectedChildren")) : nullptr;
    Dl_info image = {};
    bool valid = QString::fromLatin1(qVersion()) == QStringLiteral("6.11.3") && getter
            && dladdr(reinterpret_cast<void *>(method_getImplementation(getter)), &image);
    QString actual;
    if (valid) {
        QLibrary plugin(QString::fromUtf8(image.dli_fname));
        plugin.setLoadHints(QLibrary::PreventUnloadHint);
        using Identity = const char *(*)();
        const auto identity = reinterpret_cast<Identity>(plugin.resolve("tiled_qt_cocoa_fix_identity"));
        if (identity)
            actual = QString::fromLatin1(identity());
        valid = actual == expected;
    }
    if (valid) {
        qInfo().noquote() << "Cocoa accessibility fix:" << actual;
        return true;
    }
    const QString message = QStringLiteral("此应用加载了未修复或不匹配的 Qt 辅助功能组件。请使用本分支的完整新版应用包，不要单独复制 Qt 插件或沿用旧应用目录。");
    qCritical().noquote() << message << "Runtime Qt:" << qVersion() << "Patch:" << actual;
    if (!qEnvironmentVariableIsSet("TILED_WIDGET_ARTIFACTS"))
        QMessageBox::critical(nullptr, QStringLiteral("Tiled 组件版本不匹配"), message);
    return false;
}
