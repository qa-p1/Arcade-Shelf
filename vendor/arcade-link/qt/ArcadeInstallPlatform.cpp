#include "ArcadeInstall.h"
#include "ArcadeShortcuts.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ArcadeLink {
static bool fail(QString *error, const QString &message) {
  if (error)
    *error = message;
  return false;
}
#ifdef Q_OS_WIN
struct RegistryKey {
  HKEY key = nullptr;
  ~RegistryKey() {
    if (key)
      RegCloseKey(key);
  }
  bool open(const ArcadeInstall::Environment &e, QString *error) {
    const auto code = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        reinterpret_cast<LPCWSTR>(e.windowsEnvironmentKey.utf16()), 0, nullptr,
        0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    return code == ERROR_SUCCESS ||
           fail(error,
                QStringLiteral("cannot open user Path key (%1)").arg(code));
  }
  bool read(QString *value, DWORD *kind, QString *error) {
    DWORD size = 0;
    *kind = REG_EXPAND_SZ;
    auto code = RegQueryValueExW(key, L"Path", nullptr, kind, nullptr, &size);
    if (code == ERROR_FILE_NOT_FOUND) {
      // The API may clear lpType even on failure. A new Path must be a
      // string, or the next shim installation cannot read its own value.
      *kind = REG_EXPAND_SZ;
      value->clear();
      return true;
    }
    if (code != ERROR_SUCCESS)
      return fail(error,
                  QStringLiteral("cannot read user Path (%1)").arg(code));
    if ((*kind != REG_SZ && *kind != REG_EXPAND_SZ) || size > 16 * 1024 * 1024)
      return fail(error, QStringLiteral("user Path is not a string"));
    QList<wchar_t> bytes(qsizetype(size / sizeof(wchar_t)) + 1, wchar_t(0));
    code = RegQueryValueExW(key, L"Path", nullptr, kind,
                            reinterpret_cast<BYTE *>(bytes.data()), &size);
    if (code != ERROR_SUCCESS)
      return fail(error,
                  QStringLiteral("cannot read user Path (%1)").arg(code));
    *value = QString::fromWCharArray(bytes.data());
    return true;
  }
  bool write(const QString &value, DWORD kind, bool broadcast, QString *error) {
    const auto code = RegSetValueExW(
        key, L"Path", 0, kind, reinterpret_cast<const BYTE *>(value.utf16()),
        DWORD((value.size() + 1) * sizeof(wchar_t)));
    if (code != ERROR_SUCCESS)
      return fail(error,
                  QStringLiteral("cannot write user Path (%1)").arg(code));
    if (broadcast)
      SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                          reinterpret_cast<LPARAM>(L"Environment"),
                          SMTO_ABORTIFHUNG, 5000, nullptr);
    return true;
  }
};
static bool samePath(QString entry, const QString &bin) {
  entry = entry.trimmed();
  if (entry.startsWith(QLatin1Char('"')) && entry.endsWith(QLatin1Char('"')))
    entry = entry.mid(1, entry.size() - 2);
  const auto size = ExpandEnvironmentStringsW(
      reinterpret_cast<LPCWSTR>(entry.utf16()), nullptr, 0);
  if (size == 0)
    return false;
  QList<wchar_t> bytes(qsizetype(size), wchar_t(0));
  const auto written = ExpandEnvironmentStringsW(
      reinterpret_cast<LPCWSTR>(entry.utf16()), bytes.data(), size);
  if (written == 0 || written > size)
    return false;
  const auto clean = [](QString path) {
    path = QDir::fromNativeSeparators(path);
    while (path.endsWith(QLatin1Char('/')))
      path.chop(1);
    return path;
  };
  return clean(QString::fromWCharArray(bytes.data()))
             .compare(clean(bin), Qt::CaseInsensitive) == 0;
}
#endif
bool ArcadeInstall::addWindowsPath(const QString &bin, QString *error) const {
  if (!m_environment.validate(error))
    return false;
#ifdef Q_OS_WIN
  if (error)
    error->clear();
  if (!QDir::isAbsolutePath(bin))
    return fail(error, QStringLiteral("bin path must be absolute"));
  RegistryKey key;
  if (!key.open(m_environment, error))
    return false;
  QString path;
  DWORD kind;
  if (!key.read(&path, &kind, error))
    return false;
  for (const auto &entry : path.split(QLatin1Char(';')))
    if (samePath(entry, bin))
      return true;
  while (path.endsWith(QLatin1Char(';')))
    path.chop(1);
  if (!path.isEmpty())
    path += QLatin1Char(';');
  path += QDir::toNativeSeparators(bin);
  return key.write(
      path, kind, m_environment.windowsEnvironmentKey == u"Environment", error);
#else
  Q_UNUSED(bin);
  return fail(error, QStringLiteral(
                         "Windows PATH integration is unavailable on this OS"));
#endif
}
bool ArcadeInstall::removeWindowsPathIfLast(const QString &bin,
                                            QString *error) const {
  if (!m_environment.validate(error))
    return false;
#ifdef Q_OS_WIN
  if (error)
    error->clear();
  if (!QDir::isAbsolutePath(bin))
    return fail(error, QStringLiteral("bin path must be absolute"));
  if (!QDir(bin).entryList({QStringLiteral("*.cmd")}, QDir::Files).isEmpty())
    return true;
  RegistryKey key;
  if (!key.open(m_environment, error))
    return false;
  QString path;
  DWORD kind;
  if (!key.read(&path, &kind, error))
    return false;
  QStringList entries;
  for (const auto &entry : path.split(QLatin1Char(';')))
    if (!samePath(entry, bin))
      entries << entry;
  const auto updated = entries.join(QLatin1Char(';'));
  return updated == path ||
         key.write(updated, kind,
                   m_environment.windowsEnvironmentKey == u"Environment",
                   error);
#else
  Q_UNUSED(bin);
  return fail(error, QStringLiteral(
                         "Windows PATH integration is unavailable on this OS"));
#endif
}
bool ArcadeInstall::writeWindowsShim(const QString &name, const QString &exe,
                                     QString *out, QString *error) const {
  if (!m_environment.validate(error))
    return false;
#ifdef Q_OS_WIN
  if (error)
    error->clear();
  if (!Shortcuts::identifier(name) || !QDir::isAbsolutePath(exe) ||
      exe.contains(QLatin1Char('"')) || exe.contains(QLatin1Char('\n')) ||
      exe.contains(QLatin1Char('\r')))
    return fail(error, QStringLiteral("invalid shim name or executable"));
  if (!QDir().mkpath(m_environment.bin))
    return fail(error, QStringLiteral("cannot create shim folder"));
  const auto path =
      QDir(m_environment.bin).filePath(name + QStringLiteral(".cmd"));
  const auto contents =
      QStringLiteral("@echo off\r\n\"%1\" %*\r\n")
          .arg(QString(QDir::toNativeSeparators(exe))
                   .replace(QStringLiteral("%"), QStringLiteral("%%")))
          .toUtf8();
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(contents) != contents.size() || !file.commit())
    return fail(error, file.errorString());
  if (!addWindowsPath(m_environment.bin, error))
    return false;
  if (out)
    *out = path;
  return true;
#else
  Q_UNUSED(name);
  Q_UNUSED(exe);
  Q_UNUSED(out);
  return fail(error,
              QStringLiteral("Windows shims are unavailable on this OS"));
#endif
}
bool ArcadeInstall::removeWindowsShim(const QString &name,
                                      QString *error) const {
  if (!m_environment.validate(error))
    return false;
#ifdef Q_OS_WIN
  if (!Shortcuts::identifier(name))
    return fail(error, QStringLiteral("invalid shim name"));
  const auto path =
      QDir(m_environment.bin).filePath(name + QStringLiteral(".cmd"));
  if (QFileInfo::exists(path) && !QFile::remove(path))
    return fail(error, QStringLiteral("cannot remove shim"));
  return removeWindowsPathIfLast(m_environment.bin, error);
#else
  Q_UNUSED(name);
  return fail(error,
              QStringLiteral("Windows shims are unavailable on this OS"));
#endif
}
bool ArcadeInstall::isTranslocated(const QString &path) {
  return QDir::fromNativeSeparators(path)
      .split(QLatin1Char('/'))
      .contains(QStringLiteral("AppTranslocation"));
}
QString ArcadeInstall::bundleFor(const QString &executable) {
  QString path = QDir::cleanPath(executable);
  while (!path.isEmpty()) {
    if (QFileInfo(path).suffix() == u"app")
      return path;
    const auto parent = QFileInfo(path).absolutePath();
    if (parent == path)
      break;
    path = parent;
  }
  return {};
}
bool ArcadeInstall::inApplications(const QString &bundle) const {
  const auto root =
      QDir(m_environment.home).filePath(QStringLiteral("Applications"));
  return bundle == root || bundle.startsWith(root + QLatin1Char('/')) ||
         bundle == u"/Applications" || bundle.startsWith(u"/Applications/");
}
bool ArcadeInstall::moveToApplications(const QString &bundle,
                                       const QString &destination, QString *out,
                                       QString *error) const {
  if (!m_environment.validate(error))
    return false;
#ifdef Q_OS_MACOS
  if (error)
    error->clear();
  if (!QDir::isAbsolutePath(bundle))
    return fail(error, QStringLiteral("bundle must be an absolute path"));
  if (!bundle.endsWith(u".app") || !QFileInfo(bundle).isDir())
    return fail(error, QStringLiteral("expected an app bundle"));
  const auto root =
      destination.isEmpty()
          ? QDir(m_environment.home).filePath(QStringLiteral("Applications"))
          : destination;
  if (!QDir::isAbsolutePath(root))
    return fail(error, QStringLiteral("Applications path must be absolute"));
  if (!QDir().mkpath(root))
    return fail(error, QStringLiteral("cannot create Applications"));
  const auto target = QDir(root).filePath(QFileInfo(bundle).fileName());
  if (QFileInfo::exists(target))
    return fail(error, QStringLiteral("bundle already exists"));
  // ditto retains metadata and quarantine. Never call xattr to clear it.
  QProcess copy;
  copy.start(QStringLiteral("/usr/bin/ditto"), {bundle, target});
  if (!copy.waitForFinished(-1) || copy.exitStatus() != QProcess::NormalExit ||
      copy.exitCode() != 0) {
    QDir(target).removeRecursively();
    return fail(error, QStringLiteral("ditto failed: ") + copy.errorString());
  }
  if (out)
    *out = target;
  return true;
#else
  Q_UNUSED(bundle);
  Q_UNUSED(destination);
  Q_UNUSED(out);
  return fail(error, QStringLiteral("move-to-Applications requires macOS"));
#endif
}
QString ArcadeInstall::cliSymlinkOffer(const QString &name,
                                       const QString &exe) const {
  const auto quote = [](QString value) {
    return QLatin1Char('\'') +
           value.replace(QStringLiteral("'"), QStringLiteral("'\\''")) +
           QLatin1Char('\'');
  };
  return QStringLiteral("mkdir -p %1 && ln -s %2 %3")
      .arg(quote(m_environment.bin), quote(exe),
           quote(QDir(m_environment.bin).filePath(name)));
}
bool ArcadeInstall::installCli(const QString &name, const QString &exe,
                               QString *out, QString *error) const {
  if (!m_environment.validate(error))
    return false;
#ifdef Q_OS_UNIX
  if (!Shortcuts::identifier(name) || !QDir::isAbsolutePath(exe))
    return fail(error, QStringLiteral("invalid CLI name or target"));
  // The symlink helper lives in the install implementation so Linux repair
  // and the macOS offer share exactly the same collision policy.
  extern bool arcadeInstallSymlink(const QString &, const QString &, QString *);
  const auto path = QDir(m_environment.bin).filePath(name);
  if (!arcadeInstallSymlink(path, exe, error))
    return false;
  if (out)
    *out = path;
  return true;
#else
  Q_UNUSED(name);
  Q_UNUSED(exe);
  Q_UNUSED(out);
  return fail(error, QStringLiteral("CLI symlinks require Unix"));
#endif
}
} // namespace ArcadeLink
