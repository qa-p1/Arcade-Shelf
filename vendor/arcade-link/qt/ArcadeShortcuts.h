// App shortcut documents and catalog sheets (SPEC §8.5). Unknown fields
// ignored.
#pragma once
#include <QJsonObject>
#include <QStringList>

namespace ArcadeLink::Shortcuts {
bool identifier(const QString &id);
bool validate(const QJsonObject &document, QString *error = nullptr,
              const QJsonObject *manifest = nullptr);
QStringList effective(const QJsonObject &shortcut, const QString &os);
QString markdown(const QJsonObject &document, QString *error = nullptr);
bool sheetMatches(const QJsonObject &sheet, const QString &os,
                  const QString &windowIdentity);
} // namespace ArcadeLink::Shortcuts
