#include "AppInstaller.h"
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QEventLoop>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QSaveFile>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <optional>
#include <QCoreApplication>
#include <QProcess>
#include <QXmlStreamWriter>
#include <QDateTime>

namespace ArcadeApp {
namespace {
using ArcadeLink::ArcadeInstall;

// Keep the event loop responsive while install files or IPC are handled. CLI
// commands use the same path under QCoreApplication without a GUI dependency.
template<class F> auto io(F work)
{
    using Result = decltype(work());
    QFutureWatcher<Result> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(std::move(work)));
    if (!watcher.isFinished()) loop.exec();
    return watcher.result();
}

enum class Dialog { Install, Update, OpenInstalled, MoveToApplications };

struct Options {
    QString channel = QStringLiteral("stable");
    bool startAtLogin = false, removeDownload = true, installCli = true;
};

void err(const QString &message) { QTextStream(stderr) << message << '\n'; }

QString format(const InstallSpec &spec, const QString &pattern, const QString &version, const QString &from = {})
{
    return ArcadeInstall::formatString(pattern, spec.app.name, version, from, spec.version);
}

QString errorText(const InstallSpec &spec, const QString &key, const QString &detail)
{
    const auto errors = ArcadeInstall::strings().value(QStringLiteral("errors")).toObject();
    return format(spec, errors.value(key).toString(QStringLiteral("{from}")), spec.version, detail);
}

bool environment(ArcadeInstall::Environment *out, QString *error)
{
    return ArcadeInstall::Environment::discover(out, error);
}

Options defaults(const InstallSpec &spec, const ArcadeInstall::Environment &env)
{
    Options o;
    QJsonObject receipt;
    env.receipts().read(spec.app.id, &receipt);
    o.channel = receipt.value(QStringLiteral("channel")).toString(QStringLiteral("stable"));
    const QString autostart = receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("autostart")).toString();
    // An existing install reflects whether its entry will really run.
    o.startAtLogin = receipt.isEmpty() ? spec.startAtLoginDefault : (!autostart.isEmpty() && ArcadeInstall::autostartEnabled(autostart));
    return o;
}

// "Don't ask again for this file": path + size + modification time.
QString identity(const QString &path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1\n%2\n%3").arg(info.absoluteFilePath()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
}

QString dismissedFile(const InstallSpec &spec) { return spec.stateDir + QStringLiteral("/install-dismissed.json"); }

bool dismissed(const InstallSpec &spec, const QString &path)
{
    QFile file(dismissedFile(spec));
    if (!file.open(QIODevice::ReadOnly)) return false;
    return QJsonDocument::fromJson(file.readAll()).array().contains(identity(path));
}

void dismiss(const InstallSpec &spec, const QString &path)
{
    QFile file(dismissedFile(spec));
    QJsonArray list;
    if (file.open(QIODevice::ReadOnly)) list = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    while (list.size() >= 128) list.removeFirst();
    list.append(identity(path));
    if (!ArcadeLink::ensurePrivateDir(spec.stateDir)) return;
    QSaveFile output(dismissedFile(spec));
    const auto bytes = QJsonDocument(list).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly)
        || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || output.write(bytes) != bytes.size()) return;
    output.commit();
}

std::optional<Dialog> dialogFor(const InstallSpec &spec, const ArcadeInstall::Detection &d)
{
    if (d.state == u"notInstalled") return Dialog::Install;
    if (d.state != u"installedElsewhere") return std::nullopt;
    const auto installed = QVersionNumber::fromString(d.receipt.value(QStringLiteral("version")).toString());
    return installed < QVersionNumber::fromString(spec.version) ? Dialog::Update : Dialog::OpenInstalled;
}

QString launchPath(const InstallSpec &spec, const QJsonObject &receipt)
{
    const QString path = receipt.value(QStringLiteral("path")).toString();
    if (receipt.value(QStringLiteral("method")).toString() == u"macos-bundle")
        return path + QStringLiteral("/Contents/MacOS/") + QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    Q_UNUSED(spec)
    return path;
}

bool toolsManages(const ArcadeInstall::Environment &env, const QJsonObject &receipt)
{
    if (receipt.value(QStringLiteral("managedBy")).toString() != u"tools") return false;
    ArcadeLink::Registry registry(env.locations);
    registry.refresh();
    const auto tools = registry.app(QStringLiteral("arcade.tools"));
    return !tools.isEmpty() && tools.value(QStringLiteral("settings")).toObject().value(QStringLiteral("linkEnabled")).toBool(true);
}

// Updates of a Tools-managed copy go through Tools (`tools.install`).
bool handoffToTools(const InstallSpec &spec, const ArcadeInstall::Environment &env, QString *error)
{
    ArcadeLink::Registry registry(env.locations);
    registry.refresh();
    const auto tools = registry.app(QStringLiteral("arcade.tools"));
    if (tools.isEmpty()) { if (error) *error = QStringLiteral("Arcade Tools is unavailable."); return false; }
    QJsonObject result;
    ArcadeLink::Error e;
    const QJsonObject request{{QStringLiteral("action"), QStringLiteral("tools.install")},
        {QStringLiteral("inputs"), QJsonArray{ArcadeLink::textContent(QStringLiteral("text/plain"), spec.app.id)}},
        {QStringLiteral("options"), QJsonObject{{QStringLiteral("app"), spec.app.id}}}};
    if (ArcadeLink::invokeAction(env.locations, spec.app.id, spec.version, tools, request, &result, &e)) return true;
    if (error) *error = e.userMessage(QStringLiteral("Arcade Tools"));
    return false;
}

#ifdef Q_OS_MACOS
bool macLogin(const InstallSpec &spec, const ArcadeInstall::Environment &env, const QString &executable,
              bool enabled, QString *path, QString *error)
{
    const QString target = env.home + QStringLiteral("/Library/LaunchAgents/") + spec.app.id + QStringLiteral(".plist");
    const QByteArray marker = (spec.app.name + QStringLiteral(" managed login entry")).toUtf8();
    QFile existing(target);
    if (QFileInfo::exists(target) && (!existing.open(QIODevice::ReadOnly) || !existing.read(65536).contains(marker))) {
        *error = QStringLiteral("An unrelated login entry already uses Shelf's name."); return false;
    }
    existing.close();
    if (!enabled) return !QFileInfo::exists(target) || QFile::remove(target);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) { *error = QStringLiteral("Couldn't create the login directory."); return false; }
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.writeStartDocument();
    xml.writeComment(QString::fromUtf8(marker));
    xml.writeStartElement(QStringLiteral("plist")); xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("dict"));
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("Label")); xml.writeTextElement(QStringLiteral("string"), spec.app.id);
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProgramArguments")); xml.writeStartElement(QStringLiteral("array"));
    xml.writeTextElement(QStringLiteral("string"), executable);
    for (const auto &arg : spec.app.backgroundArgs) xml.writeTextElement(QStringLiteral("string"), arg);
    xml.writeEndElement();
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("RunAtLoad")); xml.writeEmptyElement(QStringLiteral("true"));
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndDocument();
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) { *error = QStringLiteral("Couldn't save the login entry."); return false; }
    *path = target;
    return true;
}
bool installMac(const InstallSpec &spec, const ArcadeInstall &install, const ArcadeInstall::Runtime &runtime,
                const Options &options, QJsonObject *receipt, QString *error)
{
    const auto &env = install.environment();
    const QString source = ArcadeInstall::bundleFor(runtime.executable);
    if (source.isEmpty()) { *error = QStringLiteral("--install requires an app bundle"); return false; }
    const bool alreadyInApplications = install.inApplications(source);
    const QString root = alreadyInApplications ? QFileInfo(source).absolutePath() : env.home + QStringLiteral("/Applications");
    QDir().mkpath(root);
    const QString target = alreadyInApplications ? source : root + QLatin1Char('/') + spec.bundleName;
    QJsonObject old;
    env.receipts().read(spec.app.id, &old);
    if (QFileInfo(source).canonicalFilePath() != QFileInfo(target).canonicalFilePath()) {
        QString moved;
        const QString stage = root + QStringLiteral("/.%1-%2").arg(spec.app.cliName, ArcadeLink::newToken());
        if (!install.moveToApplications(source, stage, &moved, error)) return false;
        const QString previous = target + QStringLiteral(".arcade-previous.app");
        if (QFileInfo::exists(target)) {
            if (old.value(QStringLiteral("path")).toString() != target) {
                QDir(moved).removeRecursively();
                *error = QStringLiteral("Applications already contains %1 without its receipt").arg(spec.bundleName);
                return false;
            }
            if (QFileInfo::exists(previous)
                && (old.value(QStringLiteral("previous")).toObject().value(QStringLiteral("path")).toString() != previous
                    || !QDir(previous).removeRecursively())) {
                QDir(stage).removeRecursively();
                *error = QStringLiteral("The previous bundle is not owned by this installation."); return false;
            }
            if (!QDir().rename(target, previous)) {
                QDir(moved).removeRecursively();
                *error = QStringLiteral("Couldn't preserve the installed copy before updating."); return false;
            }
        }
        if (!QDir().rename(moved, target)) {
            if (QFileInfo::exists(previous)) QDir().rename(previous, target);
            QDir(moved).removeRecursively();
            *error = QStringLiteral("Couldn't move the app into Applications"); return false;
        }
        if (QFileInfo::exists(previous)) {
            old.insert(QStringLiteral("previous"), QJsonObject{{QStringLiteral("path"), previous},
                {QStringLiteral("version"), old.value(QStringLiteral("version"))}});
        }
        QDir(stage).removeRecursively();
    }
    QString cli;
    const QString executable = target + QStringLiteral("/Contents/MacOS/") + QFileInfo(runtime.executable).fileName();
    if (options.installCli && !install.installCli(spec.app.cliName, executable, &cli, error)) return false;
    QString autostart;
    if (!macLogin(spec, env, executable, options.startAtLogin, &autostart, error)) return false;
    const QString now = ArcadeLink::nowRfc3339();
    *receipt = QJsonObject{{QStringLiteral("schema"), 1}, {QStringLiteral("id"), spec.app.id},
        {QStringLiteral("version"), spec.version}, {QStringLiteral("channel"), options.channel},
        {QStringLiteral("method"), QStringLiteral("macos-bundle")},
        {QStringLiteral("managedBy"), old.value(QStringLiteral("managedBy")).toString(QStringLiteral("self"))},
        {QStringLiteral("path"), target},
        {QStringLiteral("integration"), QJsonObject{{QStringLiteral("cli"), cli.isEmpty() ? QJsonValue() : QJsonValue(cli)},
            {QStringLiteral("autostart"), autostart.isEmpty() ? QJsonValue() : QJsonValue(autostart)},
            {QStringLiteral("icons"), QJsonArray{}}}},
        {QStringLiteral("installedAt"), old.value(QStringLiteral("installedAt")).toString(now)},
        {QStringLiteral("updatedAt"), now}};
    if (old.contains(QStringLiteral("previous"))) receipt->insert(QStringLiteral("previous"), old.value(QStringLiteral("previous")));
    return env.receipts().write(*receipt, error);
}
#endif

#ifdef Q_OS_LINUX
// Link writes a generic launcher. Shelf adds its standard metadata (tagline,
// keywords, a specific category, the window class) and names the actions the
// way the tray does. Only an entry carrying Shelf's ownership marker changes,
// and running it again changes nothing.
bool finishDesktopEntry(const InstallSpec &spec, const QJsonObject &receipt, QString *error)
{
    const QString path = receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("desktopEntry")).toString();
    if (path.isEmpty()) return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error = QStringLiteral("Couldn't read the launcher."); return false; }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    if (!lines.contains(QStringLiteral("X-Arcade-Id=") + spec.app.id)) {
        *error = QStringLiteral("Couldn't update the launcher. Its ownership marker changed.");
        return false;
    }
    QString section;
    QStringList out;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1Char('['))) section = line;
        const bool main = section == u"[Desktop Entry]";
        if (main && (line.startsWith(QStringLiteral("Comment=")) || line.startsWith(QStringLiteral("Keywords="))
                     || line.startsWith(QStringLiteral("StartupWMClass="))))
            continue;
        QString next = line;
        if (main && next == u"Categories=Utility;" && !spec.category.isEmpty())
            next = QStringLiteral("Categories=Utility;%1;").arg(spec.category);
        else if (section == u"[Desktop Action Settings]" && next == u"Name=Open Settings")
            next = QStringLiteral("Name=Settings");
        else if (section == u"[Desktop Action Quit]" && next == u"Name=Quit")
            next = QStringLiteral("Name=Quit ") + spec.app.name;
        out << next;
        if (main && next == QStringLiteral("Name=") + spec.app.name) {
            if (!spec.tagline.isEmpty()) out << QStringLiteral("Comment=") + spec.tagline;
            if (!spec.keywords.isEmpty()) out << QStringLiteral("Keywords=") + spec.keywords;
            out << QStringLiteral("StartupWMClass=") + spec.app.desktopId;
        }
    }
    const QByteArray bytes = out.join(QLatin1Char('\n')).toUtf8();
    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly) || save.write(bytes) != bytes.size() || !save.commit()) {
        *error = QStringLiteral("Couldn't update the launcher.");
        return false;
    }
    return true;
}
#endif

bool installCopy(const InstallSpec &spec, const ArcadeInstall &install, const ArcadeInstall::Runtime &runtime,
                 const Options &options, QJsonObject *receipt, QString *error)
{
#ifdef Q_OS_LINUX
    if (!runtime.isAppImage()) {
        // A second install window, after the first one installed (and removed) this download.
        QJsonObject current;
        install.environment().receipts().read(spec.app.id, &current);
        if (!runtime.appimage.isEmpty() && !QFileInfo::exists(runtime.appimage)) {
            if (current.value(QStringLiteral("version")).toString() == spec.version
                && QFileInfo(current.value(QStringLiteral("path")).toString()).isFile()) { *receipt = current; return true; }
            *error = QStringLiteral("This downloaded file is gone (it may have been installed from another window). Open %1 from the app launcher.").arg(spec.app.name);
            return false;
        }
        *error = QStringLiteral("--install requires a real AppImage, not an ordinary or development binary");
        return false;
    }
    QTemporaryDir stage;
    if (!stage.isValid()) { *error = QStringLiteral("Couldn't stage the icons"); return false; }
    QJsonObject old;
    install.environment().receipts().read(spec.app.id, &old);
    ArcadeInstall::InstallOptions o;
    o.app = spec.app;
    o.runtime = runtime;
    o.version = spec.version;
    o.channel = options.channel;
    o.managedBy = old.value(QStringLiteral("managedBy")).toString(QStringLiteral("self"));
    if (!spec.writeIcons) { *error = QStringLiteral("This package has no install icons."); return false; }
    o.icons = spec.writeIcons(stage.path());
    if (o.icons.isEmpty()) { *error = QStringLiteral("Couldn't stage the install icons."); return false; }
    o.startAtLogin = options.startAtLogin;
    o.removeDownload = options.removeDownload;
    return install.install(o, receipt, error) && finishDesktopEntry(spec, *receipt, error);
#elif defined(Q_OS_MACOS)
    return installMac(spec, install, runtime, options, receipt, error);
#else
    Q_UNUSED(spec) Q_UNUSED(install) Q_UNUSED(runtime) Q_UNUSED(options) Q_UNUSED(receipt)
    *error = QStringLiteral("Use the per-user installer on Windows");
    return false;
#endif
}

// The install/update/open dialog. Returns the clicked button index (-1 on
// close) and updates the options from the checkboxes.
int ask(const InstallSpec &spec, Dialog kind, const QString &installedVersion, bool managed, Options *options,
        const QStringList &legacy = {})
{
    const auto strings = ArcadeInstall::strings();
    // Say so when this file is the older one, not just "already installed".
    const bool newerInstalled = kind == Dialog::OpenInstalled && strings.contains(QStringLiteral("newerInstalled"))
        && QVersionNumber::fromString(installedVersion) > QVersionNumber::fromString(spec.version);
    const QString key = kind == Dialog::Install ? QStringLiteral("notInstalled")
        : kind == Dialog::Update ? QStringLiteral("older")
        : newerInstalled ? QStringLiteral("newerInstalled")
        : kind == Dialog::OpenInstalled ? QStringLiteral("sameOrNewer") : QStringLiteral("macos");
    const auto text = strings.value(key).toObject();
    const QString version = kind == Dialog::OpenInstalled ? installedVersion : spec.version;
    const QString from = kind == Dialog::Update ? installedVersion : newerInstalled ? spec.version : QString();
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("%1 — Installation").arg(spec.app.name));
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(QStringLiteral("<b style='font-size:large'>%1</b>").arg(format(spec, text.value(QStringLiteral("title")).toString(), version, from).toHtmlEscaped()));
    title->setWordWrap(true);
    layout->addWidget(title);
    if (text.contains(QStringLiteral("body"))) {
        auto *body = new QLabel(format(spec, text.value(QStringLiteral("body")).toString(), version, from));
        body->setWordWrap(true);
        layout->addWidget(body);
    }
    if (managed) {
        auto *note = new QLabel(format(spec, strings.value(QStringLiteral("managedByTools")).toString(), spec.version));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    const QString note = strings.value(QStringLiteral("replacesLegacy")).toString();
    if (!legacy.isEmpty() && !note.isEmpty() && (kind == Dialog::Install || kind == Dialog::Update)) {
        auto *replaces = new QLabel(format(spec, note, spec.version, legacy.first()));
        replaces->setWordWrap(true);
        layout->addWidget(replaces);
    }
    const auto labels = strings.value(QStringLiteral("notInstalled")).toObject().value(QStringLiteral("options")).toArray();
    // The kit prompt (ui/InstallPrompt.qml), built only while it asks.
    {
        const bool options_shown = kind == Dialog::Install || kind == Dialog::Update || kind == Dialog::MoveToApplications;
        QStringList paragraphs;
        if (text.contains(QStringLiteral("body"))) paragraphs << format(spec, text.value(QStringLiteral("body")).toString(), version, from);
        if (managed) paragraphs << format(spec, strings.value(QStringLiteral("managedByTools")).toString(), spec.version);
        if (!legacy.isEmpty() && !note.isEmpty() && (kind == Dialog::Install || kind == Dialog::Update))
            paragraphs << format(spec, note, spec.version, legacy.first());
        // Option order and keys follow the shared strings: start at login, then
        // remove the download (Linux) or add the terminal command (macOS).
        QVariantList optionList;
        QStringList optionKeys;
        if (options_shown) {
            optionList << QVariantMap{{QStringLiteral("label"), labels.at(0).toString()}, {QStringLiteral("checked"), options->startAtLogin}};
            optionKeys << QStringLiteral("login");
#ifdef Q_OS_LINUX
            optionList << QVariantMap{{QStringLiteral("label"), labels.at(1).toString()}, {QStringLiteral("checked"), options->removeDownload}};
            optionKeys << QStringLiteral("remove");
#endif
#ifdef Q_OS_MACOS
            optionList << QVariantMap{{QStringLiteral("label"), QStringLiteral("Add the %1 terminal command").arg(spec.app.cliName)}, {QStringLiteral("checked"), options->installCli}};
            optionKeys << QStringLiteral("cli");
#endif
        }
        QStringList buttons;
        for (const auto &name : text.value(QStringLiteral("buttons")).toArray()) buttons << name.toString();
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{QStringLiteral("heading"), format(spec, text.value(QStringLiteral("title")).toString(), version, from)},
                                     {QStringLiteral("paragraphs"), paragraphs}, {QStringLiteral("options"), optionList},
                                     {QStringLiteral("buttons"), buttons}});
        engine.loadFromModule("ArcadeShelf", "InstallPrompt");
        if (auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            QEventLoop loop;
            QObject::connect(window, SIGNAL(finished()), &loop, SLOT(quit()));
            loop.exec();
            const QVariantList checks = window->property("checks").toList();
            for (int i = 0; i < optionKeys.size() && i < checks.size(); ++i) {
                if (optionKeys.at(i) == QStringLiteral("login")) options->startAtLogin = checks.at(i).toBool();
                else if (optionKeys.at(i) == QStringLiteral("remove")) options->removeDownload = checks.at(i).toBool();
                else if (optionKeys.at(i) == QStringLiteral("cli")) options->installCli = checks.at(i).toBool();
            }
            return window->property("clicked").toInt();
        }
    }
    // Fallback where the QML module is absent: the plain Qt Widgets prompt.
    QCheckBox login(labels.at(0).toString()), remove(labels.at(1).toString());
    QCheckBox cli(QStringLiteral("Add the %1 terminal command").arg(spec.app.cliName));
    if (kind == Dialog::Install || kind == Dialog::Update || kind == Dialog::MoveToApplications) {
        login.setChecked(options->startAtLogin);
        layout->addWidget(&login);
#ifdef Q_OS_LINUX
        remove.setChecked(options->removeDownload);
        layout->addWidget(&remove);
#endif
#ifdef Q_OS_MACOS
        cli.setChecked(options->installCli);
        layout->addWidget(&cli);
#endif
    }
    // The main action sits rightmost and is the default, as in every Arcade
    // prompt, whatever button layout the Qt platform theme prefers.
    auto *row = new QHBoxLayout;
    row->addStretch();
    int clicked = -1;
    const auto names = text.value(QStringLiteral("buttons")).toArray();
    for (int i = int(names.size()) - 1; i >= 0; --i) {
        auto *button = new QPushButton(names.at(i).toString());
        if (i == 0) button->setDefault(true);
        row->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, &clicked, i] { clicked = i; dialog.accept(); });
    }
    layout->addLayout(row);
    dialog.adjustSize();
    if (const auto *screen = QGuiApplication::primaryScreen())
        dialog.move(screen->availableGeometry().center() - dialog.rect().center());
    dialog.exec();
    options->startAtLogin = login.isChecked();
    options->removeDownload = remove.isChecked();
    options->installCli = cli.isChecked();
    return clicked;
}

void message(const InstallSpec &spec, const QString &text, bool headless)
{
    if (headless) err(text);
    else QMessageBox::information(nullptr, spec.app.name, text);
}

// After a successful install: the PATH hint once, when the command folder
// isn't on PATH. Returns the copy to start.
QString finishInstall(const InstallSpec &spec, const ArcadeInstall &install, const QJsonObject &receipt, bool headless)
{
    const auto status = io([&] { return install.integrationStatus(spec.app.id, qEnvironmentVariable("SHELL")); });
    const QString hint = status.value(QStringLiteral("pathHint")).toString();
    if (!hint.isEmpty())
        message(spec, ArcadeInstall::formatString(ArcadeInstall::strings().value(QStringLiteral("pathHint")).toString(),
                                                  spec.app.name, spec.version, {}, hint), headless);
    return launchPath(spec, receipt);
}

QStringList launchArgs(const QStringList &args)
{
    QStringList out;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == u"--channel") { ++i; continue; }
        if (args[i].startsWith(QStringLiteral("--channel="))) continue;
        if (args[i] == u"--install" || args[i] == u"--silent" || args[i] == u"--no-launch" || args[i] == u"--keep-download") continue;
        out << args[i];
    }
    return out;
}
} // namespace

bool isIntegrationCommand(const QStringList &args)
{
    for (const auto &a : {"--install", "--uninstall", "--repair", "--integration-status"})
        if (args.contains(QLatin1String(a))) return true;
    return false;
}

bool isHeadlessIntegrationCommand(const QStringList &args)
{
    const bool lifecycle = args.contains(QStringLiteral("--install")) || args.contains(QStringLiteral("--uninstall"));
    return args.contains(QStringLiteral("--integration-status")) || args.contains(QStringLiteral("--repair"))
        || (lifecycle && args.contains(QStringLiteral("--silent")));
}

int runIntegrationCommand(const InstallSpec &spec, const QStringList &args)
{
    const bool headless = isHeadlessIntegrationCommand(args);
    ArcadeInstall::Environment env;
    QString error;
    if (!io([&] { return environment(&env, &error); })) { err(spec.app.cliName + QStringLiteral(": ") + error); return 1; }
    ArcadeInstall install(env);
    const auto runtime = ArcadeInstall::Runtime::discover();
    const auto json = [](const QJsonObject &o) { QTextStream(stdout) << QJsonDocument(o).toJson(QJsonDocument::Indented); };
    if (args.contains(QStringLiteral("--integration-status"))) {
        const auto status = io([&] { return install.integrationStatus(spec.app.id, qEnvironmentVariable("SHELL"), &error); });
        if (!error.isEmpty()) { err(spec.app.cliName + QStringLiteral(": ") + error); return 1; }
        json(status);
        return 0;
    }
    ArcadeInstall::Detection detection;
    if (!io([&] { return install.detect(spec.app.id, runtime, &detection, &error); })) { err(spec.app.cliName + QStringLiteral(": ") + error); return 1; }
    if (args.contains(QStringLiteral("--repair"))) {
        QJsonObject receipt;
#ifdef Q_OS_LINUX
        const QString moved = detection.state != u"devBuild" && detection.isAppImage ? detection.runningFrom : QString();
        if (!io([&] { return install.repair(spec.app, moved, &receipt, &error) && finishDesktopEntry(spec, receipt, &error); })) {
            err(errorText(spec, QStringLiteral("repair"), error)); return 1;
        }
#else
        receipt = detection.receipt;
        if (receipt.isEmpty()) { err(errorText(spec, QStringLiteral("noReceipt"), {})); return 1; }
#ifdef Q_OS_WIN
        if (!io([&] {
            if (!ArcadeLink::validateNativeReceiptPaths(receipt, &error)) return false;
            const auto integration = receipt.value(QStringLiteral("integration")).toObject();
            const QString executable = receipt.value(QStringLiteral("path")).toString();
            const QString helper = QFileInfo(executable).absolutePath() + QStringLiteral("/arcade-integration.ps1");
            if (!QFileInfo::exists(helper)) { error = QStringLiteral("The installed Windows integration helper is missing."); return false; }
            QStringList parameters{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("RemoteSigned"),
                QStringLiteral("-File"), helper, QStringLiteral("-Action"), QStringLiteral("Install"),
                QStringLiteral("-Id"), spec.app.id, QStringLiteral("-CliName"), spec.app.cliName,
                QStringLiteral("-Version"), receipt.value(QStringLiteral("version")).toString(),
                QStringLiteral("-Channel"), receipt.value(QStringLiteral("channel")).toString(),
                QStringLiteral("-ManagedBy"), receipt.value(QStringLiteral("managedBy")).toString(),
                QStringLiteral("-ExePath"), executable, QStringLiteral("-Name"), spec.app.name,
                QStringLiteral("-QuietUninstallArgs"), QStringLiteral("/S")};
            for (const auto &key : {QStringLiteral("DesktopEntry"), QStringLiteral("Autostart"), QStringLiteral("Uninstaller")}) {
                QString field = key; field[0] = field[0].toLower();
                const QString value = integration.value(field).toString();
                if (!value.isEmpty()) parameters << QStringLiteral("-") + key << value;
            }
            QProcess process;
            process.start(qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe"), parameters);
            if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                process.kill(); process.waitForFinished(1000);
                error = QStringLiteral("Couldn't repair Windows launcher, CLI and login integration."); return false;
            }
            return env.receipts().read(spec.app.id, &receipt, &error);
        })) { err(error); return 1; }
#else
        const QString executable = launchPath(spec, receipt);
        const QString cli = receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("cli")).toString();
        if (!cli.isEmpty() && !io([&] { return install.installCli(spec.app.cliName, executable, nullptr, &error); })) { err(error); return 1; }
        const bool loginEnabled = !receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("autostart")).toString().isEmpty();
        QString autostart;
        if (loginEnabled && !io([&] { return macLogin(spec, env, executable, true, &autostart, &error); })) { err(error); return 1; }
#endif
#endif
        json(receipt);
        return 0;
    }
    if (args.contains(QStringLiteral("--uninstall"))) {
        const bool removeData = args.contains(QStringLiteral("--remove-data"));
        if (!headless) {
            const auto answer = QMessageBox::question(nullptr, QStringLiteral("Uninstall %1?").arg(spec.app.name),
                QStringLiteral("This removes %1, its launcher entry, terminal command and start-at-login entry.%2")
                    .arg(spec.app.name, removeData ? QStringLiteral(" Settings and data are removed too.") : QStringLiteral(" Settings and data are kept.")));
            if (answer != QMessageBox::Yes) return 0;
        }
#ifdef Q_OS_WIN
        const QString uninstaller = detection.receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("uninstaller")).toString();
        if (uninstaller.isEmpty()) { err(errorText(spec, QStringLiteral("noReceipt"), {})); return 1; }
        if (spec.quitRunning && !io([&] { return spec.quitRunning(&error); })) { err(error); return 1; }
        if (removeData && !io([&] {
            for (const auto &folder : spec.dataFolders) {
                if (!QDir::isAbsolutePath(folder) || QDir(folder).isRoot()) return false;
                const QString canonical = QFileInfo(folder).canonicalFilePath();
                for (const auto &reserved : {env.home, env.data, env.config, env.applications, env.bin})
                    if (canonical == QFileInfo(reserved).canonicalFilePath() || reserved.startsWith(canonical + "/")) return false;
                if (QFileInfo::exists(folder) && !QDir(folder).removeRecursively()) return false;
            }
            return true;
        })) { err(QStringLiteral("Couldn't safely remove Shelf settings and data.")); return 1; }
        QStringList uninstallArgs;
        if (headless) uninstallArgs << QStringLiteral("/S");
        if (!QProcess::startDetached(uninstaller, uninstallArgs)) { err(QStringLiteral("Couldn't start the Windows uninstaller.")); return 1; }
        return 0;
#else
        if (spec.quitRunning && !io([&] { return spec.quitRunning(&error); })) { err(error); return 1; }
#ifdef Q_OS_MACOS
        const QString cli = detection.receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("cli")).toString();
        if (!cli.isEmpty() && !io([&] {
            return !QFileInfo(cli).isSymLink() || QFileInfo(cli).symLinkTarget() != launchPath(spec, detection.receipt) || QFile::remove(cli);
        })) { err(QStringLiteral("Couldn't remove the owned CLI link.")); return 1; }
#endif
        bool removed = false;
        if (!io([&] { return install.uninstall(spec.app.id, {removeData, spec.dataFolders}, &removed, &error); })) {
            message(spec, errorText(spec, QStringLiteral("uninstall"), error), headless);
            return 1;
        }
        if (headless) json(QJsonObject{{QStringLiteral("uninstalled"), removed}});
        return 0;
#endif
    }
    // --install
    if (detection.state == u"devBuild") { message(spec, errorText(spec, QStringLiteral("devBuild"), {}), headless); return 1; }
    Options options = io([&] { return defaults(spec, env); });
    const int channelAt = args.indexOf(QStringLiteral("--channel"));
    if (channelAt >= 0 && channelAt + 1 < args.size()) options.channel = args[channelAt + 1];
    for (const auto &arg : args) if (arg.startsWith(QStringLiteral("--channel="))) options.channel = arg.mid(10);
    const bool keepDownload = args.contains(QStringLiteral("--keep-download"));
    if (keepDownload) options.removeDownload = false;
    if (!headless) {
        const auto kind = dialogFor(spec, detection).value_or(Dialog::Install);
        const bool managed = io([&] { return toolsManages(env, detection.receipt); });
        const auto legacy = io([&] { return install.legacyCopies(spec.app); });
        if (ask(spec, kind, detection.receipt.value(QStringLiteral("version")).toString(), managed, &options, legacy) != 0) return 0;
        if (keepDownload) options.removeDownload = false;
        if (kind == Dialog::OpenInstalled) {
            if (args.contains(QStringLiteral("--no-launch"))) return 0;
            return ArcadeInstall::reexec(launchPath(spec, detection.receipt), launchArgs(args), &error) ? 0 : 1;
        }
        if (managed) {
            if (io([&] { return handoffToTools(spec, env, &error); })) return 0;
            message(spec, error, false); return 1;
        }
    }
    QJsonObject receipt;
    if (!io([&] { return installCopy(spec, install, runtime, options, &receipt, &error); })) {
        message(spec, errorText(spec, QStringLiteral("install"), error), headless);
        return 1;
    }
    const QString launch = finishInstall(spec, install, receipt, headless);
    if (headless) json(receipt);
    // The installed copy replaces an older one still running.
    if (!args.contains(QStringLiteral("--no-launch")) && spec.quitRunning) io([&] { QString ignored; return spec.quitRunning(&ignored); });
    if (!args.contains(QStringLiteral("--no-launch")) && !ArcadeInstall::reexec(launch, launchArgs(args), &error)) {
        err(spec.app.cliName + QStringLiteral(": relaunch: ") + error);
        return 1;
    }
    return 0;
}

bool offerInstall(const InstallSpec &spec, const QStringList &args)
{
    ArcadeInstall::Environment env;
    if (!io([&] { return environment(&env, nullptr); })) return false;
    ArcadeInstall install(env);
    const auto runtime = ArcadeInstall::Runtime::discover();
    ArcadeInstall::Detection detection;
    if (!io([&] { return install.detect(spec.app.id, runtime, &detection, nullptr); })) return false;
    std::optional<Dialog> kind;
#ifdef Q_OS_MACOS
    const QString bundle = ArcadeInstall::bundleFor(runtime.executable);
    if (bundle.isEmpty() || detection.state == u"devBuild") return false;
    if (install.inApplications(bundle)) {
        // First run of a dragged copy: record it, never ask.
        if (detection.receipt.isEmpty()) {
            QJsonObject receipt;
            QString error;
            Options o = defaults(spec, env);
            o.installCli = false;
            io([&] { return installMac(spec, install, runtime, o, &receipt, &error); });
        }
        return false;
    }
    const bool needsMove = ArcadeInstall::isTranslocated(bundle) || bundle.startsWith(env.home + QStringLiteral("/Downloads"))
        || bundle.startsWith(QStringLiteral("/Volumes/")) || !QFileInfo(bundle).isWritable();
    if (!needsMove || dismissed(spec, bundle) || !ArcadeInstall::shouldPrompt(detection, args)) return false;
    kind = dialogFor(spec, detection);
    if (kind == Dialog::Install) kind = Dialog::MoveToApplications;
    const QString downloaded = bundle;
#else
    if (!ArcadeInstall::shouldPrompt(detection, args, false, dismissed(spec, detection.runningFrom))) return false;
    kind = dialogFor(spec, detection);
    const QString downloaded = detection.runningFrom;
#endif
    if (!kind) return false;
    const bool managed = io([&] { return toolsManages(env, detection.receipt); });
    Options options = io([&] { return defaults(spec, env); });
    const auto legacy = io([&] { return install.legacyCopies(spec.app); });
    const int button = ask(spec, *kind, detection.receipt.value(QStringLiteral("version")).toString(), managed, &options, legacy);
    if (button == 2 && kind == Dialog::Install) { io([&] { dismiss(spec, downloaded); return true; }); return false; }
    // Closing the question answers nothing: don't install, don't start.
    if (button == -1) return true;
    if (button != 0) return false;
    QString error;
    if (kind == Dialog::OpenInstalled)
        return ArcadeInstall::reexec(launchPath(spec, detection.receipt), args, &error);
    if (managed) {
        if (io([&] { return handoffToTools(spec, env, &error); })) return true;
        message(spec, errorText(spec, QStringLiteral("install"), error), false);
        return false;
    }
    QJsonObject receipt;
    if (!io([&] { return installCopy(spec, install, runtime, options, &receipt, &error); })) {
        message(spec, errorText(spec, QStringLiteral("install"), error), false);
        return false;
    }
    const QString launch = finishInstall(spec, install, receipt, false);
    // The installed copy replaces an older one still running.
    if (spec.quitRunning) io([&] { QString ignored; return spec.quitRunning(&ignored); });
    if (ArcadeInstall::reexec(launch, args, &error)) return true;
    message(spec, error, false);
    return false;
}

bool migrateIntegration(const InstallSpec &spec)
{
#ifdef Q_OS_LINUX
    ArcadeInstall::Environment env;
    if (!io([&] { return environment(&env, nullptr); })) return false;
    ArcadeInstall install(env);
    const auto runtime = ArcadeInstall::Runtime::discover();
    ArcadeInstall::Detection detection;
    if (!io([&] { return install.detect(spec.app.id, runtime, &detection, nullptr); })) return false;
    if (detection.state != u"installedHere" || !runtime.isAppImage()) return true;
    const QJsonObject integration = detection.receipt.value(QStringLiteral("integration")).toObject();
    const QString entry = integration.value(QStringLiteral("desktopEntry")).toString();
    if (entry.isEmpty() || QFileInfo(entry).fileName() == spec.app.desktopId + QStringLiteral(".desktop")) return true;
    Options options = io([&] { return defaults(spec, env); });
    options.removeDownload = false;
    options.channel = detection.receipt.value(QStringLiteral("channel")).toString(options.channel);
    QJsonObject receipt;
    QString error;
    if (!io([&] { return installCopy(spec, install, runtime, options, &receipt, &error); })) {
        err(spec.app.cliName + QStringLiteral(": couldn't move the launcher to its new name: ") + error);
        return false;
    }
    return true;
#else
    Q_UNUSED(spec)
    return true;
#endif
}

QString installMethod(const QString &id)
{
    ArcadeInstall::Environment env;
    if (!environment(&env, nullptr)) return QStringLiteral("manual");
    ArcadeInstall install(env);
    const auto runtime = ArcadeInstall::Runtime::discover();
    ArcadeInstall::Detection d;
    if (!install.detect(id, runtime, &d, nullptr)) return QStringLiteral("manual");
    if (d.state == u"devBuild") return QStringLiteral("dev");
    if (d.state == u"installedHere") return d.receipt.value(QStringLiteral("method")).toString(QStringLiteral("manual"));
    return d.isAppImage ? QStringLiteral("appimage") : QStringLiteral("manual");
}
} // namespace ArcadeApp
