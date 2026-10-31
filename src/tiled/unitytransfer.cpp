/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "unitytransfer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace Tiled { namespace UnityTransfer {
namespace {
const QString packageName = QStringLiteral("com.seanba.super-tiled2unity");
const QString recordName = QStringLiteral(".tiled-import.json");
QString hash(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
QString fail(const QString &message, const QString &path = QString()) { return path.isEmpty() ? message : message + QStringLiteral("\n") + path; }
bool noLinks(QString path) {
    for (;;) {
        QFileInfo info(path);
        if (info.isSymLink()) return false;
        const QString parent = info.absolutePath();
        if (parent == path) return true;
        path = parent;
    }
}
bool readFile(const QString &path, QByteArray &data, QString &error) {
    QFileInfo info(path);
    if (!info.isFile() || !noLinks(path)) { error = fail(QStringLiteral("文件不存在或路径包含符号链接，已停止导入。"), path); return false; }
    if (info.size() > 128 * 1024 * 1024) { error = fail(QStringLiteral("单个文件超过 128 MiB，当前安全导入不支持。"), path); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = fail(QStringLiteral("无法读取文件："), path); return false; }
    data = file.readAll();
    if (file.error() != QFile::NoError) { error = fail(QStringLiteral("读取文件失败："), path); return false; }
    return true;
}
bool tree(const QString &root, QMap<QString, QByteArray> &files, QString &error) {
    if (!QFileInfo(root).isDir() || !noLinks(root)) { error = fail(QStringLiteral("目录无效或包含符号链接："), root); return false; }
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    qint64 total = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        if (it.fileInfo().isSymLink()) { error = fail(QStringLiteral("目录包含符号链接，不能安全覆盖："), path); return false; }
        if (it.fileInfo().isDir()) continue;
        QByteArray data;
        if (!readFile(path, data, error)) return false;
        total += data.size();
        if (total > 512 * 1024 * 1024 || files.size() >= 10000) { error = QStringLiteral("导入目录超过安全大小或文件数量限制。"); return false; }
        files.insert(QDir(root).relativeFilePath(path), data);
    }
    return true;
}
struct Collector {
    Plan &p;
    QMap<QString, QString> names;
    QSet<QString> busy;
    qint64 total = 0;
    QString collect(const QString &path, bool main = false) {
        if (busy.size() >= 64) { p.error = QStringLiteral("地图依赖嵌套超过 64 层，已停止导入。"); return {}; }
        QFileInfo info(path);
        if (!noLinks(path) || !info.isFile()) { p.error = fail(QStringLiteral("依赖文件不存在或包含符号链接："), path); return {}; }
        const QString canonical = info.canonicalFilePath();
        if (busy.contains(canonical)) { p.error = fail(QStringLiteral("发现循环地图/瓦片集/模板引用："), path); return {}; }
        if (names.contains(canonical)) return names.value(canonical);
        QByteArray bytes;
        if (!readFile(canonical, bytes, p.error)) return {};
        total += bytes.size();
        if (total > 512 * 1024 * 1024 || names.size() >= 4096) { p.error = QStringLiteral("地图依赖超过 512 MiB 或 4096 个文件，已停止导入。"); return {}; }
        const QString suffix = info.suffix().toLower();
        const QSet<QString> passive = {QStringLiteral("tmx"), QStringLiteral("tsx"), QStringLiteral("tx"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tga"), QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("psd")};
        if (!passive.contains(suffix)) { p.error = fail(QStringLiteral("依赖类型不在安全白名单中；当前仅支持 TMX/TSX/TX 与常用图片，不导入脚本、插件或其他可执行资源："), path); return {}; }
        if (suffix == QStringLiteral("meta")) { p.error = QStringLiteral("不能将 Unity .meta 文件作为地图依赖导入。"); return {}; }
        if (suffix == QStringLiteral("tmj") || suffix == QStringLiteral("tsj") || suffix == QStringLiteral("tj")) { p.error = QStringLiteral("当前导入仅支持 XML 格式 TMX、TSX、TX；请先转换 JSON 文件。"); return {}; }
        const QString name = main ? QStringLiteral("map/") + info.fileName()
                                  : QStringLiteral("dependencies/") + hash(canonical.toUtf8()).left(24) + QLatin1Char('/') + info.fileName();
        names.insert(canonical, name);
        p.inputs.insert(canonical, bytes);
        if (suffix != QStringLiteral("tmx") && suffix != QStringLiteral("tsx") && suffix != QStringLiteral("tx")) { p.assets.insert(name, bytes); return name; }
        busy.insert(canonical);
        QXmlStreamReader reader(bytes);
        QByteArray rendered;
        QXmlStreamWriter writer(&rendered);
        bool rootSeen = false;
        QStringList elements;
        while (!reader.atEnd() && p.error.isEmpty()) {
            reader.readNext();
            // Invalid tokens must never reach writeCurrentToken (Qt debug asserts).
            if (reader.hasError())
                break;
            if (reader.isDTD() || reader.isEntityReference()) { p.error = QStringLiteral("不支持 DTD 或自定义 XML 实体，已停止导入。"); break; }
            if (!reader.isStartElement()) {
                if (reader.isEndElement() && !elements.isEmpty()) elements.removeLast();
                writer.writeCurrentToken(reader);
                continue;
            }
            const QString tag = reader.name().toString();
            if (!rootSeen) {
                rootSeen = true;
                const QString expected = suffix == QStringLiteral("tmx") ? QStringLiteral("map") : suffix == QStringLiteral("tsx") ? QStringLiteral("tileset") : QStringLiteral("template");
                if (tag != expected || !reader.namespaceUri().isEmpty()) { p.error = QStringLiteral("文件内容与 TMX/TSX/TX 格式不匹配。"); break; }
            }
            if (tag == QStringLiteral("map")) {
                const QString orientation = reader.attributes().value(QStringLiteral("orientation")).toString();
                if (orientation != QStringLiteral("orthogonal") && orientation != QStringLiteral("isometric") && orientation != QStringLiteral("staggered") && orientation != QStringLiteral("hexagonal")) { p.error = QStringLiteral("当前 Unity 导入不支持此地图方向（包括斜投影 oblique）。"); break; }
            }
            if (tag == QStringLiteral("data") && (reader.attributes().value(QStringLiteral("compression")) == QStringLiteral("zstd") || elements.contains(QStringLiteral("image")))) { p.error = QStringLiteral("不支持 Zstandard 压缩或内嵌图片；请改用 CSV/zlib/gzip 与外部图片。"); break; }
            if (tag == QStringLiteral("property") && reader.attributes().value(QStringLiteral("type")) == QStringLiteral("file") && !reader.attributes().hasAttribute(QStringLiteral("value"))) { p.error = QStringLiteral("文件属性必须使用标准 value 属性保存，请用 Tiled 重新保存后导入。"); break; }
            writer.writeStartElement(tag);
            for (const QXmlStreamAttribute &attribute : reader.attributes()) {
                const QString key = attribute.name().toString();
                QString value = attribute.value().toString();
                const bool dependency = (key == QStringLiteral("source") && (tag == QStringLiteral("tileset") || tag == QStringLiteral("image")))
                        || (key == QStringLiteral("template") && tag == QStringLiteral("object"))
                        || (key == QStringLiteral("value") && tag == QStringLiteral("property") && reader.attributes().value(QStringLiteral("type")) == QStringLiteral("file"));
                if (dependency && !value.isEmpty()) {
                    if (QDir::isAbsolutePath(value) || value.contains(QLatin1Char(':')) || value.contains(QLatin1Char('\\')) || value.contains(QLatin1Char('\0'))) { p.error = fail(QStringLiteral("依赖必须是本地相对路径，不能使用网址、绝对路径或反斜杠："), value); break; }
                    if ((tag == QStringLiteral("tileset") && QFileInfo(value).suffix().toLower() != QStringLiteral("tsx")) || (key == QStringLiteral("template") && QFileInfo(value).suffix().toLower() != QStringLiteral("tx"))) { p.error = QStringLiteral("外部瓦片集必须是 TSX，模板必须是 TX（XML）。"); break; }
                    const QString target = collect(QDir(info.absolutePath()).absoluteFilePath(value));
                    if (!p.error.isEmpty()) break;
                    // Both paths must be absolute: QDir::relativeFilePath leaves a
                    // relative target unchanged instead of resolving it from our layout.
                    const QDir layout(p.projectPath);
                    const QString sourceDirectory = QFileInfo(layout.absoluteFilePath(name)).absolutePath();
                    value = QDir(sourceDirectory).relativeFilePath(layout.absoluteFilePath(target));
                }
                writer.writeAttribute(key, value);
            }
            elements.append(tag);
        }
        busy.remove(canonical);
        if (reader.hasError() || !rootSeen) p.error = fail(QStringLiteral("XML 文件损坏或无法解析："), canonical);
        if (!p.error.isEmpty()) return {};
        p.assets.insert(name, rendered);
        return name;
    }
};
bool writeTree(const QString &root, const QMap<QString, QByteArray> &files, QString &error) {
    if (!QDir().mkpath(root)) { error = fail(QStringLiteral("无法创建暂存目录："), root); return false; }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QString path = QDir(root).filePath(it.key());
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error = fail(QStringLiteral("无法创建暂存子目录："), path); return false; }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size() || !file.flush()) { error = fail(QStringLiteral("写入暂存文件失败（请检查空间和权限）："), path); return false; }
    }
    return true;
}
} // namespace

Plan plan(const QString &mapPath, const QString &unityProject, const QString &bundledPackagePath) {
    Plan p;
    if (!noLinks(QFileInfo(mapPath).absoluteFilePath()) || !noLinks(QFileInfo(unityProject).absoluteFilePath()) || !noLinks(QFileInfo(bundledPackagePath).absoluteFilePath())) { p.error = QStringLiteral("所选地图、项目或导入包的路径包含符号链接，请选择真实路径。"); return p; }
    p.sourceMap = QFileInfo(mapPath).canonicalFilePath();
    p.projectPath = QFileInfo(unityProject).canonicalFilePath();
    p.bundledPackagePath = QFileInfo(bundledPackagePath).canonicalFilePath();
    if (p.sourceMap.isEmpty() || QFileInfo(mapPath).suffix().compare(QStringLiteral("tmx"), Qt::CaseInsensitive) != 0 || QFileInfo(mapPath).isSymLink()) { p.error = QStringLiteral("请先把当前地图保存为 TMX 文件；JSON/未保存地图暂不支持直接导入。"); return p; }
    if (p.projectPath.isEmpty() || QFileInfo(unityProject).isSymLink() || p.bundledPackagePath.isEmpty()) { p.error = QStringLiteral("Unity 项目或内置导入包目录不存在。"); return p; }
    for (const QString &directory : {QStringLiteral("Assets"), QStringLiteral("Packages"), QStringLiteral("ProjectSettings")}) {
        const QString path = QDir(p.projectPath).filePath(directory);
        if (!QFileInfo(path).isDir() || !noLinks(path)) { p.error = fail(QStringLiteral("请选择完整且不含符号链接的 Unity 项目："), path); return p; }
    }
    if (QFileInfo::exists(QDir(p.projectPath).filePath(QStringLiteral("Temp/UnityLockfile")))) { p.error = QStringLiteral("请先关闭此 Unity 项目的编辑器，导入完成后再打开，避免导入过程被同时修改。"); return p; }
    QByteArray version, manifest;
    const QString versionPath = QDir(p.projectPath).filePath(QStringLiteral("ProjectSettings/ProjectVersion.txt"));
    const QString manifestPath = QDir(p.projectPath).filePath(QStringLiteral("Packages/manifest.json"));
    if (!readFile(versionPath, version, p.error) || !readFile(manifestPath, manifest, p.error)) return p;
    if (!QRegularExpression(QStringLiteral("(?:^|\\n)m_EditorVersion: 2022\\.3\\.[^\\r\\n]+")).match(QString::fromUtf8(version)).hasMatch()) { p.error = QStringLiteral("当前仅支持识别 Unity 2022.3 项目；Unity 侧编译和生成仍需在编辑器中确认。"); return p; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifest, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(QStringLiteral("dependencies")).isObject()) { p.error = QStringLiteral("Unity Packages/manifest.json 无效；不会修改项目包清单。"); return p; }
    if (document.object().value(QStringLiteral("dependencies")).toObject().contains(packageName)) { p.error = QStringLiteral("项目清单已声明 SuperTiled2Unity，请先人工确认现有安装；不会覆盖或改写包配置。"); return p; }
    p.inputs.insert(versionPath, version); p.inputs.insert(manifestPath, manifest);
    if (!tree(p.bundledPackagePath, p.packageFiles, p.error)) return p;
    const QString provenancePath = QDir(p.bundledPackagePath).absoluteFilePath(QStringLiteral("../SOURCE.json"));
    QByteArray provenanceBytes;
    if (!readFile(provenancePath, provenanceBytes, p.error)) return p;
    const auto provenance = QJsonDocument::fromJson(provenanceBytes).object();
    const auto expectedFiles = provenance.value(QStringLiteral("files")).toObject();
    if (provenance.value(QStringLiteral("commit")).toString() != QStringLiteral("d3deae415963807caef14c522ed8be9bc30e89e0") || provenance.value(QStringLiteral("version")).toString() != QStringLiteral("2.4.0") || expectedFiles.size() != p.packageFiles.size()) {
        p.error = QStringLiteral("内置 Unity 导入器来源记录不匹配，不能安全安装。"); return p;
    }
    for (auto it = p.packageFiles.cbegin(); it != p.packageFiles.cend(); ++it) {
        if (expectedFiles.value(it.key()).toString() != hash(it.value())) { p.error = fail(QStringLiteral("内置 Unity 导入器文件校验失败："), it.key()); return p; }
    }
    p.inputs.insert(provenancePath, provenanceBytes);
    const auto package = QJsonDocument::fromJson(p.packageFiles.value(QStringLiteral("package.json"))).object();
    if (package.value(QStringLiteral("name")).toString() != packageName || package.value(QStringLiteral("version")).toString() != QStringLiteral("2.4.0")) { p.error = QStringLiteral("内置 SuperTiled2Unity 包身份错误，要求官方 2.4.0。"); return p; }
    p.packagePath = QDir(p.projectPath).filePath(QStringLiteral("Packages/") + packageName);
    p.installPackage = !QFileInfo::exists(p.packagePath);
    if (!p.installPackage) {
        QMap<QString, QByteArray> existing;
        if (!tree(p.packagePath, existing, p.error)) return p;
        if (existing != p.packageFiles) { p.error = QStringLiteral("项目已有不同或已修改的 SuperTiled2Unity 包；为保护项目，已取消导入。"); return p; }
    }
    const QDir packages(QDir(p.projectPath).filePath(QStringLiteral("Packages")));
    for (const QString &directory : packages.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (directory == packageName) continue;
        const QString otherManifest = packages.filePath(directory + QStringLiteral("/package.json"));
        if (!QFileInfo::exists(otherManifest)) continue;
        QByteArray contents;
        if (!readFile(otherManifest, contents, p.error)) return p;
        if (QJsonDocument::fromJson(contents).object().value(QStringLiteral("name")).toString() == packageName) {
            p.error = fail(QStringLiteral("其他目录已包含 SuperTiled2Unity 包，不能重复安装："), otherManifest); return p;
        }
    }
    QDirIterator legacy(QDir(p.projectPath).filePath(QStringLiteral("Assets")), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int inspected = 0;
    while (legacy.hasNext()) {
        const QString path = legacy.next();
        if (++inspected > 100000) { p.error = QStringLiteral("项目资源过多，无法完成旧导入器冲突检查；请人工整理后重试。"); return p; }
        const QString name = legacy.fileInfo().fileName();
        if (name.compare(QStringLiteral("SuperTiled2Unity"), Qt::CaseInsensitive) == 0 || name.compare(QStringLiteral("SuperTiledImporter.cs"), Qt::CaseInsensitive) == 0 || name.compare(QStringLiteral("SuperMap.cs"), Qt::CaseInsensitive) == 0) {
            p.error = fail(QStringLiteral("Assets 中可能已安装旧 SuperTiled2Unity，不能重复安装；请先人工确认："), path); return p;
        }
    }
    Collector collector{p, {}, {}, 0};
    const QString main = collector.collect(p.sourceMap, true);
    if (!p.error.isEmpty()) return p;
    const QString imports = QDir(p.projectPath).filePath(QStringLiteral("Assets/TiledImports"));
    if (QFileInfo::exists(imports) && (!QFileInfo(imports).isDir() || !noLinks(imports))) { p.error = QStringLiteral("Assets/TiledImports 不是安全目录。"); return p; }
    p.destinationPath = QDir(imports).filePath(hash(p.sourceMap.toUtf8()).left(24));
    p.destinationMapPath = QDir(p.destinationPath).filePath(main);
    if (QFileInfo::exists(p.destinationPath)) {
        if (!tree(p.destinationPath, p.previousAssets, p.error)) return p;
        const auto record = QJsonDocument::fromJson(p.previousAssets.value(recordName)).object();
        if (record.value(QStringLiteral("source")).toString() != p.sourceMap || !record.value(QStringLiteral("files")).isObject()) { p.error = QStringLiteral("目标目录不属于此地图的受控导入，不能覆盖。"); return p; }
        const auto hashes = record.value(QStringLiteral("files")).toObject();
        for (auto it = hashes.begin(); it != hashes.end(); ++it) {
            if (!p.previousAssets.contains(it.key()) || hash(p.previousAssets.value(it.key())) != it.value().toString()) { p.error = fail(QStringLiteral("Unity 目标文件被修改或删除，已停止覆盖："), it.key()); return p; }
        }
        for (auto it = p.previousAssets.cbegin(); it != p.previousAssets.cend(); ++it) {
            if (it.key() == recordName || hashes.contains(it.key())) continue;
            if (!it.key().endsWith(QStringLiteral(".meta"))) { p.error = fail(QStringLiteral("受控目录出现额外用户文件，已停止覆盖："), it.key()); return p; }
            const QString target = it.key().left(it.key().size() - 5);
            bool retained = target == recordName || p.assets.contains(target);
            for (auto asset = p.assets.cbegin(); !retained && asset != p.assets.cend(); ++asset) retained = asset.key().startsWith(target + QLatin1Char('/'));
            bool previouslyOwned = target == recordName || hashes.contains(target);
            for (auto old = hashes.begin(); !previouslyOwned && old != hashes.end(); ++old) previouslyOwned = old.key().startsWith(target + QLatin1Char('/'));
            if (!previouslyOwned) { p.error = fail(QStringLiteral("发现来源不明的 Unity meta 文件，已停止覆盖："), it.key()); return p; }
            if (retained) p.assets.insert(it.key(), it.value());
        }
    }
    QJsonObject hashes;
    for (auto it = p.assets.cbegin(); it != p.assets.cend(); ++it) if (!it.key().endsWith(QStringLiteral(".meta"))) hashes.insert(it.key(), hash(it.value()));
    QJsonObject record; record.insert(QStringLiteral("source"), p.sourceMap); record.insert(QStringLiteral("files"), hashes);
    p.assets.insert(recordName, QJsonDocument(record).toJson());
    p.preview.append(QStringLiteral("Unity 项目：") + p.projectPath);
    p.preview.append(p.installPackage ? QStringLiteral("安装内置官方 SuperTiled2Unity 2.4.0（保留 MIT 许可证）；不修改 manifest.json。") : QStringLiteral("复用内容完全匹配的 SuperTiled2Unity 2.4.0。"));
    if (p.installPackage) {
        p.preview.append(QStringLiteral("新增导入器文件（%1 项）：").arg(p.packageFiles.size()));
        for (auto it = p.packageFiles.cbegin(); it != p.packageFiles.cend(); ++it)
            p.preview.append(QStringLiteral("新增：Packages/") + packageName + QLatin1Char('/') + it.key());
    }
    p.preview.append(QStringLiteral("受控地图目录：") + p.destinationPath);
    for (auto it = p.assets.cbegin(); it != p.assets.cend(); ++it) p.preview.append((p.previousAssets.contains(it.key()) ? QStringLiteral("更新：") : QStringLiteral("新增：")) + it.key());
    for (auto it = p.previousAssets.cbegin(); it != p.previousAssets.cend(); ++it) if (!p.assets.contains(it.key())) p.preview.append(QStringLiteral("移除旧依赖：") + it.key());
    return p;
}

QString apply(const Plan &requested, int simulateFailurePhase) {
    if (!requested.error.isEmpty()) return requested.error;
    if (requested.sourceMap.isEmpty() || requested.projectPath.isEmpty() || requested.bundledPackagePath.isEmpty()) return QStringLiteral("请先生成导入预览。");
    QLockFile lock(QDir(requested.projectPath).filePath(QStringLiteral(".tiled-unity-import.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return QStringLiteral("另一个 Tiled 导入正在使用此项目，或项目目录不可写。");
    const Plan current = plan(requested.sourceMap, requested.projectPath, requested.bundledPackagePath);
    if (!current.error.isEmpty()) return current.error;
    if (current.inputs != requested.inputs || current.assets != requested.assets || current.previousAssets != requested.previousAssets || current.packageFiles != requested.packageFiles || current.installPackage != requested.installPackage || current.destinationPath != requested.destinationPath || current.packagePath != requested.packagePath)
        return QStringLiteral("预览后源文件或项目发生变化，请重新预览并确认。");
    QTemporaryDir staging(QDir(current.projectPath).filePath(QStringLiteral(".tiled-transfer-XXXXXX")));
    if (!staging.isValid()) return QStringLiteral("无法创建项目内的事务暂存目录，请检查磁盘空间和权限。");
    QString error;
    const QString stagedAssets = QDir(staging.path()).filePath(QStringLiteral("assets"));
    const QString stagedPackage = QDir(staging.path()).filePath(QStringLiteral("package"));
    if (!writeTree(stagedAssets, current.assets, error) || (current.installPackage && !writeTree(stagedPackage, current.packageFiles, error))) return error;
    const Plan checked = plan(current.sourceMap, current.projectPath, current.bundledPackagePath);
    if (!checked.error.isEmpty() || checked.inputs != current.inputs || checked.previousAssets != current.previousAssets || checked.packageFiles != current.packageFiles || checked.installPackage != current.installPackage || checked.destinationPath != current.destinationPath)
        return QStringLiteral("暂存期间文件或目录发生变化，已取消导入；请重新预览。 ");
    const QString imports = QFileInfo(current.destinationPath).absolutePath();
    const bool madeImports = !QFileInfo::exists(imports);
    if (!QDir().mkpath(imports) || !noLinks(imports)) return QStringLiteral("无法创建安全的 Assets/TiledImports 目录。");
    if (!noLinks(current.packagePath) || !noLinks(current.destinationPath) || !noLinks(current.projectPath)) {
        if (madeImports) QDir().rmdir(imports);
        return QStringLiteral("提交前目录路径发生符号链接变化，已停止导入。");
    }
    bool installedPackage = false;
    if (current.installPackage) {
        if (!QDir().rename(stagedPackage, current.packagePath)) { if (madeImports) QDir().rmdir(imports); return QStringLiteral("安装导入包失败，地图未改变。"); }
        installedPackage = true;
    }
    const QString backup = QDir(staging.path()).filePath(QStringLiteral("previous-assets"));
    bool backedUp = false;
    if (simulateFailurePhase != 1 && !current.previousAssets.isEmpty()) backedUp = QDir().rename(current.destinationPath, backup);
    const bool backupOk = current.previousAssets.isEmpty() || backedUp;
    const bool assetsInstalled = simulateFailurePhase != 1 && backupOk && QDir().rename(stagedAssets, current.destinationPath);
    if (!assetsInstalled || simulateFailurePhase == 2) {
        if (assetsInstalled && !QDir().rename(current.destinationPath, stagedAssets)) { staging.setAutoRemove(false); return fail(QStringLiteral("回滚未完成，保留恢复目录："), staging.path()); }
        bool restored = true;
        if (backedUp) restored = QDir().rename(backup, current.destinationPath);
        if (installedPackage) {
            QMap<QString, QByteArray> installed; QString inspectError;
            if (tree(current.packagePath, installed, inspectError) && installed == current.packageFiles)
                restored = QDir(current.packagePath).removeRecursively() && restored;
            else
                restored = false; // Never delete files changed by another process.
        }
        if (madeImports) QDir().rmdir(imports);
        if (!restored) { staging.setAutoRemove(false); return fail(QStringLiteral("提交失败且自动恢复未完成。请勿继续编辑；保留此恢复目录并联系维护人员："), staging.path()); }
        return QStringLiteral("导入提交失败，已恢复原有文件；请检查空间、权限或重新尝试。");
    }
    return {};
}
} }
