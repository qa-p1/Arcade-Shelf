#include "ArcadeShortcuts.h"
#include "ArcadeAccelerator.h"
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace ArcadeLink::Shortcuts {
static const QStringList systems{QStringLiteral("linux"),
                                 QStringLiteral("windows"),
                                 QStringLiteral("macos")};
static bool matches(const QString &value, const QString &pattern) {
  return QRegularExpression(QStringLiteral("\\A(?:") + pattern +
                            QStringLiteral(")\\z"))
      .match(value)
      .hasMatch();
}
bool identifier(const QString &id) {
  return matches(id, QStringLiteral("[a-z0-9][a-z0-9.-]*"));
}
static bool text(const QJsonValue &value, qsizetype limit = -1) {
  return value.isString() && !value.toString().trimmed().isEmpty() &&
         (limit < 0 || value.toString().toUcs4().size() <= limit);
}
static bool bindings(const QJsonValue &value, QStringList *out) {
  if (value.isNull() || value.isUndefined())
    return true;
  if (value.isString()) {
    *out << value.toString();
    return true;
  }
  if (!value.isArray() || value.toArray().isEmpty())
    return false;
  for (const auto &key : value.toArray()) {
    if (!key.isString())
      return false;
    *out << key.toString();
  }
  return true;
}
QStringList effective(const QJsonObject &shortcut, const QString &os) {
  const auto keys = shortcut.value(QStringLiteral("keys")).toObject();
  QStringList result;
  bindings(keys.value(keys.contains(os) ? os : QStringLiteral("default")),
           &result);
  return result;
}
bool validate(const QJsonObject &doc, QString *error,
              const QJsonObject *manifest) {
  if (error)
    error->clear();
  const auto fail = [&](const QString &message) {
    if (error)
      *error = message;
    return false;
  };
  if (!doc.value(QStringLiteral("schema")).isDouble() ||
      doc.value(QStringLiteral("schema")).toDouble() != 1)
    return fail(QStringLiteral("schema must be 1"));
  const bool sheet = !doc.contains(QStringLiteral("app"));
  if (sheet) {
    if (!matches(doc.value(QStringLiteral("id")).toString(),
                 QStringLiteral("[a-z0-9]+(?:-[a-z0-9]+)*")) ||
        !text(doc.value(QStringLiteral("name"))) ||
        !text(doc.value(QStringLiteral("checkedVersion"))))
      return fail(QStringLiteral("sheet needs id, name and checkedVersion"));
    const auto sources = doc.value(QStringLiteral("sources"));
    if (!sources.isArray() || sources.toArray().isEmpty())
      return fail(QStringLiteral("sources need https URLs"));
    for (const auto &source : sources.toArray())
      if (!source.isString() ||
          !matches(source.toString(),
                   QStringLiteral("https://[^/\\s]+[^\\s]*")))
        return fail(QStringLiteral("sources need https URLs"));
    if (!doc.value(QStringLiteral("match")).isObject())
      return fail(QStringLiteral("match needs an OS"));
    const auto match = doc.value(QStringLiteral("match")).toObject();
    bool any = false;
    const QStringList fields{QStringLiteral("class"), QStringLiteral("exe"),
                             QStringLiteral("bundle")};
    for (int i = 0; i < systems.size(); ++i) {
      if (!match.contains(systems[i]))
        continue;
      any = true;
      if (!match.value(systems[i]).isObject())
        return fail(QStringLiteral("invalid OS match"));
      const auto names = match.value(systems[i]).toObject().value(fields[i]);
      if (!names.isArray() || names.toArray().isEmpty())
        return fail(QStringLiteral("match needs names"));
      for (const auto &name : names.toArray())
        if (!text(name))
          return fail(QStringLiteral("match names must be nonempty strings"));
    }
    if (!any)
      return fail(QStringLiteral("match needs at least one OS"));
  } else {
    if (!matches(doc.value(QStringLiteral("app")).toString(),
                 QStringLiteral("arcade\\.[a-z0-9][a-z0-9.-]*")) ||
        !text(doc.value(QStringLiteral("version"))))
      return fail(QStringLiteral("invalid app or version"));
    if (manifest && manifest->value(QStringLiteral("id")) !=
                        doc.value(QStringLiteral("app")))
      return fail(QStringLiteral("manifest belongs to another app"));
  }
  if (!doc.value(QStringLiteral("groups")).isArray())
    return fail(QStringLiteral("groups must be an array"));
  QSet<QString> ids;
  QHash<QString, QList<QPair<QString, QString>>> used;
  for (const auto &groupValue : doc.value(QStringLiteral("groups")).toArray()) {
    if (!groupValue.isObject())
      return fail(QStringLiteral("invalid group"));
    const auto group = groupValue.toObject();
    const auto context = group.value(QStringLiteral("context")).toString();
    if (!text(group.value(QStringLiteral("title"))) ||
        !matches(context, QStringLiteral("[a-z0-9]+(?:-[a-z0-9]+)*")))
      return fail(QStringLiteral("invalid group title or context"));
    if (sheet && context == u"global" &&
        doc.value(QStringLiteral("id")).toString() != u"windows-system" &&
        doc.value(QStringLiteral("id")).toString() != u"macos-system")
      return fail(QStringLiteral(
          "sheet global context is reserved for system sheets; use app"));
    if (!group.value(QStringLiteral("shortcuts")).isArray())
      return fail(QStringLiteral("shortcuts must be an array"));
    for (const auto &shortcutValue :
         group.value(QStringLiteral("shortcuts")).toArray()) {
      if (!shortcutValue.isObject())
        return fail(QStringLiteral("invalid shortcut"));
      const auto shortcut = shortcutValue.toObject();
      const auto id = shortcut.value(QStringLiteral("id")).toString();
      if (!identifier(id) || ids.contains(id))
        return fail(QStringLiteral("invalid or duplicate shortcut id"));
      ids.insert(id);
      if (!text(shortcut.value(QStringLiteral("title")), 80))
        return fail(QStringLiteral("title must have 1–80 characters"));
      const auto description = shortcut.value(QStringLiteral("description"));
      if (!description.isNull() && !description.isUndefined() &&
          !description.isString())
        return fail(QStringLiteral("description must be a string"));
      if (shortcut.contains(QStringLiteral("rebindable"))) {
        const auto rebindable = shortcut.value(QStringLiteral("rebindable"));
        if (!rebindable.isBool() || sheet || context != u"global")
          return fail(QStringLiteral(
              "rebindable is only allowed in app global groups"));
        if (rebindable.toBool() && manifest) {
          bool found = false;
          for (const auto &entry :
               manifest->value(QStringLiteral("shortcuts")).toArray())
            found |=
                entry.toObject().value(QStringLiteral("id")).toString() == id;
          if (!found)
            return fail(QStringLiteral("rebindable id absent from manifest"));
        }
      }
      if (!shortcut.value(QStringLiteral("keys")).isObject())
        return fail(QStringLiteral("keys must be an object"));
      const auto keys = shortcut.value(QStringLiteral("keys")).toObject();
      bool nonnull = false;
      for (const auto &os : QStringList{QStringLiteral("default")} + systems) {
        if (!keys.contains(os))
          continue;
        QStringList values;
        if (!bindings(keys.value(os), &values))
          return fail(QStringLiteral("invalid keys value"));
        for (qsizetype index = 0; index < values.size(); ++index) {
          const auto &key = values[index];
          nonnull = true;
          if (!Accelerator::isCanonical(key))
            return fail(QStringLiteral("noncanonical key: %1").arg(key));
          for (qsizetype previous = 0; previous < index; ++previous)
            if (Accelerator::conflicts(key, values[previous]))
              return fail(QStringLiteral("conflicting alternatives"));
        }
      }
      if (!nonnull)
        return fail(QStringLiteral("keys need at least one non-null binding"));
      for (const auto &os : systems) {
        auto &seen = used[context + QLatin1Char('/') + os];
        for (const auto &key : effective(shortcut, os)) {
          for (const auto &other : std::as_const(seen))
            if (Accelerator::conflicts(key, other.second))
              return fail(QStringLiteral("%1: %2 conflict with %3 in %4")
                              .arg(id, os, other.first, context));
          seen << qMakePair(id, key);
        }
      }
    }
  }
  return true;
}
static QString escape(QString value) {
  return value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"))
      .replace(QStringLiteral("|"), QStringLiteral("\\|"))
      .replace(QLatin1Char('\r'), QLatin1Char(' '))
      .replace(QLatin1Char('\n'), QLatin1Char(' '));
}
QString markdown(const QJsonObject &doc, QString *error) {
  if (!validate(doc, error))
    return {};
  QString result;
  for (const auto &groupValue : doc.value(QStringLiteral("groups")).toArray()) {
    const auto group = groupValue.toObject();
    result += QStringLiteral("## %1\n\n| Action | Linux | Windows | macOS |\n| "
                             "--- | --- | --- | --- |\n")
                  .arg(escape(group.value(QStringLiteral("title")).toString()));
    for (const auto &shortcutValue :
         group.value(QStringLiteral("shortcuts")).toArray()) {
      const auto shortcut = shortcutValue.toObject();
      QStringList cells{
          escape(shortcut.value(QStringLiteral("title")).toString())};
      for (int i = 0; i < systems.size(); ++i) {
        QStringList values;
        for (const auto &key : effective(shortcut, systems[i]))
          values << escape(
              Accelerator::display(key, static_cast<Accelerator::Platform>(i)));
        cells << (values.isEmpty() ? QStringLiteral("—")
                                   : values.join(QStringLiteral("<br>")));
      }
      result += QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) +
                QStringLiteral(" |\n");
    }
    result += QLatin1Char('\n');
  }
  return result;
}
bool sheetMatches(const QJsonObject &sheet, const QString &os,
                  const QString &identity) {
  const auto i = systems.indexOf(os);
  if (i < 0)
    return false;
  const QStringList fields{QStringLiteral("class"), QStringLiteral("exe"),
                           QStringLiteral("bundle")};
  for (const auto &name : sheet.value(QStringLiteral("match"))
                              .toObject()
                              .value(os)
                              .toObject()
                              .value(fields[i])
                              .toArray())
    if (name.toString().compare(identity, Qt::CaseInsensitive) == 0)
      return true;
  return false;
}
} // namespace ArcadeLink::Shortcuts
