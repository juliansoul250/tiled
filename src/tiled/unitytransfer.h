/* SPDX-License-Identifier: GPL-2.0-or-later
 * Safe, preview-first transfer of saved Tiled maps to Unity projects.
 */
#pragma once
#include "tilededitor_global.h"
#include <QByteArray>
#include <QMap>
#include <QStringList>

namespace Tiled { namespace UnityTransfer {
struct Plan {
    QString error;
    QString sourceMap;
    QString projectPath;
    QString bundledPackagePath;
    QString destinationPath;
    QString destinationMapPath;
    QString packagePath;
    QStringList preview;
    bool installPackage = false;
    // Immutable preview snapshots, checked again immediately before committing.
    QMap<QString, QByteArray> inputs;
    QMap<QString, QByteArray> assets;
    QMap<QString, QByteArray> packageFiles;
    QMap<QString, QByteArray> previousAssets;
};
TILED_EDITOR_EXPORT Plan plan(const QString &mapPath, const QString &unityProject,
                             const QString &bundledPackagePath);
// Empty string means success. The optional fault point is for transaction tests.
TILED_EDITOR_EXPORT QString apply(const Plan &plan, int simulateFailurePhase = 0);
} }
