#include "ArcadeInstall.h"
#include "ArcadeShortcuts.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QThread>
#include <QSet>
#include <QRandomGenerator>
#include <functional>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <csignal>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <io.h>
#include <sddl.h>
#include <windows.h>
#endif

namespace ArcadeLink {
QJsonObject ArcadeInstall::strings() {
  static const QByteArray json =
#include "install-strings.inc"
      ;
  return QJsonDocument::fromJson(json).object();
}
QString ArcadeInstall::formatString(const QString &pattern, const QString &app,
                                    const QString &version, const QString &from,
                                    const QString &to) {
  static const QRegularExpression placeholders(
      QStringLiteral("\\{(app|version|from|to)\\}"));
  const QHash<QString, QString> values{{QStringLiteral("app"), app},
                                       {QStringLiteral("version"), version},
                                       {QStringLiteral("from"), from},
                                       {QStringLiteral("to"), to}};
  auto matches = placeholders.globalMatch(pattern);
  QString result;
  qsizetype offset = 0;
  while (matches.hasNext()) {
    const auto match = matches.next();
    result += pattern.mid(offset, match.capturedStart() - offset) +
              values.value(match.captured(1));
    offset = match.capturedEnd();
  }
  return result + pattern.mid(offset);
}
static bool fail(QString *error, const QString &message) {
  if (error)
    *error = message;
  return false;
}
static bool arcadeId(const QString &id) {
  return id.startsWith(u"arcade.") && Shortcuts::identifier(id.mid(7));
}
static bool absolute(const QString &path) {
  return path.startsWith(QLatin1Char('/')) ||
         path.startsWith(QStringLiteral("\\\\")) ||
         QRegularExpression(QStringLiteral("\\A[A-Za-z]:[/\\\\]"))
             .match(path)
             .hasMatch();
}
static bool nativeAbsolute(const QString &path, QString *error) {
  const auto native = QDir::fromNativeSeparators(path);
#ifdef Q_OS_WIN
  const bool rooted = native.startsWith(QStringLiteral("//")) ||
                      QRegularExpression(QStringLiteral("\\A[A-Za-z]:/"))
                          .match(native)
                          .hasMatch();
#else
  const bool rooted = native.startsWith(QLatin1Char('/'));
#endif
  if (!rooted || native.split(QLatin1Char('/')).contains(QStringLiteral("..")))
    return fail(error, QStringLiteral("filesystem path must be natively "
                                      "absolute without parent traversal"));
  return true;
}
static QString canonical(const QString &path) {
  const auto name = QFileInfo(path).canonicalFilePath();
  return name.isEmpty() ? QDir::cleanPath(path) : name;
}
static bool inside(const QString &path, const QString &root) {
  return path == root || path.startsWith(root + QLatin1Char('/'));
}
static bool timestamp(const QJsonValue &value) {
  if (!value.isString())
    return false;
  const auto time = value.toString();
  if (!QRegularExpression(
           QStringLiteral(
               "\\A[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z\\z"))
           .match(time)
           .hasMatch())
    return false;
  const int year = time.mid(0, 4).toInt(), month = time.mid(5, 2).toInt(),
            day = time.mid(8, 2).toInt();
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month >= 1 && month <= 12 && day > 0 &&
         day <= days[month] + (month == 2 && leap ? 1 : 0) &&
         time.mid(11, 2).toInt() < 24 && time.mid(14, 2).toInt() < 60 &&
         time.mid(17, 2).toInt() < 60;
}
bool validateReceipt(const QJsonObject &r, QString *error) {
  if (error)
    error->clear();
  if (!r.value(QStringLiteral("schema")).isDouble() ||
      r.value(QStringLiteral("schema")).toDouble() != 1 ||
      !arcadeId(r.value(QStringLiteral("id")).toString()) ||
      r.value(QStringLiteral("version")).toString().trimmed().isEmpty() ||
      r.value(QStringLiteral("channel")).toString().trimmed().isEmpty())
    return fail(error,
                QStringLiteral(
                    "receipt needs schema 1, Arcade id, version and channel"));
  const QStringList methods{
      QStringLiteral("appimage"),     QStringLiteral("windows-installer"),
      QStringLiteral("macos-bundle"), QStringLiteral("tarball"),
      QStringLiteral("manual"),       QStringLiteral("dev")};
  if (!methods.contains(r.value(QStringLiteral("method")).toString()))
    return fail(error, QStringLiteral("unknown install method"));
  const auto owner = r.value(QStringLiteral("managedBy")).toString();
  if (owner != u"self" && owner != u"tools")
    return fail(error, QStringLiteral("unknown managedBy"));
  if (!absolute(r.value(QStringLiteral("path")).toString()))
    return fail(error, QStringLiteral("receipt path must be absolute"));
  if (!r.value(QStringLiteral("integration")).isObject())
    return fail(error, QStringLiteral("integration must be an object"));
  const auto integration = r.value(QStringLiteral("integration")).toObject();
  for (const auto &key :
       {QStringLiteral("desktopEntry"), QStringLiteral("cli"),
        QStringLiteral("autostart"), QStringLiteral("uninstaller")}) {
    const auto value = integration.value(key);
    if (!value.isNull() && !value.isUndefined() &&
        (!value.isString() || !absolute(value.toString())))
      return fail(error, QStringLiteral("integration paths must be absolute"));
  }
  if (integration.contains(QStringLiteral("icons"))) {
    if (!integration.value(QStringLiteral("icons")).isArray())
      return fail(error, QStringLiteral("icons must be an array"));
    for (const auto &icon :
         integration.value(QStringLiteral("icons")).toArray())
      if (!icon.isString() || !absolute(icon.toString()))
        return fail(error, QStringLiteral("icon path must be absolute"));
  }
  const auto previous = r.value(QStringLiteral("previous"));
  if (!previous.isNull() && !previous.isUndefined()) {
    if (!previous.isObject() ||
        previous.toObject()
            .value(QStringLiteral("version"))
            .toString()
            .trimmed()
            .isEmpty() ||
        !absolute(
            previous.toObject().value(QStringLiteral("path")).toString()) ||
        previous.toObject().value(QStringLiteral("path")) ==
            r.value(QStringLiteral("path")))
      return fail(error, QStringLiteral("invalid previous installation"));
  }
  if (!timestamp(r.value(QStringLiteral("installedAt"))) ||
      !timestamp(r.value(QStringLiteral("updatedAt"))) ||
      r.value(QStringLiteral("updatedAt")).toString() <
          r.value(QStringLiteral("installedAt")).toString())
    return fail(error, QStringLiteral("invalid receipt timestamps"));
  return true;
}
bool validateNativeReceiptPaths(const QJsonObject &r, QString *error) {
  if (!validateReceipt(r, error))
    return false;
  const auto integration = r.value(QStringLiteral("integration")).toObject();
  QStringList paths{r.value(QStringLiteral("path")).toString()};
  for (const auto &key :
       {QStringLiteral("desktopEntry"), QStringLiteral("cli"),
        QStringLiteral("autostart"), QStringLiteral("uninstaller")}) {
    const auto path = integration.value(key).toString();
    if (!path.isEmpty())
      paths << path;
  }
  for (const auto &icon : integration.value(QStringLiteral("icons")).toArray())
    paths << icon.toString();
  const auto previous = r.value(QStringLiteral("previous"))
                            .toObject()
                            .value(QStringLiteral("path"))
                            .toString();
  if (!previous.isEmpty())
    paths << previous;
  for (const auto &path : paths)
    if (!nativeAbsolute(path, error))
      return false;
  return true;
}

#ifdef Q_OS_WIN
static bool privateAcl(const QString &path, QString *error) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return fail(error, QStringLiteral("cannot read user SID"));
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  QByteArray info(int(size), '\0');
  const bool got =
      GetTokenInformation(token, TokenUser, info.data(), size, &size);
  CloseHandle(token);
  if (!got)
    return fail(error, QStringLiteral("cannot read user SID"));
  LPWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(
          reinterpret_cast<TOKEN_USER *>(info.data())->User.Sid, &sid))
    return fail(error, QStringLiteral("cannot format user SID"));
  const QString sddl = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%1)")
                           .arg(QString::fromWCharArray(sid));
  LocalFree(sid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor,
          nullptr))
    return fail(error, QStringLiteral("cannot make private ACL"));
  BOOL present = FALSE, defaulted = FALSE;
  PACL acl = nullptr;
  GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted);
  auto native = QDir::toNativeSeparators(path).toStdWString();
  const auto code = SetNamedSecurityInfoW(
      native.data(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
      nullptr, acl, nullptr);
  LocalFree(descriptor);
  return code == ERROR_SUCCESS ||
         fail(
             error,
             QStringLiteral("cannot protect receipt directory (%1)").arg(code));
}
#endif
static bool atomicWrite(const QString &path, const QByteArray &bytes,
                        bool privateFile, QString *error) {
  if (!nativeAbsolute(path, error))
    return false;
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return fail(error, QStringLiteral("cannot create parent of %1").arg(path));
  // QSaveFile resolves a final symlink. Stage a sibling instead and rename the
  // directory entry itself, so an owned launcher symlink cannot redirect writes.
  // A plain QFile is used because QTemporaryFile keeps its own handle on
  // Windows after close(), which makes the rename fail with a sharing error.
  const auto staged = QFileInfo(path).absolutePath() +
      QStringLiteral("/.arcade-write-%1").arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'));
  QFile file(staged);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    return fail(error, file.errorString());
  const auto discard = [&](const QString &message) {
    file.close();
    QFile::remove(staged);
    return fail(error, message);
  };
  const auto permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
      (privateFile ? QFileDevice::Permissions{} :
                     QFileDevice::Permissions(QFileDevice::ReadGroup | QFileDevice::ReadOther));
  if (!file.setPermissions(permissions))
    return discard(file.errorString());
  if (file.write(bytes) != bytes.size() || !file.flush())
    return discard(file.errorString());
#ifdef Q_OS_UNIX
  if (::fsync(file.handle()) != 0)
    return discard(QString::fromLocal8Bit(std::strerror(errno)));
#endif
  file.close();
#ifdef Q_OS_WIN
  // Another handle (a concurrent writer, a reader or a virus scanner) can
  // briefly hold either file; retry sharing and access errors with a backoff.
  const auto from = QDir::toNativeSeparators(staged);
  const auto to = QDir::toNativeSeparators(path);
  DWORD code = 0;
  for (int attempt = 0, delay = 1; attempt < 40; ++attempt, delay = qMin(delay * 2, 25)) {
    if (MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()), reinterpret_cast<LPCWSTR>(to.utf16()),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      code = 0;
      break;
    }
    code = GetLastError();
    if (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED)
      break;
    Sleep(DWORD(delay));
  }
  if (code != 0) {
    QFile::remove(staged);
    return fail(error, QStringLiteral("cannot replace %1 (%2)").arg(path).arg(code));
  }
#else
  if (::rename(QFile::encodeName(staged).constData(),
               QFile::encodeName(path).constData()) != 0) {
    const auto message = QString::fromLocal8Bit(std::strerror(errno));
    QFile::remove(staged);
    return fail(error, message);
  }
#endif
  return true;
}
static bool removeFile(const QString &path, QString *error) {
  if (path.isEmpty())
    return true;
  if (!nativeAbsolute(path, error))
    return false;
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink())
    return true;
  QFile file(path);
  return file.remove() || fail(error, file.errorString());
}
ReceiptStore::ReceiptStore(const Locations &locations)
    : m_directory(
          QDir(locations.registry).filePath(QStringLiteral("../installs"))) {
  m_directory = QDir::cleanPath(m_directory);
}
ReceiptStore ReceiptStore::discover() {
  return ReceiptStore(Locations::discover());
}
QString ReceiptStore::path(const QString &id) const {
  return arcadeId(id) ? QDir(m_directory).filePath(id + QStringLiteral(".json"))
                      : QString();
}
bool ReceiptStore::read(const QString &id, QJsonObject *out,
                        QString *error) const {
  if (error)
    error->clear();
  if (out)
    *out = {};
  if (!arcadeId(id))
    return fail(error, QStringLiteral("invalid receipt id"));
  QFile file(path(id));
  if (!QFileInfo::exists(file.fileName()))
    return true;
  if (!file.open(QIODevice::ReadOnly))
    return fail(error, file.errorString());
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject())
    return fail(error, QStringLiteral("invalid receipt JSON"));
  const auto receipt = doc.object();
  if (!validateReceipt(receipt, error))
    return false;
  if (receipt.value(QStringLiteral("id")).toString() != id)
    return fail(error, QStringLiteral("receipt id differs from filename"));
  if (out)
    *out = receipt;
  return true;
}
bool ReceiptStore::write(const QJsonObject &r, QString *error) const {
  if (!nativeAbsolute(m_directory, error))
    return false;
  if (!validateReceipt(r, error))
    return false;
  if (r.value(QStringLiteral("method")).toString() == u"dev")
    return fail(error, QStringLiteral("dev receipts are never written"));
  if (!ensurePrivateDir(m_directory))
    return fail(error, QStringLiteral("cannot make private receipt directory"));
#ifdef Q_OS_WIN
  if (!privateAcl(m_directory, error))
    return false;
#endif
  return atomicWrite(path(r.value(QStringLiteral("id")).toString()),
                     QJsonDocument(r).toJson(QJsonDocument::Indented), true,
                     error);
}
bool ReceiptStore::remove(const QString &id, QString *error) const {
  if (error)
    error->clear();
  if (!arcadeId(id))
    return fail(error, QStringLiteral("invalid receipt id"));
  return removeFile(path(id), error);
}
static QString shortHash(const QString &root) {
  quint64 hash = 0xcbf29ce484222325ULL;
  for (char c : QString(root)
                    .replace(QLatin1Char('\\'), QLatin1Char('/'))
                    .remove(QRegularExpression(QStringLiteral("/+$")))
                    .toUtf8()) {
    const auto byte = quint8(c);
    hash ^= byte >= 'A' && byte <= 'Z' ? byte + 32 : byte;
    hash *= 0x100000001b3ULL;
  }
  return QStringLiteral("%1").arg(hash & 0xffffffffffffULL, 12, 16,
                                  QLatin1Char('0'));
}
ArcadeInstall::Environment
ArcadeInstall::Environment::under(const QString &root) {
  const QDir dir(root);
  Environment e;
  e.home = QDir::cleanPath(root);
  e.data = dir.filePath(QStringLiteral(".local/share"));
  e.config = dir.filePath(QStringLiteral(".config"));
  e.applications = dir.filePath(QStringLiteral("Applications/Arcade"));
#ifdef Q_OS_WIN
  e.bin = dir.filePath(QStringLiteral("bin"));
#else
  e.bin = dir.filePath(QStringLiteral(".local/bin"));
#endif
  e.localAppData = dir.filePath(QStringLiteral("AppData/Local"));
  e.locations = Locations::under(root);
  e.windowsEnvironmentKey =
      QStringLiteral("Software\\ArcadeLink\\Isolated\\") + shortHash(root);
  return e;
}
bool ArcadeInstall::Environment::validate(QString *error) const {
  for (const auto &root : {home, data, config, applications, bin, localAppData,
                           locations.registry, locations.runtime})
    if (!nativeAbsolute(root, error))
      return false;
  return true;
}
bool ArcadeInstall::Environment::discover(Environment *out, QString *error) {
  if (error)
    error->clear();
  const auto root = qEnvironmentVariable("ARCADE_HOME");
  if (!root.isEmpty()) {
    if (!QDir::isAbsolutePath(root))
      return fail(error, QStringLiteral(
                             "ARCADE_HOME must be absolute for installation"));
    *out = under(root);
  } else {
#ifdef Q_OS_WIN
    const auto home = qEnvironmentVariable("USERPROFILE");
#else
    const auto home = qEnvironmentVariable("HOME");
#endif
    if (!QDir::isAbsolutePath(home))
      return fail(error, QStringLiteral("an absolute home is required"));
    *out = under(home);
    const auto overridePath = [](const char *key, const QString &fallback) {
      const auto value = qEnvironmentVariable(key);
      return QDir::isAbsolutePath(value) ? value : fallback;
    };
    out->data = overridePath("XDG_DATA_HOME", out->data);
    out->config = overridePath("XDG_CONFIG_HOME", out->config);
    out->localAppData = overridePath("LOCALAPPDATA", out->localAppData);
#ifdef Q_OS_WIN
    out->bin = QDir(out->localAppData).filePath(QStringLiteral("Arcade/bin"));
#endif
    out->locations = Locations::discover();
    out->windowsEnvironmentKey = QStringLiteral("Environment");
  }
  out->path = qEnvironmentVariable("PATH");
  return true;
}
ArcadeInstall::Runtime ArcadeInstall::Runtime::discover() {
  return {QCoreApplication::applicationFilePath(),
          qEnvironmentVariable("APPIMAGE"), qEnvironmentVariable("APPDIR"),
          qEnvironmentVariable("ARCADE_DEV_BUILD") == u"1"};
}
bool ArcadeInstall::Runtime::isAppImage() const {
  return QDir::isAbsolutePath(appimage) && QFileInfo(appimage).isFile() &&
         QDir::isAbsolutePath(appdir) &&
         inside(canonical(executable), canonical(appdir));
}
QString ArcadeInstall::Runtime::runningFrom() const {
#ifdef Q_OS_MACOS
  const auto bundle = ArcadeInstall::bundleFor(executable);
  if (!bundle.isEmpty())
    return bundle;
#endif
  return isAppImage() ? appimage : executable;
}
QJsonObject ArcadeInstall::Detection::toJson() const {
  QJsonObject value{{QStringLiteral("kind"), state}};
  if (state == u"installedElsewhere")
    value.insert(QStringLiteral("version"),
                 receipt.value(QStringLiteral("version")));
  return {{QStringLiteral("runningFrom"), runningFrom},
          {QStringLiteral("isAppImage"), isAppImage},
          {QStringLiteral("receipt"), receipt.isEmpty()
                                          ? QJsonValue(QJsonValue::Null)
                                          : QJsonValue(receipt)},
          {QStringLiteral("installedPath"), installedPath.isEmpty()
                                                ? QJsonValue(QJsonValue::Null)
                                                : QJsonValue(installedPath)},
          {QStringLiteral("state"), value}};
}
bool ArcadeInstall::isDevBuild(const QString &executable) {
  QString path = canonical(executable);
  while (!path.isEmpty()) {
    if (QFileInfo::exists(QDir(path).filePath(QStringLiteral(".git"))))
      return true;
    const QFileInfo info(path);
    if (info.fileName() == u"target" || info.fileName() == u"build") {
      for (const auto &name :
           {QStringLiteral("Cargo.toml"), QStringLiteral("CMakeLists.txt"),
            QStringLiteral("package.json"), QStringLiteral("pubspec.yaml")})
        if (QFileInfo(QDir(info.absolutePath()).filePath(name)).isFile())
          return true;
    }
    const auto parent = info.absolutePath();
    if (path == parent)
      break;
    path = parent;
  }
  return false;
}
bool ArcadeInstall::detect(const QString &id, const Runtime &runtime,
                           Detection *out, QString *error) const {
  Detection d;
  if (!m_environment.receipts().read(id, &d.receipt, error))
    return false;
  d.runningFrom = runtime.runningFrom();
  d.isAppImage = runtime.isAppImage();
  const auto installed = d.receipt.value(QStringLiteral("path")).toString();
  if (!installed.isEmpty() && QDir::isAbsolutePath(installed) &&
      QFileInfo::exists(installed))
    d.installedPath = installed;
  if (runtime.devBuild || isDevBuild(d.runningFrom))
    d.state = QStringLiteral("devBuild");
  else if (d.installedPath.isEmpty())
    d.state = QStringLiteral("notInstalled");
  else
    d.state = canonical(d.runningFrom) == canonical(d.installedPath)
                  ? QStringLiteral("installedHere")
                  : QStringLiteral("installedElsewhere");
  if (out)
    *out = d;
  return true;
}
bool ArcadeInstall::shouldPrompt(const Detection &d, const QStringList &args,
                                 bool noPrompt, bool dismissed) {
  bool installable = d.isAppImage;
#ifdef Q_OS_MACOS
  installable = installable || d.runningFrom.endsWith(u".app");
#endif
  if (!installable || d.state == u"devBuild" || d.state == u"installedHere" ||
      noPrompt || dismissed ||
      qEnvironmentVariable("ARCADE_NO_INSTALL_PROMPT") == u"1")
    return false;
  const QStringList suppress{QStringLiteral("--background"),
                             QStringLiteral("--arcade-invoke"),
                             QStringLiteral("--arcade-manifest"),
                             QStringLiteral("--version"),
                             QStringLiteral("-V"),
                             QStringLiteral("--install"),
                             QStringLiteral("--uninstall"),
                             QStringLiteral("--repair"),
                             QStringLiteral("--integration-status")};
  for (const auto &arg : args)
    if (suppress.contains(arg.section(QLatin1Char('='), 0, 0)))
      return false;
  return true;
}

static bool appValid(const ArcadeInstall::AppInfo &app, QString *error) {
  if (!arcadeId(app.id) || app.name.trimmed().isEmpty() ||
      app.name.contains(QLatin1Char('\n')) ||
      app.name.contains(QLatin1Char('\r')))
    return fail(error, QStringLiteral("invalid app id or name"));
  const auto names =
      QStringList{app.filename, app.desktopId, app.cliName, app.autostartId} +
      app.legacyDesktopIds;
  for (const auto &name : names)
    if (name.isEmpty() || name == u"." || name == u".." ||
        name.contains(
            QRegularExpression(QStringLiteral("[/\\\\\\r\\n\\x{0}]"))))
      return fail(
          error,
          QStringLiteral("integration names must be single path components"));
  for (const auto &arg : app.backgroundArgs)
    if (arg.contains(QRegularExpression(QStringLiteral("[\\r\\n\\x{0}]"))))
      return fail(error, QStringLiteral("invalid background argument"));
  return true;
}
static QString execQuote(QString value) {
  // Exec escaping then Desktop Entry string escaping. Percent is never a field
  // code; control characters use the string escapes (a raw newline would end
  // the key).
  value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"))
      .replace(QStringLiteral("\""), QStringLiteral("\\\""))
      .replace(QStringLiteral("`"), QStringLiteral("\\`"))
      .replace(QStringLiteral("$"), QStringLiteral("\\$"))
      .replace(QStringLiteral("%"), QStringLiteral("%%"));
  value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"))
      .replace(QStringLiteral("\n"), QStringLiteral("\\n"))
      .replace(QStringLiteral("\t"), QStringLiteral("\\t"))
      .replace(QStringLiteral("\r"), QStringLiteral("\\r"));
  return QLatin1Char('"') + value + QLatin1Char('"');
}
static QString execLine(const QString &path, const QStringList &args = {}) {
  QStringList parts;
  // GLib looks the program up before expanding "%%", so a '%' in the path
  // (e.g. in HOME) breaks every launcher; `env` takes it as an argument.
  if (path.contains(QLatin1Char('%')))
    parts << QStringLiteral("env");
  parts << execQuote(path);
  for (const auto &arg : args)
    parts << execQuote(arg);
  return parts.join(QLatin1Char(' '));
}
static QByteArray desktop(const ArcadeInstall::AppInfo &app,
                          const QString &path) {
  return QStringLiteral(
             "# Generated by Arcade Link\n[Desktop "
             "Entry]\nType=Application\nName=%1\nExec=%2\nIcon=%3\nTerminal="
             "false\nCategories=Utility;\nX-Arcade-Id=%4\nActions=Settings;"
             "Quit;Uninstall;\n\n[Desktop Action Settings]\nName=Open "
             "Settings\nExec=%5\n\n[Desktop Action "
             "Quit]\nName=Quit\nExec=%6\n\n[Desktop Action "
             "Uninstall]\nName=Uninstall\nExec=%7\n")
      .arg(app.name, execLine(path), app.desktopId, app.id,
           execLine(path, {QStringLiteral("--settings")}),
           execLine(path, {QStringLiteral("--quit")}),
           execLine(path, {QStringLiteral("--uninstall")}))
      .toUtf8();
}
bool arcadeInstallReplaceSymlink(const QString &path, const QString &target,
                                 QString *error);
static bool symlink(const QString &path, const QString &target,
                    QString *error) {
  if (!nativeAbsolute(path, error) || !nativeAbsolute(target, error))
    return false;
#ifdef Q_OS_UNIX
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return fail(error, QStringLiteral("cannot create CLI parent"));
  const QFileInfo info(path);
  if (info.exists() && !info.isSymLink())
    return fail(error, QStringLiteral("CLI path is an existing regular file"));
  return arcadeInstallReplaceSymlink(path, target, error);
#else
  Q_UNUSED(path);
  Q_UNUSED(target);
  return fail(error, QStringLiteral("CLI symlinks require Unix"));
#endif
}
bool arcadeInstallReplaceSymlink(const QString &path, const QString &target,
                                 QString *error) {
#ifdef Q_OS_UNIX
  if (!nativeAbsolute(path, error))
    return false;
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return fail(error, QStringLiteral("cannot create symlink parent"));
  const auto stage =
      path + QLatin1Char('.') + newToken() + QStringLiteral(".link");
  if (::symlink(QFile::encodeName(target).constData(),
                QFile::encodeName(stage).constData()) != 0)
    return fail(error, QString::fromLocal8Bit(std::strerror(errno)));
  if (::rename(QFile::encodeName(stage).constData(),
               QFile::encodeName(path).constData()) != 0) {
    QFile::remove(stage);
    return fail(error, QString::fromLocal8Bit(std::strerror(errno)));
  }
  return true;
#else
  Q_UNUSED(path);
  Q_UNUSED(target);
  return fail(error, QStringLiteral("CLI symlinks require Unix"));
#endif
}
bool arcadeInstallSymlink(const QString &path, const QString &target,
                          QString *error) {
  return symlink(path, target, error);
}
static bool autostart(const QString &path, const ArcadeInstall::AppInfo &app,
                      const QString &executable, bool create, bool enable,
                      bool *written, QString *error) {
  if (written)
    *written = false;
  QFile file(path);
  QString old;
  if (file.open(QIODevice::ReadOnly))
    old = QString::fromUtf8(file.readAll());
  else if (QFileInfo::exists(path))
    return fail(error, file.errorString());
  else if (!create)
    return true;
  else
    old = QStringLiteral(
              "# Generated by Arcade Link\n[Desktop "
              "Entry]\nType=Application\nName=%1\nExec=\nTerminal=false\nX-"
              "GNOME-Autostart-enabled=true\nX-Arcade-Id=%2\n")
              .arg(app.name, app.id);
  file.close();
  auto lines = old.split(QLatin1Char('\n'));
  if (!lines.isEmpty() && lines.last().isEmpty())
    lines.removeLast();
  bool main = false, replaced = false;
  // A login entry taken over from an older copy becomes recognizably ours.
  const bool marked = old.contains(QStringLiteral("\nX-Arcade-Id=")) ||
                      old.startsWith(QStringLiteral("X-Arcade-Id="));
  for (qsizetype i = 0; i < lines.size(); ++i) {
    auto &line = lines[i];
    if (line.startsWith(QLatin1Char('[')))
      main = line == u"[Desktop Entry]";
    // An explicit "Start at login" re-enables an entry the desktop's startup
    // settings switched off; a repair leaves that choice alone.
    if (enable && main && line.trimmed() == u"Hidden=true") {
      lines.removeAt(i--);
      continue;
    }
    if (enable && main && line.trimmed() == u"X-GNOME-Autostart-enabled=false") {
      line = QStringLiteral("X-GNOME-Autostart-enabled=true");
      continue;
    }
    if (main && line.startsWith(u"Exec=") && !replaced) {
      line = QStringLiteral("Exec=") + execLine(executable, app.backgroundArgs);
      replaced = true;
      if (!marked)
        lines.insert(++i, QStringLiteral("X-Arcade-Id=") + app.id);
    }
  }
  if (!replaced)
    return fail(error, QStringLiteral("autostart entry has no main Exec key"));
  if (!atomicWrite(path,
                   (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8(),
                   false, error))
    return false;
  if (written)
    *written = true;
  return true;
}
static QString entryValue(const QString &text, const QString &key) {
  bool main = false;
  for (const auto &raw : text.split(QLatin1Char('\n'))) {
    const auto line = raw.trimmed();
    if (line.startsWith(QLatin1Char('[')))
      main = line == u"[Desktop Entry]";
    const auto equal = line.indexOf(QLatin1Char('='));
    if (main && equal >= 0 && line.left(equal).trimmed() == key)
      return line.mid(equal + 1);
  }
  return {};
}
// One Exec argument of an already string-unescaped value, from *pos on.
static QString execToken(const QString &decoded, qsizetype *pos) {
  qsizetype i = *pos;
  while (i < decoded.size() && decoded[i].isSpace())
    ++i;
  const bool quoted = i < decoded.size() && decoded[i] == QLatin1Char('"');
  bool closed = !quoted;
  QString token;
  for (i += quoted ? 1 : 0; i < decoded.size(); ++i) {
    auto c = decoded[i];
    if (quoted && c == QLatin1Char('"')) {
      closed = true;
      if (i + 1 < decoded.size() && !decoded[i + 1].isSpace())
        return {};
      ++i;
      break;
    }
    if (!quoted && c.isSpace())
      break;
    if (quoted && c == QLatin1Char('\\')) {
      if (++i == decoded.size() ||
          !QStringLiteral("\"`$\\").contains(decoded[i]))
        return {};
      c = decoded[i];
    } else if (c == QLatin1Char('%')) {
      if (++i == decoded.size() || decoded[i] != QLatin1Char('%'))
        return {};
    } else if (!quoted && (c == QLatin1Char('"') || c == QLatin1Char('\\')))
      return {};
    token += c;
  }
  *pos = i;
  return closed ? token : QString();
}
static QString desktopExecutable(const QString &text) {
  const auto value = entryValue(text, QStringLiteral("Exec"));
  QString decoded;
  for (qsizetype i = 0; i < value.size(); ++i) {
    auto c = value[i];
    if (c == QLatin1Char('\\')) {
      if (++i == value.size())
        return {};
      switch (value[i].unicode()) {
      case 's':
        c = QLatin1Char(' ');
        break;
      case 'n':
        c = QLatin1Char('\n');
        break;
      case 't':
        c = QLatin1Char('\t');
        break;
      case 'r':
        c = QLatin1Char('\r');
        break;
      case '\\':
        c = QLatin1Char('\\');
        break;
      default:
        return {};
      }
    }
    decoded += c;
  }
  qsizetype pos = 0;
  auto token = execToken(decoded, &pos);
  // Launchers for paths containing '%' run the program through `env`.
  if (token == u"env")
    token = execToken(decoded, &pos);
  return token.contains(QRegularExpression(QStringLiteral("[=\\r\\n\\x{0}]")))
             ? QString()
             : token;
}
// Releases before the self-installer ran from wherever they were saved and
// wrote their own launcher, e.g. ArcadeWheel-0.2.0-Linux-x86_64.AppImage.
static bool legacyAppImageName(const ArcadeInstall::AppInfo &app,
                               const QString &name) {
  if (name == app.filename || name == app.cliName)
    return false;
  const auto normalize = [](const QString &s) {
    QString out;
    for (const auto c : s)
      if (c.isLetterOrNumber() && c.unicode() < 128)
        out += c.toLower();
    return out;
  };
  const auto appName = normalize(app.name);
  return !appName.isEmpty() &&
         name.endsWith(QStringLiteral(".appimage"), Qt::CaseInsensitive) &&
         normalize(name).startsWith(appName);
}
static bool desktopOwned(const QString &path, const ArcadeInstall::AppInfo &app,
                         bool recorded) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const auto text = QString::fromUtf8(file.readAll());
  const auto marker = entryValue(text, QStringLiteral("X-Arcade-Id"));
  if (!marker.isEmpty() && marker != app.id)
    return false;
  if (recorded)
    return true;
  const auto exe = desktopExecutable(text);
  if (entryValue(text, QStringLiteral("Type")) != u"Application" ||
      !QDir::isAbsolutePath(exe))
    return false;
  if (marker == app.id)
    return true;
  const auto id = QFileInfo(path).completeBaseName();
  const bool known = id == app.desktopId || id == app.autostartId ||
                     app.legacyDesktopIds.contains(id);
  return known && (QFileInfo(exe).fileName() == app.filename ||
                   QFileInfo(exe).fileName() == app.cliName ||
                   legacyAppImageName(app, QFileInfo(exe).fileName()));
}
static bool availableEntry(const QString &path, bool owned) {
  const QFileInfo info(path);
  return (!info.exists() && !info.isSymLink()) ||
         (owned && (info.isFile() || info.isSymLink()));
}
#ifdef Q_OS_LINUX
static bool recordedDesktopOwned(const QString &path, const QString &id,
                                  const QString &exe) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const auto text = QString::fromUtf8(file.readAll());
  const auto marker = entryValue(text, QStringLiteral("X-Arcade-Id"));
  if (!marker.isEmpty())
    return marker == id;
  const auto target = desktopExecutable(text);
  return entryValue(text, QStringLiteral("Type")) == u"Application" &&
         QDir::isAbsolutePath(target) && canonical(target) == canonical(exe);
}
static QStringList integrationConflicts(const QJsonObject &receipt) {
  const auto integration = receipt.value(QStringLiteral("integration")).toObject();
  const auto id = receipt.value(QStringLiteral("id")).toString();
  const auto exe = receipt.value(QStringLiteral("path")).toString();
  QStringList conflicts;
  for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("autostart")}) {
    const auto path = integration.value(key).toString();
    if (!path.isEmpty() && !availableEntry(path, recordedDesktopOwned(path, id, exe)))
      conflicts << path;
  }
  const auto cli = integration.value(QStringLiteral("cli")).toString();
  if (!cli.isEmpty() && !availableEntry(cli, QFileInfo(cli).isSymLink() &&
                                            QFileInfo(cli).symLinkTarget() == exe))
    conflicts << cli;
  return conflicts;
}
static void appendSkipped(QJsonArray &skipped, const QString &path) {
  for (const auto &entry : skipped)
    if (entry.toObject().value(QStringLiteral("path")).toString() == path)
      return;
  skipped << QJsonObject{{QStringLiteral("path"), path},
                         {QStringLiteral("reason"), QStringLiteral("existing entry belongs to another installation")}};
}
#endif
static QString resolvedPath(const QString &path) {
  QString ancestor = path;
  QStringList tail;
  while (!QFileInfo::exists(ancestor)) {
    const auto info = QFileInfo(ancestor);
    if (info.absoluteFilePath() == info.absolutePath())
      return {};
    tail.prepend(info.fileName());
    ancestor = info.absolutePath();
  }
  auto result = canonical(ancestor);
  for (const auto &part : tail)
    result = QDir(result).filePath(part);
  return result;
}
static bool withinRoot(const QString &path, const QString &root,
                       QString *error) {
  if (!nativeAbsolute(path, error) || !inside(path, root) ||
      !inside(resolvedPath(QFileInfo(path).absolutePath()), resolvedPath(root)))
    return fail(error,
                QStringLiteral("integration path escapes its configured root"));
  return true;
}
static bool integrationRoots(const ArcadeInstall::Environment &e,
                             const QJsonObject &r, QString *error) {
  const auto integration = r.value(QStringLiteral("integration")).toObject();
  for (const auto &icon : integration.value(QStringLiteral("icons")).toArray())
    if (!withinRoot(icon.toString(),
                    QDir(e.data).filePath(QStringLiteral("icons")), error))
      return false;
  for (const auto &entry : QList<QPair<QString, QString>>{
           {QStringLiteral("desktopEntry"),
            QDir(e.data).filePath(QStringLiteral("applications"))},
           {QStringLiteral("cli"), e.bin},
           {QStringLiteral("autostart"),
            QDir(e.config).filePath(QStringLiteral("autostart"))}}) {
    const auto path = integration.value(entry.first).toString();
    if (!path.isEmpty() && !withinRoot(path, entry.second, error))
      return false;
  }
  const auto previous = r.value(QStringLiteral("previous"))
                            .toObject()
                            .value(QStringLiteral("path"))
                            .toString();
  return previous.isEmpty() ||
         withinRoot(previous,
                    QDir(e.applications).filePath(QStringLiteral(".previous")),
                    error);
}
static QStringList duplicates(const ArcadeInstall::Environment &e,
                              const ArcadeInstall::AppInfo &app,
                              const QString &main, const QString &exe) {
  QStringList result;
  const QDir dir(QDir(e.data).filePath(QStringLiteral("applications")));
  for (const auto &info :
       dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
    if (info.absoluteFilePath() == main ||
        !desktopOwned(info.absoluteFilePath(), app, false))
      continue;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
      continue;
    const auto path = desktopExecutable(QString::fromUtf8(file.readAll()));
    if (!path.isEmpty() && canonical(path) != canonical(exe))
      result << info.absoluteFilePath();
  }
  return result;
}


// Held for the whole of an install, repair or uninstall of one app, across
// processes: Rust and Qt lock the same <installs>/<id>.lock. A second
// installer waits (up to two minutes) instead of interleaving with the first.
class AppLock {
public:
  bool acquire(const ReceiptStore &store, const QString &id, QString *error) {
    if (!ensurePrivateDir(store.directory()))
      return fail(error, QStringLiteral("Couldn't create %1").arg(store.directory()));
    const auto json = store.path(id);
    if (json.isEmpty())
      return fail(error, QStringLiteral("invalid receipt id"));
    m_file.setFileName(json.left(json.size() - 4) + QStringLiteral("lock"));
    if (!m_file.open(QIODevice::ReadWrite))
      return fail(error, QStringLiteral("Couldn't open %1: %2").arg(m_file.fileName(), m_file.errorString()));
    QElapsedTimer timer;
    timer.start();
    for (;;) {
#ifdef Q_OS_UNIX
      if (::flock(m_file.handle(), LOCK_EX | LOCK_NB) == 0)
        return true;
      if (errno != EWOULDBLOCK)
        return fail(error, QString::fromLocal8Bit(std::strerror(errno)));
#else
      OVERLAPPED overlapped{};
      if (LockFileEx(reinterpret_cast<HANDLE>(_get_osfhandle(m_file.handle())),
                     LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                     MAXDWORD, MAXDWORD, &overlapped))
        return true;
      if (GetLastError() != ERROR_LOCK_VIOLATION)
        return fail(error, QStringLiteral("cannot lock %1").arg(m_file.fileName()));
#endif
      if (timer.elapsed() > 120000)
        return fail(error, QStringLiteral("another install, update or uninstall of this app is still running"));
      QThread::msleep(100);
    }
  }

private:
  QFile m_file; // closing it releases the lock
};

#ifdef Q_OS_LINUX
// $XDG_DOWNLOAD_DIR from user-dirs.dirs, else ~/Downloads.
static QString downloadDir(const ArcadeInstall::Environment &e) {
  QFile dirs(QDir(e.config).filePath(QStringLiteral("user-dirs.dirs")));
  if (dirs.open(QIODevice::ReadOnly))
    for (const auto &raw : QString::fromUtf8(dirs.readAll()).split(QLatin1Char('\n'))) {
      auto line = raw.trimmed();
      if (!line.startsWith(u"XDG_DOWNLOAD_DIR="))
        continue;
      auto value = line.mid(17).trimmed();
      if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2);
      if (value.startsWith(u"$HOME"))
        value = QDir(e.home).filePath(value.mid(5).remove(0, value.mid(5).startsWith(QLatin1Char('/')) ? 1 : 0));
      if (QDir::isAbsolutePath(value))
        return value;
    }
  return QDir(e.home).filePath(QStringLiteral("Downloads"));
}
// Asks this user's processes started from the AppImage `path` to quit, so an
// older copy doesn't keep running beside its replacement.
static void stopProcessesRunning(const QString &path) {
  const auto target = canonical(path).toLocal8Bit();
  const auto me = QCoreApplication::applicationPid();
  for (const auto &pid : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok = false;
    const auto number = pid.toLongLong(&ok);
    if (!ok || number == me)
      continue;
    QFile environ(QStringLiteral("/proc/%1/environ").arg(pid));
    if (!environ.open(QIODevice::ReadOnly))
      continue;
    for (const auto &var : environ.readAll().split('\0'))
      if (var.startsWith("APPIMAGE=") &&
          canonical(QString::fromLocal8Bit(var.mid(9))).toLocal8Bit() == target)
        ::kill(pid_t(number), SIGTERM);
  }
}
// <filename>.<token>.stage copies left by a killed installer. Called with the
// app's lock held, so no other installer owns them.
static void removeStaleStages(const QString &applications, const QString &filename) {
  static const QRegularExpression token(QStringLiteral("^[0-9a-f]{16,}$"));
  for (const auto &name : QDir(applications).entryList({filename + QStringLiteral(".*.stage")}, QDir::Files)) {
    const auto middle = name.mid(filename.size() + 1, name.size() - filename.size() - 7);
    if (token.match(middle).hasMatch())
      QFile::remove(QDir(applications).filePath(name));
  }
}
#endif
QStringList ArcadeInstall::legacyCopies(const AppInfo &app) const {
  QStringList copies;
#ifdef Q_OS_LINUX
  QStringList entries;
  for (const auto &id : QStringList{app.desktopId} + app.legacyDesktopIds)
    entries << QDir(m_environment.data).filePath(QStringLiteral("applications/") + id + QStringLiteral(".desktop"));
  entries << QDir(m_environment.config).filePath(QStringLiteral("autostart/") + app.autostartId + QStringLiteral(".desktop"));
  for (const auto &entry : entries) {
    QFile file(entry);
    if (!file.open(QIODevice::ReadOnly))
      continue;
    const auto text = QString::fromUtf8(file.readAll());
    const auto exe = desktopExecutable(text);
    if (!exe.isEmpty() && legacyAppImageName(app, QFileInfo(exe).fileName()) &&
        entryValue(text, QStringLiteral("X-Arcade-Id")).isEmpty() &&
        QFileInfo(exe).isFile() && !copies.contains(exe))
      copies << exe;
  }
#else
  Q_UNUSED(app);
#endif
  return copies;
}
bool ArcadeInstall::quitRunning(const Locations &locations, const QString &id,
                                int timeoutMs, bool *wasRunning, QString *error) {
  if (wasRunning)
    *wasRunning = false;
  Endpoint endpoint;
  if (!Endpoint::read(locations, id, &endpoint))
    return true;
  Error linkError;
  auto client = Client::connect(locations, id, id + QStringLiteral(".installer"),
                                QStringLiteral("1"), 2000, &linkError);
  if (!client)
    return true; // a stale endpoint file
  client->call(QStringLiteral("app.quit"), {}, &linkError, 5000);
  if (!linkError.code.isEmpty())
    return fail(error, linkError.message);
  client.reset();
  if (wasRunning)
    *wasRunning = true;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    Endpoint current;
    const bool replaced = !Endpoint::read(locations, id, &current) || current.token != endpoint.token;
#ifdef Q_OS_LINUX
    // An app that exits without removing its endpoint is gone too.
    const bool exited = !QFileInfo::exists(QStringLiteral("/proc/%1").arg(endpoint.pid));
#else
    const bool exited = false;
#endif
    if (replaced || exited)
      return true;
    QThread::msleep(50);
  }
  return fail(error, QStringLiteral("%1 did not quit").arg(id));
}
bool ArcadeInstall::autostartEnabled(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const auto text = QString::fromUtf8(file.readAll());
  return entryValue(text, QStringLiteral("Hidden")).trimmed() != u"true" &&
         entryValue(text, QStringLiteral("X-GNOME-Autostart-enabled")).trimmed() != u"false";
}
bool ArcadeInstall::isDownload(const QString &path) const {
#ifdef Q_OS_LINUX
  return inside(canonical(path), canonical(downloadDir(m_environment)));
#else
  Q_UNUSED(path);
  return false;
#endif
}

struct SavedEntry {
  QString path, target;
  QByteArray bytes;
  QFileDevice::Permissions permissions;
  enum Kind { Missing, File, Link } kind = Missing;
};
class InstallRollback {
public:
  QString dest, stage, replaced, savedPrevious, previous;
  QList<SavedEntry> entries;
  bool newBinary = false, committed = false;
  bool save(const QString &path, QString *error) {
    for (const auto &e : entries)
      if (e.path == path)
        return true;
    SavedEntry e;
    e.path = path;
    const QFileInfo info(path);
    if (info.isSymLink()) {
      e.kind = SavedEntry::Link;
#ifdef Q_OS_UNIX
      QByteArray bytes(512, '\0');
      for (;;) {
        const auto size = ::readlink(QFile::encodeName(path).constData(),
                                     bytes.data(), size_t(bytes.size()));
        if (size < 0)
          return fail(error, QString::fromLocal8Bit(std::strerror(errno)));
        if (size < bytes.size()) {
          e.target = QFile::decodeName(bytes.left(size));
          break;
        }
        if (bytes.size() >= MaxLineBytes)
          return fail(error, QStringLiteral("symlink target is too large"));
        bytes.resize(bytes.size() * 2);
      }
#else
      e.target = info.symLinkTarget();
#endif
    } else if (info.isFile()) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
      e.kind = SavedEntry::File;
      e.bytes = file.readAll();
      e.permissions = info.permissions();
    } else if (info.exists())
      return fail(
          error, QStringLiteral("integration entry must be a file or symlink"));
    entries << e;
    return true;
  }
  ~InstallRollback() {
    QFile::remove(stage);
    if (committed) {
      if (!savedPrevious.isEmpty())
        QFile::remove(savedPrevious);
      return;
    }
    if (newBinary)
      QFile::remove(dest);
    if (!replaced.isEmpty())
      QFile::rename(replaced, dest);
    if (!savedPrevious.isEmpty())
      QFile::rename(savedPrevious, previous);
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
      if (it->kind == SavedEntry::Missing)
        QFile::remove(it->path);
      else if (it->kind == SavedEntry::Link)
        arcadeInstallReplaceSymlink(it->path, it->target, nullptr);
      else if (atomicWrite(it->path, it->bytes, false, nullptr))
        QFile::setPermissions(it->path, it->permissions);
    }
  }
};
bool ArcadeInstall::install(const InstallOptions &opts, QJsonObject *out,
                            QString *error) const {
#ifndef Q_OS_LINUX
  Q_UNUSED(opts);
  Q_UNUSED(out);
  return fail(
      error,
      QStringLiteral(
          "self-install requires Linux; use native installer or move helper"));
#else
  if (error)
    error->clear();
  if (!m_environment.validate(error) || !appValid(opts.app, error))
    return false;
  Detection d;
  if (!detect(opts.app.id, opts.runtime, &d, error))
    return false;
  if (d.state == u"devBuild")
    return fail(error, QStringLiteral("dev builds cannot be installed"));
  if (opts.version.trimmed().isEmpty() || opts.channel.trimmed().isEmpty() ||
      (opts.managedBy != u"self" && opts.managedBy != u"tools"))
    return fail(error,
                QStringLiteral("version, channel and managedBy are required"));
  // Refusals above leave no trace; everything below runs under the lock,
  // starting from a fresh read of the receipt.
  AppLock lock;
  if (!lock.acquire(m_environment.receipts(), opts.app.id, error) ||
      !detect(opts.app.id, opts.runtime, &d, error))
    return false;
  const auto source = d.runningFrom;
  if (!QDir::isAbsolutePath(source) || !QFileInfo(source).isFile())
    return fail(error, QStringLiteral("source must be an absolute file"));
  const auto &e = m_environment;
  const auto dest = QDir(e.applications).filePath(opts.app.filename);
  const bool same = canonical(source) == canonical(dest);
  const auto old = d.receipt;
  // A dangling symlink is still somebody's entry.
  const bool destPresent = QFileInfo::exists(dest) || QFileInfo(dest).isSymLink();
  if (!same && destPresent &&
      old.value(QStringLiteral("path")).toString() != dest)
    return fail(
        error, QStringLiteral("destination exists without this app's receipt"));
  if (!e.validate(error) ||
      (!old.isEmpty() && (!validateNativeReceiptPaths(old, error) ||
                          !integrationRoots(e, old, error))))
    return false;
  const auto oldIntegration =
      old.value(QStringLiteral("integration")).toObject();
  QJsonObject integration{{QStringLiteral("desktopEntry"), QJsonValue::Null},
                          {QStringLiteral("cli"), QJsonValue::Null},
                          {QStringLiteral("autostart"), QJsonValue::Null},
                          {QStringLiteral("uninstaller"), QJsonValue::Null}};
  QJsonArray skipped;
  const auto skip = [&](const QString &path) {
    skipped << QJsonObject{
        {QStringLiteral("path"), path},
        {QStringLiteral("reason"),
         QStringLiteral("existing entry belongs to another installation")}};
  };
  const auto entry =
      QDir(e.data).filePath(QStringLiteral("applications/") +
                            opts.app.desktopId + QStringLiteral(".desktop"));
  if (!withinRoot(entry, QDir(e.data).filePath(QStringLiteral("applications")),
                  error))
    return false;
  if (availableEntry(
          entry,
          oldIntegration.value(QStringLiteral("desktopEntry")).toString() == entry
              ? recordedDesktopOwned(entry, opts.app.id, old.value(QStringLiteral("path")).toString())
              : desktopOwned(entry, opts.app, false)))
    integration.insert(QStringLiteral("desktopEntry"), entry);
  else
    skip(entry);
  const auto cli = QDir(e.bin).filePath(opts.app.cliName);
  if (!withinRoot(cli, e.bin, error))
    return false;
  const auto cliTarget = QFileInfo(cli).symLinkTarget();
  const bool cliOwned =
      QFileInfo(cli).isSymLink() &&
      (cliTarget == dest ||
       (!old.isEmpty() &&
        cliTarget == old.value(QStringLiteral("path")).toString()));
  if (availableEntry(cli, cliOwned))
    integration.insert(QStringLiteral("cli"), cli);
  else
    skip(cli);
  QJsonArray icons;
  QList<QPair<QString, QByteArray>> prepared;
  for (const auto &icon : opts.icons) {
    if (icon.pixels != 0 && (icon.pixels < 16 || icon.pixels > 512))
      return fail(error,
                  QStringLiteral("icon size must be 16–512 or scalable"));
    const auto size = icon.pixels == 0
                          ? QStringLiteral("scalable")
                          : QStringLiteral("%1x%1").arg(icon.pixels);
    const auto ext =
        icon.pixels == 0 ? QStringLiteral(".svg") : QStringLiteral(".png");
    const auto target =
        QDir(e.data).filePath(QStringLiteral("icons/hicolor/%1/apps/%2%3")
                                  .arg(size, opts.app.desktopId, ext));
    if (!withinRoot(target, QDir(e.data).filePath(QStringLiteral("icons")),
                    error))
      return false;
    if (!availableEntry(target, oldIntegration.value(QStringLiteral("icons"))
                                    .toArray()
                                    .contains(target))) {
      skip(target);
      continue;
    }
    QFile file(icon.source);
    if (!file.open(QIODevice::ReadOnly))
      return fail(error, file.errorString());
    prepared << qMakePair(target, file.readAll());
    icons << target;
  }
  for (const auto &icon :
       oldIntegration.value(QStringLiteral("icons")).toArray()) {
    bool blocked = false;
    for (const auto &item : skipped)
      blocked |= item.toObject().value(QStringLiteral("path")) == icon;
    if (!blocked && !icons.contains(icon))
      icons << icon;
  }
  integration.insert(QStringLiteral("icons"), icons);
  const auto autoPath = QDir(e.config).filePath(QStringLiteral("autostart/") +
                                                opts.app.autostartId +
                                                QStringLiteral(".desktop"));
  if (!withinRoot(autoPath,
                  QDir(e.config).filePath(QStringLiteral("autostart")), error))
    return false;
  if (QFileInfo::exists(autoPath) || QFileInfo(autoPath).isSymLink() ||
      opts.startAtLogin) {
    if (availableEntry(
            autoPath,
            oldIntegration.value(QStringLiteral("autostart")).toString() == autoPath
                ? recordedDesktopOwned(autoPath, opts.app.id, old.value(QStringLiteral("path")).toString())
                : desktopOwned(autoPath, opts.app, false)))
      integration.insert(QStringLiteral("autostart"), autoPath);
    else
      skip(autoPath);
  }
  if (!skipped.isEmpty())
    integration.insert(QStringLiteral("skipped"), skipped);
  const auto legacy = legacyCopies(opts.app);
  InstallRollback rollback;
  rollback.dest = dest;
  rollback.stage =
      dest + QLatin1Char('.') + newToken() + QStringLiteral(".stage");
  for (const auto &icon : prepared)
    if (!rollback.save(icon.first, error))
      return false;
  for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("cli"),
                          QStringLiteral("autostart")}) {
    const auto path = integration.value(key).toString();
    if (!path.isEmpty() && !rollback.save(path, error))
      return false;
  }
  const auto stale =
      integration.value(QStringLiteral("desktopEntry")).isString()
          ? duplicates(e, opts.app, entry, dest)
          : QStringList{};
  for (const auto &path : stale)
    if (!rollback.save(path, error))
      return false;
  auto previous = old.value(QStringLiteral("previous"));
  if (!same) {
    if (!QDir().mkpath(e.applications))
      return fail(error, QStringLiteral("Couldn't create %1").arg(e.applications));
    removeStaleStages(e.applications, opts.app.filename);
    // Copy before any replacement. QFile::copy closes/flushed output before
    // rename; the rollback journal never holds AppImage bytes in memory.
    QFile input(source);
    if (!input.copy(rollback.stage))
      return fail(error, QStringLiteral("Couldn't write to %1: %2").arg(e.applications, input.errorString()));
#ifdef Q_OS_UNIX
    QFile staged(rollback.stage);
    if (!staged.open(QIODevice::ReadWrite) || !staged.flush() ||
        ::fsync(staged.handle()) != 0)
      return fail(error, QStringLiteral("cannot sync staged AppImage"));
    staged.close();
#endif
    if (!QFile::setPermissions(
            rollback.stage, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                QFileDevice::ExeOther))
      return fail(error, QStringLiteral("cannot chmod AppImage"));
    if (destPresent) {
      const auto ownedPrevious = old.value(QStringLiteral("previous"))
                                     .toObject()
                                     .value(QStringLiteral("path"))
                                     .toString();
      const auto preferred =
          QDir(e.applications)
              .filePath(QStringLiteral(".previous/") + opts.app.filename);
      rollback.previous = ownedPrevious.isEmpty() ? preferred : ownedPrevious;
      if (ownedPrevious.isEmpty() &&
          (QFileInfo::exists(preferred) || QFileInfo(preferred).isSymLink()))
        rollback.previous =
            QDir(QFileInfo(preferred).absolutePath())
                .filePath(newToken() + QLatin1Char('-') + opts.app.filename);
      if (!QDir().mkpath(QFileInfo(rollback.previous).absolutePath()))
        return fail(error, QStringLiteral("cannot create previous folder"));
      if (QFileInfo::exists(rollback.previous) ||
          QFileInfo(rollback.previous).isSymLink()) {
        if (ownedPrevious != rollback.previous)
          return fail(error, QStringLiteral(
                                 "previous destination is not receipt-owned"));
        const auto backup = rollback.previous + QLatin1Char('.') + newToken() +
                            QStringLiteral(".backup");
        if (!QFile::rename(rollback.previous, backup))
          return fail(error,
                      QStringLiteral("cannot preserve previous AppImage"));
        rollback.savedPrevious = backup;
      }
      if (!QFile::rename(dest, rollback.previous))
        return fail(error, QStringLiteral("cannot retain replaced AppImage"));
      rollback.replaced = rollback.previous;
      previous = QJsonObject{
          {QStringLiteral("version"), old.value(QStringLiteral("version"))},
          {QStringLiteral("path"), rollback.previous}};
    }
    if (!QFile::rename(rollback.stage, dest))
      return fail(error, QStringLiteral("cannot replace installed AppImage"));
    rollback.newBinary = true;
  }
  if (!QFile::setPermissions(
          dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                    QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                    QFileDevice::ExeGroup | QFileDevice::ReadOther |
                    QFileDevice::ExeOther))
    return fail(error, QStringLiteral("cannot chmod AppImage"));
  for (const auto &icon : prepared)
    if (!atomicWrite(icon.first, icon.second, false, error))
      return false;
  const auto entryPath =
      integration.value(QStringLiteral("desktopEntry")).toString();
  if (!entryPath.isEmpty() &&
      !atomicWrite(entryPath, desktop(opts.app, dest), false, error))
    return false;
  if (integration.value(QStringLiteral("cli")).isString() &&
      !symlink(cli, dest, error))
    return false;
  if (integration.value(QStringLiteral("autostart")).isString() &&
      !autostart(autoPath, opts.app, dest, opts.startAtLogin, opts.startAtLogin,
                 nullptr, error))
    return false;
  for (const auto &path : stale)
    if (!removeFile(path, error))
      return false;
  const auto now = nowRfc3339();
  QJsonObject receipt{{QStringLiteral("schema"), 1},
                      {QStringLiteral("id"), opts.app.id},
                      {QStringLiteral("version"), opts.version},
                      {QStringLiteral("channel"), opts.channel},
                      {QStringLiteral("method"), QStringLiteral("appimage")},
                      {QStringLiteral("managedBy"), opts.managedBy},
                      {QStringLiteral("path"), dest},
                      {QStringLiteral("integration"), integration},
                      {QStringLiteral("installedAt"),
                       old.isEmpty()
                           ? QJsonValue(now)
                           : old.value(QStringLiteral("installedAt"))},
                      {QStringLiteral("updatedAt"), now}};
  if (previous.isObject())
    receipt.insert(QStringLiteral("previous"), previous);
  if (!e.receipts().write(receipt, error))
    return false;
  rollback.committed = true;
  if (out)
    *out = receipt;
  // The install has happened: from here nothing turns it into a failure, and
  // "Remove the downloaded file" only ever removes a file from Downloads.
  if (opts.removeDownload && !same && isDownload(source))
    removeFile(source, nullptr);
  for (const auto &copy : legacy) {
    if (canonical(copy) == canonical(dest) || canonical(copy) == canonical(source))
      continue;
    stopProcessesRunning(copy);
    if (opts.removeDownload && isDownload(copy))
      removeFile(copy, nullptr);
  }
  if (opts.refreshCaches) {
    QProcess::execute(QStringLiteral("update-desktop-database"),
                      {QDir(e.data).filePath(QStringLiteral("applications"))});
    QProcess::execute(QStringLiteral("gtk-update-icon-cache"),
                      {QStringLiteral("-t"),
                       QDir(e.data).filePath(QStringLiteral("icons/hicolor"))});
  }
  return true;
#endif
}
bool ArcadeInstall::repair(const AppInfo &app, const QString &movedTo,
                           QJsonObject *out, QString *error) const {
#ifndef Q_OS_LINUX
  Q_UNUSED(app);
  Q_UNUSED(movedTo);
  Q_UNUSED(out);
  return fail(error, QStringLiteral("desktop repair requires Linux"));
#else
  if (!m_environment.validate(error) || !appValid(app, error))
    return false;
  QJsonObject receipt;
  if (!m_environment.receipts().read(app.id, &receipt, error))
    return false;
  if (receipt.isEmpty())
    return fail(error, QStringLiteral("no receipt"));
  AppLock lock;
  if (!lock.acquire(m_environment.receipts(), app.id, error) ||
      !m_environment.receipts().read(app.id, &receipt, error))
    return false;
  if (receipt.isEmpty())
    return fail(error, QStringLiteral("no receipt"));
  if (!validateNativeReceiptPaths(receipt, error) ||
      !integrationRoots(m_environment, receipt, error))
    return false;
  const auto conflicts = integrationConflicts(receipt);
  if (!movedTo.isEmpty()) {
    if (!nativeAbsolute(movedTo, error))
      return false;
    if (!QFileInfo(movedTo).isFile())
      return fail(
          error, QStringLiteral("moved installation must be an absolute file"));
    receipt.insert(QStringLiteral("path"), canonical(movedTo));
  }
  if (!validateNativeReceiptPaths(receipt, error))
    return false;
  const auto exe = receipt.value(QStringLiteral("path")).toString();
  if (!QFileInfo(exe).isFile())
    return fail(error, QStringLiteral("installed executable is missing"));
  auto integration =
      receipt.value(QStringLiteral("integration")).toObject();
  auto skipped = integration.value(QStringLiteral("skipped")).toArray();
  for (const auto &path : conflicts) {
    for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("autostart"), QStringLiteral("cli")})
      if (integration.value(key).toString() == path)
        integration.insert(key, QJsonValue::Null);
    appendSkipped(skipped, path);
  }
  if (!skipped.isEmpty())
    integration.insert(QStringLiteral("skipped"), skipped);
  receipt.insert(QStringLiteral("integration"), integration);
  const auto entry =
      integration.value(QStringLiteral("desktopEntry")).toString();
  if (!entry.isEmpty()) {
    if (!atomicWrite(entry, desktop(app, exe), false, error))
      return false;
    for (const auto &path : duplicates(m_environment, app, entry, exe))
      if (!removeFile(path, error))
        return false;
  }
  const auto cli = integration.value(QStringLiteral("cli")).toString();
  if (!cli.isEmpty() && !symlink(cli, exe, error))
    return false;
  const auto autoPath =
      integration.value(QStringLiteral("autostart")).toString();
  if (!autoPath.isEmpty() &&
      !autostart(autoPath, app, exe, true, false, nullptr, error))
    return false;
  receipt.insert(QStringLiteral("updatedAt"), nowRfc3339());
  if (!m_environment.receipts().write(receipt, error))
    return false;
  if (out)
    *out = receipt;
  return true;
#endif
}
static bool removeTree(const QString &path, QString *error) {
  if (!nativeAbsolute(path, error))
    return false;
  const QFileInfo info(path);
  if (info.isDir() && !info.isSymLink())
    return QDir(path).removeRecursively() ||
           fail(error, QStringLiteral("cannot remove %1").arg(path));
  return removeFile(path, error);
}
bool ArcadeInstall::uninstall(const QString &id, const UninstallOptions &opts,
                              bool *removed, QString *error) const {
  if (removed)
    *removed = false;
  if (!m_environment.validate(error))
    return false;
  QJsonObject receipt;
  if (!m_environment.receipts().read(id, &receipt, error))
    return false;
  if (receipt.isEmpty())
    return true;
  AppLock lock;
  if (!lock.acquire(m_environment.receipts(), id, error) ||
      !m_environment.receipts().read(id, &receipt, error))
    return false;
  if (receipt.isEmpty())
    return true;
  if (!validateNativeReceiptPaths(receipt, error))
    return false;
#ifdef Q_OS_LINUX
  if (!integrationRoots(m_environment, receipt, error))
    return false;
  const auto conflicts = integrationConflicts(receipt);
  if (!conflicts.isEmpty())
    return fail(error, QStringLiteral("integration ownership conflict at %1; repair to detach the foreign entry before uninstalling")
                           .arg(conflicts.first()));
#endif
  const auto exe = receipt.value(QStringLiteral("path")).toString();
  const auto previous = receipt.value(QStringLiteral("previous"))
                            .toObject()
                            .value(QStringLiteral("path"))
                            .toString();
  for (const auto &path : {exe, previous})
    if (!path.isEmpty() && QFileInfo(path).isDir() &&
        (receipt.value(QStringLiteral("method")).toString() !=
             u"macos-bundle" ||
         !path.endsWith(u".app")))
      return fail(
          error,
          QStringLiteral("refusing a directory as an installed executable"));
  if (opts.removeData) {
    for (const auto &path : opts.dataFolders) {
      if (!nativeAbsolute(path, error) || QDir(path).isRoot())
        return fail(error,
                    QStringLiteral("refusing a root directory as app data"));
      for (const auto &root :
           {m_environment.home, m_environment.data, m_environment.config,
            m_environment.applications, m_environment.bin})
        if (inside(canonical(root), canonical(path)))
          return fail(error,
                      QStringLiteral("refusing a root directory as app data"));
    }
  }
  const auto integration =
      receipt.value(QStringLiteral("integration")).toObject();
  for (const auto &key :
       {QStringLiteral("desktopEntry"), QStringLiteral("autostart")})
    if (!removeFile(integration.value(key).toString(), error))
      return false;
  for (const auto &icon : integration.value(QStringLiteral("icons")).toArray())
    if (!removeFile(icon.toString(), error))
      return false;
  const auto cli = integration.value(QStringLiteral("cli")).toString();
  if (QFileInfo(cli).isSymLink()) {
    if (QFileInfo(cli).symLinkTarget() == exe && !removeFile(cli, error))
      return false;
  }
#ifdef Q_OS_WIN
  else if (!removeFile(cli, error))
    return false;
#endif
  if (!removeTree(exe, error) ||
      (!previous.isEmpty() && !removeTree(previous, error)))
    return false;
  if (opts.removeData)
    for (const auto &path : opts.dataFolders)
      if (!removeTree(path, error))
        return false;
  if (!removeFile(m_environment.locations.manifestPath(id), error) ||
      !m_environment.receipts().remove(id, error))
    return false;
#ifdef Q_OS_WIN
  if (!removeWindowsPathIfLast(m_environment.bin, error))
    return false;
#endif
  if (removed)
    *removed = true;
  return true;
}
QString ArcadeInstall::pathHint(const QString &bin, const QString &shell) {
  const auto quoted =
      QLatin1Char('\'') +
      QString(bin).replace(QStringLiteral("'"), QStringLiteral("'\\''")) +
      QLatin1Char('\'');
  return QFileInfo(shell).fileName() == u"fish"
             ? QStringLiteral("fish_add_path ") + quoted
             : QStringLiteral("export PATH=") + quoted +
                   QStringLiteral(":\"$PATH\"");
}
QJsonObject ArcadeInstall::integrationStatus(const QString &id,
                                             const QString &shell,
                                             QString *error) const {
  QJsonObject receipt;
  if (!m_environment.receipts().read(id, &receipt, error))
    return {};
  QJsonArray entries;
  const auto add = [&](const QString &path) {
    if (!path.isEmpty())
      entries << QJsonObject{
          {QStringLiteral("path"), path},
          {QStringLiteral("exists"),
           QDir::isAbsolutePath(path) && QFileInfo::exists(path)}};
  };
  if (!receipt.isEmpty()) {
    add(receipt.value(QStringLiteral("path")).toString());
    const auto integration =
        receipt.value(QStringLiteral("integration")).toObject();
    add(integration.value(QStringLiteral("desktopEntry")).toString());
    for (const auto &icon :
         integration.value(QStringLiteral("icons")).toArray())
      add(icon.toString());
    for (const auto &key : {QStringLiteral("cli"), QStringLiteral("autostart"),
                            QStringLiteral("uninstaller")})
      add(integration.value(key).toString());
    add(receipt.value(QStringLiteral("previous"))
            .toObject()
            .value(QStringLiteral("path"))
            .toString());
  }
  bool onPath = false;
  for (const auto &path :
       m_environment.path.split(QDir::listSeparator(), Qt::SkipEmptyParts))
    onPath |= canonical(path) == canonical(m_environment.bin);
  auto skipped = receipt.value(QStringLiteral("integration")).toObject()
                     .value(QStringLiteral("skipped")).toArray();
#ifdef Q_OS_LINUX
  if (!receipt.isEmpty() && validateNativeReceiptPaths(receipt))
    for (const auto &path : integrationConflicts(receipt))
      appendSkipped(skipped, path);
#endif
  return {
      {QStringLiteral("receipt"),
       receipt.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(receipt)},
      {QStringLiteral("entries"), entries},
      {QStringLiteral("binOnPath"), onPath},
      {QStringLiteral("skipped"), skipped},
      {QStringLiteral("pathHint"),
       onPath ? QJsonValue(QJsonValue::Null)
              : QJsonValue(pathHint(m_environment.bin, shell))}};
}
// Mirrors arcade_link::childenv: drops what an AppImage's runtime, AppRun and
// linuxdeploy's GTK hook put into the environment, so the started app gets the
// user's environment, not this image's mount.
QProcessEnvironment ArcadeInstall::childEnvironment(QProcessEnvironment environment) {
  const auto appdir = environment.value(QStringLiteral("APPDIR"));
  const bool fromAppImage = !appdir.isEmpty() || environment.contains(QStringLiteral("APPIMAGE"));
  const auto inMount = [&](QString entry) {
    while (entry.endsWith(QLatin1Char('/')))
      entry.chop(1);
    QString dir = appdir;
    while (dir.endsWith(QLatin1Char('/')))
      dir.chop(1);
    if (!dir.isEmpty() && (entry == dir || entry.startsWith(dir + QLatin1Char('/'))))
      return true;
    for (const auto &part : entry.split(QLatin1Char('/')))
      if (part.startsWith(QStringLiteral(".mount_")))
        return true;
    return false;
  };
  static const QStringList appImageOnly{
      QStringLiteral("APPDIR"), QStringLiteral("APPIMAGE"), QStringLiteral("ARGV0"), QStringLiteral("OWD"),
      QStringLiteral("GTK_DATA_PREFIX"), QStringLiteral("GTK_EXE_PREFIX"), QStringLiteral("GTK_PATH"),
      QStringLiteral("GTK_IM_MODULE_FILE"), QStringLiteral("GDK_PIXBUF_MODULE_FILE"), QStringLiteral("GIO_MODULE_DIR"),
      QStringLiteral("GI_TYPELIB_PATH"), QStringLiteral("PYTHONHOME"), QStringLiteral("PYTHONDONTWRITEBYTECODE"),
      QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH")};
  const bool gtkHook = inMount(environment.value(QStringLiteral("GTK_DATA_PREFIX"))) &&
                       environment.contains(QStringLiteral("GTK_DATA_PREFIX"));
  for (const auto &key : environment.keys()) {
    if (fromAppImage && (appImageOnly.contains(key) || (key == u"GTK_THEME" && gtkHook))) {
      environment.remove(key);
      continue;
    }
    const auto entries = environment.value(key).split(QLatin1Char(':'));
    bool touched = false;
    for (const auto &entry : entries)
      touched |= inMount(entry);
    if (!touched)
      continue;
    QStringList kept;
    for (const auto &entry : entries)
      if (!entry.isEmpty() && !inMount(entry) && !kept.contains(entry))
        kept << entry;
    if (kept.isEmpty())
      environment.remove(key);
    else
      environment.insert(key, kept.join(QLatin1Char(':')));
  }
  return environment;
}
bool ArcadeInstall::reexec(const QString &path, const QStringList &args,
                           QString *error) {
  if (!nativeAbsolute(path, error))
    return false;
  const bool fromAppImage = qEnvironmentVariableIsSet("APPIMAGE");
  auto environment = childEnvironment(QProcessEnvironment::systemEnvironment());
  environment.remove(QStringLiteral("APPIMAGE"));
  environment.remove(QStringLiteral("APPDIR"));
#ifdef Q_OS_UNIX
  if (!fromAppImage) {
    QList<QByteArray> bytes{QFile::encodeName(path)};
    for (const auto &arg : args)
      bytes << arg.toLocal8Bit();
    QList<char *> pointers;
    for (auto &arg : bytes)
      pointers << arg.data();
    pointers << nullptr;
    QList<QByteArray> envBytes;
    for (const auto &key : environment.keys())
      envBytes << (key + QLatin1Char('=') + environment.value(key)).toLocal8Bit();
    QList<char *> envPointers;
    for (auto &value : envBytes)
      envPointers << value.data();
    envPointers << nullptr;
    ::execve(bytes.first().constData(), pointers.data(), envPointers.data());
    return fail(error, QString::fromLocal8Bit(std::strerror(errno)));
  }
#endif
  // From an AppImage (and on Windows): a detached successor, and the caller
  // exits, so the downloaded image's mount goes away with this process.
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(path);
  process.setArguments(args);
  // Output still reaches this process's stdout and stderr, as with exec.
  process.setStandardInputFile(QProcess::nullDevice());
  process.setProcessChannelMode(QProcess::ForwardedChannels);
  if (QDir::current().absolutePath().contains(QStringLiteral("/.mount_")))
    process.setWorkingDirectory(QDir::homePath());
  return process.startDetached() || fail(error, process.errorString());
}
} // namespace ArcadeLink
