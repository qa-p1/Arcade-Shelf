#include "DesktopPrivate.h"
#include "ArcadeInstall.h"
#include "Identity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QXmlStreamWriter>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace {

QString startupPath()
{
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString programs = QDir::isAbsolutePath(appData)
                                 ? QDir(appData).filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"))
                                 : QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    return QDir(programs).filePath(QStringLiteral("Startup/Arcade Shelf.lnk"));
#elif defined(Q_OS_MACOS)
    ArcadeLink::ArcadeInstall::Environment environment;
    if (!ArcadeLink::ArcadeInstall::Environment::discover(&environment)) return {};
    return QDir(environment.home).filePath(QStringLiteral("Library/LaunchAgents/") + Shelf::OsId + QStringLiteral(".plist"));
#else
    ArcadeLink::ArcadeInstall::Environment environment;
    if (!ArcadeLink::ArcadeInstall::Environment::discover(&environment)) return {};
    return QDir(environment.config).filePath(QStringLiteral("autostart/") + Shelf::OsId + QStringLiteral(".desktop"));
#endif
}

// Where earlier versions put the login entry: a .vbs launcher on Windows, and
// the arcade.shelf name before 0.4 elsewhere. Only Shelf's own entry there is
// ever read or removed (ownedLoginEntry).
QString legacyStartupPath()
{
#ifdef Q_OS_WIN
    QString path = startupPath();
    path.chop(3);
    return path + QStringLiteral("vbs");
#elif defined(Q_OS_MACOS)
    ArcadeLink::ArcadeInstall::Environment environment;
    if (!ArcadeLink::ArcadeInstall::Environment::discover(&environment)) return {};
    return QDir(environment.home).filePath(QStringLiteral("Library/LaunchAgents/arcade.shelf.plist"));
#else
    ArcadeLink::ArcadeInstall::Environment environment;
    if (!ArcadeLink::ArcadeInstall::Environment::discover(&environment)) return {};
    return QDir(environment.config).filePath(QStringLiteral("autostart/arcade.shelf.desktop"));
#endif
}

bool loginEntryExists(const QString &path)
{
#ifdef Q_OS_WIN
    // QFileInfo can resolve .lnk files and report malformed shortcuts as missing.
    const QString native = QDir::toNativeSeparators(path);
    return GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16())) != INVALID_FILE_ATTRIBUTES;
#else
    return QFileInfo::exists(path);
#endif
}

#ifdef Q_OS_WIN
bool windowsLoginShortcut(const QString &path, const QString &executable, bool write)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    IShellLinkW *link = nullptr;
    IPersistFile *file = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, reinterpret_cast<void **>(&link));
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file));
    const QString native = QDir::toNativeSeparators(path);
    if (write) {
        const QString target = QDir::toNativeSeparators(executable);
        const QString directory = QDir::toNativeSeparators(QFileInfo(executable).absolutePath());
        if (SUCCEEDED(result)) result = link->SetPath(reinterpret_cast<LPCWSTR>(target.utf16()));
        if (SUCCEEDED(result)) result = link->SetArguments(L"--background");
        if (SUCCEEDED(result)) result = link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(directory.utf16()));
        if (SUCCEEDED(result)) result = file->Save(reinterpret_cast<LPCWSTR>(native.utf16()), TRUE);
    } else {
        if (SUCCEEDED(result)) result = file->Load(reinterpret_cast<LPCWSTR>(native.utf16()), STGM_READ);
        wchar_t target[32768] = {}, arguments[1024] = {};
        if (SUCCEEDED(result)) result = link->GetPath(target, 32768, nullptr, SLGP_RAWPATH);
        if (SUCCEEDED(result)) result = link->GetArguments(arguments, 1024);
        if (SUCCEEDED(result) && (QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(target)))
                                          .compare(QDir::cleanPath(QDir::fromNativeSeparators(executable)), Qt::CaseInsensitive) != 0 ||
                                  QString::fromWCharArray(arguments) != QStringLiteral("--background"))) result = E_FAIL;
    }
    if (file) file->Release();
    if (link) link->Release();
    if (SUCCEEDED(initialized)) CoUninitialize();
    return SUCCEEDED(result);
}
#endif

QString receiptExecutable(const QJsonObject &receipt)
{
    QString path = receipt.value(QStringLiteral("path")).toString();
#ifdef Q_OS_MACOS
    if (!path.isEmpty()) path = QDir(path).filePath(QStringLiteral("Contents/MacOS/arcade-shelf"));
#endif
    return path;
}

bool ownedLoginEntry(const QString &path, const QString &executable)
{
    const QFileInfo info(path);
    // isSymLink() also matches Windows .lnk files; only reject filesystem symlinks.
    if (!info.isFile() || info.isSymbolicLink()) return false;
#ifdef Q_OS_WIN
    if (path.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) return windowsLoginShortcut(path, executable, false);
#else
    Q_UNUSED(executable)
#endif
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.read(65536);
    if (bytes.contains("Arcade Shelf managed login entry")) return true;
#ifndef Q_OS_WIN
    bool main = false;
    for (const QByteArray &raw : bytes.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.startsWith('[')) main = line == "[Desktop Entry]";
        if (main && line == "X-Arcade-Id=arcade.shelf") return true;
    }
#endif
    return false;
}

bool stableExecutable(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile() || !info.isExecutable() || path.contains(QChar::Null) || path.contains(QLatin1Char('\n')) || path.contains(QLatin1Char('\r'))) return false;
    const QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty()) return false;
    for (const QString &temporary : {QDir::tempPath(), QStringLiteral("/tmp"),
                                     QStringLiteral("/var/tmp"), QStringLiteral("/workspace")}) {
        if (!temporary.isEmpty() && (resolved == temporary || resolved.startsWith(temporary + QLatin1Char('/'))))
            return false;
    }
    const QStringList components = QDir::fromNativeSeparators(resolved).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &entry : components) {
        const QString component = entry.toLower();
        if (component == QStringLiteral("build") || component.startsWith(QStringLiteral("build-")) || component.startsWith(QStringLiteral("cmake-build-")) || component.startsWith(QStringLiteral(".mount_"))) return false;
    }
    return true;
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// Exec= quoting for the Linux login entry.
QString quotedDesktopExec(QString text)
{
    text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    text.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    text.replace(QStringLiteral("`"), QStringLiteral("\\`"));
    text.replace(QStringLiteral("$"), QStringLiteral("\\$"));
    text.replace(QStringLiteral("%"), QStringLiteral("%%"));
    return QLatin1Char('"') + text + QLatin1Char('"');
}
#endif

} // namespace

namespace Shelf {

bool Desktop::startAtLogin() const
{
    QJsonObject receipt;
    if (!ArcadeLink::ReceiptStore::discover().read(QStringLiteral("arcade.shelf"), &receipt)) return false;
    const QString executable = receipt.isEmpty() ? ArcadeLink::executablePath() : receiptExecutable(receipt);
    // An entry switched off in the desktop's startup settings won't run.
    for (const QString &path : {startupPath(), legacyStartupPath()}) {
        if (!ownedLoginEntry(path, executable)) continue;
#ifdef Q_OS_LINUX
        return ArcadeLink::ArcadeInstall::autostartEnabled(path);
#else
        return true;
#endif
    }
    return false;
}

bool Desktop::migrateLoginEntry(const QString &executable)
{
    const bool testingLogin = qEnvironmentVariableIntValue("ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS") == 1 && QDir::isAbsolutePath(qEnvironmentVariable("ARCADE_HOME"));
    if (d->isolated && !testingLogin) return true;
    const QString legacy = legacyStartupPath(), target = startupPath();
    if (legacy.isEmpty() || target.isEmpty()) return true;
    QJsonObject receipt;
    if (!ArcadeLink::ReceiptStore::discover().read(QStringLiteral("arcade.shelf"), &receipt)) return false;
    const QString owner = receipt.isEmpty() ? executable : receiptExecutable(receipt);
    if (!ownedLoginEntry(legacy, owner)) return true;
    // The new entry is already there (an in-place reinstall wrote it): the old one just goes.
    if (loginEntryExists(target)) return QFile::remove(legacy);
#ifdef Q_OS_LINUX
    // An entry switched off in the desktop's startup settings stays off.
    if (!ArcadeLink::ArcadeInstall::autostartEnabled(legacy)) return QFile::remove(legacy);
#endif
    // Writes the entry under the new name, then removes the old one.
    return setStartAtLogin(true, owner);
}

bool Desktop::setStartAtLogin(bool enabled, const QString &executable)
{
    const bool testingLogin = qEnvironmentVariableIntValue("ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS") == 1 && QDir::isAbsolutePath(qEnvironmentVariable("ARCADE_HOME"));
    if (d->isolated && !testingLogin) {
        setError(QStringLiteral("Login changes are disabled in isolated instances"));
        return false;
    }
    const auto receipts = ArcadeLink::ReceiptStore::discover();
    QJsonObject receipt;
    QString error;
    if (!receipts.read(QStringLiteral("arcade.shelf"), &receipt, &error) || (!receipt.isEmpty() && !ArcadeLink::validateNativeReceiptPaths(receipt, &error))) {
        setError(error);
        return false;
    }
    const QString target = startupPath();
    if (target.isEmpty()) {
        setError(QStringLiteral("The per-user login directory is unavailable"));
        return false;
    }
    const QString oldExecutable = receipt.isEmpty() ? executable : receiptExecutable(receipt);
    const QString recorded = receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("autostart")).toString();
    if (!recorded.isEmpty() && QDir::cleanPath(recorded) != QDir::cleanPath(target) && QDir::cleanPath(recorded) != QDir::cleanPath(legacyStartupPath())) {
        setError(QStringLiteral("The receipt names a different login entry; it was kept"));
        return false;
    }
    const auto syncReceipt = [&] {
        if (receipt.isEmpty()) return true; // A manual run must not invent an install receipt.
        auto integration = receipt.value(QStringLiteral("integration")).toObject();
        integration.insert(QStringLiteral("autostart"), enabled ? QJsonValue(target) : QJsonValue(QJsonValue::Null));
        receipt.insert(QStringLiteral("integration"), integration);
        receipt.insert(QStringLiteral("updatedAt"), ArcadeLink::nowRfc3339());
        if (receipts.write(receipt, &error)) return true;
        setError(error);
        return false;
    };
    const auto removeLegacy = [&] {
        const QString legacy = legacyStartupPath();
        if (!legacy.isEmpty() && ownedLoginEntry(legacy, oldExecutable) && !QFile::remove(legacy)) {
            setError(QStringLiteral("Couldn't remove Shelf's old login entry."));
            return false;
        }
        return true;
    };
    const QFileInfo existing(target);
    const bool targetExists = loginEntryExists(target);
    if (existing.isSymbolicLink()) {
        setError(QStringLiteral("The login entry is a symlink; Shelf will not replace it"));
        return false;
    }
    if (targetExists) {
        if (!ownedLoginEntry(target, oldExecutable)) {
            setError(QStringLiteral("An unrelated login entry already uses Shelf's name"));
            return false;
        }
    }
    if (!enabled) {
        if (!targetExists || QFile::remove(target)) return removeLegacy() && syncReceipt();
        setError(QStringLiteral("Couldn't remove Shelf's login entry"));
        return false;
    }
    const QFileInfo installed(executable);
    const bool receiptOwned = !receipt.isEmpty() && receiptExecutable(receipt) == executable && installed.isFile() && installed.isExecutable() && !executable.contains(QChar::Null) && !executable.contains(QLatin1Char('\n')) && !executable.contains(QLatin1Char('\r'));
    if (!stableExecutable(executable) && !receiptOwned) {
        setError(QStringLiteral("Install Shelf in a permanent location before enabling start at login"));
        return false;
    }
    if (!QDir().mkpath(existing.absolutePath())) {
        setError(QStringLiteral("Couldn't create the per-user login directory"));
        return false;
    }
    QByteArray bytes;
#ifdef Q_OS_WIN
    const QString pending = target + QStringLiteral(".pending-%1.lnk").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!windowsLoginShortcut(pending, executable, true) || (targetExists && !QFile::remove(target)) || !QFile::rename(pending, target)) {
        QFile::remove(pending);
        setError(QStringLiteral("Couldn't save Shelf's login shortcut"));
        return false;
    }
    return removeLegacy() && syncReceipt();
#elif defined(Q_OS_MACOS)
    QXmlStreamWriter xml(&bytes);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    xml.writeComment(QStringLiteral("Arcade Shelf managed login entry"));
    xml.writeStartElement(QStringLiteral("plist"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("dict"));
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("Label"));
    xml.writeTextElement(QStringLiteral("string"), OsId);
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProgramArguments"));
    xml.writeStartElement(QStringLiteral("array"));
    xml.writeTextElement(QStringLiteral("string"), executable);
    xml.writeTextElement(QStringLiteral("string"), QStringLiteral("--background"));
    xml.writeEndElement();
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("RunAtLoad"));
    xml.writeEmptyElement(QStringLiteral("true"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
#else
    bytes = (QStringLiteral("# Arcade Shelf managed login entry\n[Desktop Entry]\nType=Application\nName=Arcade Shelf\n") + QStringLiteral("Exec=%1 --background\nTerminal=false\nX-GNOME-Autostart-enabled=true\nX-Arcade-Id=arcade.shelf\n").arg(quotedDesktopExec(executable))).toUtf8();
#endif
#ifndef Q_OS_WIN
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Couldn't save Shelf's login entry"));
        return false;
    }
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return removeLegacy() && syncReceipt();
#endif
}

} // namespace Shelf
