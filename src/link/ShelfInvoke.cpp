// Adapted from Arcade Wheel 550efe1a56270ed85ad1bb1fe080b37005e65c84.
// App-owned bounded Link v1 invocation; vendored protocol remains unchanged.
#include "link/ShelfInvoke.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <limits>

using namespace ArcadeLink;

QJsonObject ShelfInvoke::actionFor(const QJsonObject &manifest, const QJsonObject &payload)
{
    const QString id = payload.value("action").toString();
    const QString preset = payload.value("preset").toString();
    QJsonObject plain;
    for (const auto &value : manifest.value("actions").toArray()) {
        const auto action = value.toObject();
        if (!preset.isEmpty() && action.value("id").toString() == id + '#' + preset) return action;
        if (action.value("id").toString() == id) plain = action;
    }
    const auto pipeline = payload.value("options").toObject().value("pipeline").toString();
    if (id == "box.pipeline.run" && !pipeline.isEmpty() && manifest.contains("pipelines")) {
        if (!manifest.value("pipelinesLoaded").toBool()) return {};
        for (const auto &value : manifest.value("pipelines").toArray()) {
            const auto entry = value.toObject();
            if (entry.value("id").toString() != pipeline) continue;
            if (plain.isEmpty()) return {};
            for (const auto &key : {"accepts", "produces", "effects", "interactive"}) plain.insert(key, entry.value(key));
            plain.insert("title", entry.value("name"));
            plain.insert("options", QJsonObject{{"pipeline", pipeline}});
            plain.insert("available", plain.value("available").toBool(true) && entry.value("available").toBool(true));
            if (entry.contains("reason")) plain.insert("reason", entry.value("reason"));
            return plain;
        }
        return {};
    }
    return plain;
}

Error ShelfInvoke::validate(const QJsonObject &manifest, const QJsonObject &request)
{
    if (manifest.isEmpty() || !QFileInfo(manifest.value("executable").toString()).isFile())
        return Error::make("not_installed", {});
    if (!manifest.value("settings").toObject().value("linkEnabled").toBool(true))
        return Error::make("denied", {}, "disabled");
    const auto action = actionFor(manifest, request);
    if (action.isEmpty()) return Error::make("unavailable", {}, "the saved action is unavailable");
    if (!action.value("available").toBool(true)) return Error::make("unavailable", {}, action.value("reason").toString());
    const auto platforms = action.value("platforms").toArray();
    if (!platforms.isEmpty() && !platforms.contains(currentPlatform()))
        return Error::make("unavailable", {}, "this action isn't supported on this platform");
    if (request.value("version").toInt(1) != action.value("version").toInt(1))
        return Error::make("version_mismatch", {});
    const auto inputs = request.value("inputs").toArray();
    const auto accepts = action.value("accepts").toArray();
    bool batch = false;
    for (const auto &pattern : accepts)
        if (pattern.toString().section(';', 0, 0).endsWith("[]")) batch = true;
    if (inputs.size() > 1 && (!batch || (manifest.value("id").toString() == "arcade.clipboard"
        && request.value("action").toString() == "clipboard.add")))
        return Error::make("unsupported_input", {}, "this action accepts one Content value");
    if (inputs.isEmpty() && !accepts.isEmpty() && !accepts.contains("*")
        && request.value("action").toString() != "find.show" && request.value("action").toString() != "box.open")
        return Error::make("unsupported_input", {});
    qint64 size = 0;
    qsizetype pathCount = 0;
    const auto addBytes = [&size](qint64 bytes) {
        if (bytes < 0 || size > std::numeric_limits<qint64>::max() - bytes) return false;
        size += bytes;
        return true;
    };
    for (const auto &value : inputs) {
        if (!value.isObject()) return Error::make("unsupported_input", {});
        const auto content = value.toObject();
        if (!acceptsContent(accepts, content)) return Error::make("unsupported_input", {});
        if (content.contains("path")) {
            const auto path = content.value("path").toString();
            if (!QFileInfo(path).isAbsolute() || path.contains(QChar::Null) || ++pathCount > 1000)
                return Error::make("unsupported_input", {});
            const QFileInfo info(path);
            if (!info.exists()) return Error::make("unsupported_input", {});
            if (info.isFile() && !addBytes(info.size())) return Error::make("too_large", {}, {}, MaxLineBytes);
            if (content.contains("htmlPath")) {
                const QFileInfo html(content.value("htmlPath").toString());
                if (!html.isAbsolute() || !html.isFile()) return Error::make("unsupported_input", {});
                if (!addBytes(html.size())) return Error::make("too_large", {}, {}, MaxLineBytes);
            }
        } else if (content.contains("paths")) {
            for (const auto &path : content.value("paths").toArray()) {
                if (!path.isString() || !QFileInfo(path.toString()).isAbsolute()
                    || path.toString().contains(QChar::Null) || ++pathCount > 1000)
                    return Error::make("unsupported_input", {});
                const QFileInfo info(path.toString());
                if (!info.isFile()) return Error::make("unsupported_input", {});
                if (!addBytes(info.size())) return Error::make("too_large", {}, {}, MaxLineBytes);
            }
        } else {
            // Inline data's real size wins over caller-provided metadata.
            if (!addBytes(content.value("text").toString().toUtf8().size()
                        + content.value("html").toString().toUtf8().size()))
                return Error::make("too_large", {}, {}, MaxLineBytes);
        }
    }
    const auto limit = action.value("maxBytes").toInteger(-1);
    if (limit >= 0 && size > limit) return Error::make("too_large", {}, {}, limit);
    return {};
}

namespace {
bool readLine(QLocalSocket &socket, QByteArray &buffer, QJsonObject *message, Error *error,
              QElapsedTimer &clock, int timeoutMs, const std::atomic_bool *cancel = nullptr)
{
    for (;;) {
        if (cancel && cancel->load()) {
            *error = Error::make("cancelled", {});
            return false;
        }
        if (clock.elapsed() >= timeoutMs) {
            *error = Error::make("timeout", {});
            return false;
        }
        const auto nl = buffer.indexOf('\n');
        if (nl >= 0) {
            const auto line = buffer.left(nl + 1);
            buffer.remove(0, nl + 1);
            const auto kind = classify(line, message);
            if (kind != Kind::Response && kind != Kind::Notification) {
                *error = Error::make("internal", {}, "invalid response");
                return false;
            }
            return true;
        }
        if (buffer.size() > MaxLineBytes) {
            *error = Error::make("internal", {}, "response exceeds the protocol limit");
            return false;
        }
        if (socket.bytesAvailable() || socket.waitForReadyRead(qMax(1, qMin(100, timeoutMs - int(clock.elapsed()))))) {
            buffer += socket.read(MaxLineBytes + 1 - buffer.size());
        } else if (socket.state() == QLocalSocket::UnconnectedState) {
            *error = Error::make("not_running", {});
            return false;
        }
    }
}

bool connectPeer(QLocalSocket &socket, const Locations &locations, const QString &app, Error *error)
{
    Endpoint endpoint;
    if (!Endpoint::read(locations, app, &endpoint)) return false;
    socket.connectToServer(endpoint.address);
    if (!socket.waitForConnected(HelloTimeoutMs)) return false;
    socket.write(requestLine(1, "hello", {{"token", endpoint.token}, {"protocol", QJsonArray{1}},
                                         {"client", QJsonObject{{"id", QStringLiteral("arcade.shelf")}, {"version", QStringLiteral(SHELF_VERSION)}}}}));
    socket.waitForBytesWritten(HelloTimeoutMs);
    QByteArray buffer;
    QJsonObject response;
    QElapsedTimer clock;
    clock.start();
    if (!readLine(socket, buffer, &response, error, clock, HelloTimeoutMs)) return false;
    if (response.contains("error")) {
        *error = Error::fromJson(response.value("error").toObject());
        return false;
    }
    if (response.value("id").toInt() != 1 || response.value("result").toObject().value("protocol").toInt() != 1) {
        *error = Error::make("version_mismatch", {});
        return false;
    }
    return true;
}

bool resident(QLocalSocket &socket, const QJsonObject &request, QJsonObject *result, Error *error,
              const std::function<void(double, const QString &)> &progress, const std::atomic_bool *cancel, int timeoutMs)
{
    socket.write(requestLine(2, "invoke", request));
    socket.waitForBytesWritten(HelloTimeoutMs);
    QElapsedTimer clock;
    clock.start();
    QByteArray buffer;
    QString job;
    for (;;) {
        QJsonObject message;
        if (!readLine(socket, buffer, &message, error, clock, timeoutMs, cancel)) {
            if (!job.isEmpty() && socket.state() == QLocalSocket::ConnectedState) {
                socket.write(requestLine(3, "job.cancel", {{"job", job}}));
                socket.waitForBytesWritten(HelloTimeoutMs);
            }
            // Disconnect also cancels the owner's jobs, including a peer that
            // ignores job.cancel. Never wait indefinitely for a cancellation.
            return false;
        }
        if (message.contains("id")) {
            if (message.value("id").toInt() != 2) continue;
            if (message.contains("error")) {
                *error = Error::fromJson(message.value("error").toObject());
                return false;
            }
            const auto value = message.value("result").toObject();
            if (!message.value("result").isObject()) {
                *error = Error::make("internal", {}, "invoke result is not an object");
                return false;
            }
            job = value.value("job").toString();
            if (value.contains("job") && job.isEmpty()) {
                *error = Error::make("internal", {}, "invalid job ticket");
                return false;
            }
            if (job.isEmpty()) { *result = value; return true; }
        } else {
            const auto params = message.value("params").toObject();
            if (job.isEmpty() || params.value("job").toString() != job) continue;
            const auto method = message.value("method").toString();
            if (method == "job.progress" && progress)
                progress(params.value("fraction").toDouble(-1), params.value("message").toString());
            if (method == "job.done") {
                if (params.value("status").toString() == "success") { *result = params; return true; }
                *error = params.contains("error") ? Error::fromJson(params.value("error").toObject())
                    : Error::make(params.value("status").toString() == "cancelled" ? "cancelled" : "internal", {});
                return false;
            }
        }
    }
}

bool oneShot(const QJsonObject &manifest, const QJsonObject &request, QJsonObject *result, Error *error,
             const std::function<void(double, const QString &)> &progress, const std::atomic_bool *cancel, int timeoutMs)
{
    QStringList args;
    for (const auto &arg : manifest.value("launch").toObject().value("invoke").toArray()) args << arg.toString();
    QProcess process;
    ArcadeLink::detachFromImage(process); // another app: it mustn't keep Shelf's image mounted
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(manifest.value("executable").toString(), args);
    if (!process.waitForStarted(LaunchTimeoutMs)) { *error = Error::make("launch_failed", {}); return false; }
    process.write(requestLine(1, "invoke", request));
    process.closeWriteChannel();
    QElapsedTimer clock;
    clock.start();
    QByteArray buffer;
    for (;;) {
        if ((cancel && cancel->load()) || clock.elapsed() >= timeoutMs) {
            *error = Error::make(cancel && cancel->load() ? "cancelled" : "timeout", {});
            process.kill(); process.waitForFinished(1000);
            return false;
        }
        process.waitForReadyRead(100);
        buffer += process.readAllStandardOutput();
        qsizetype nl;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const auto line = buffer.left(nl + 1);
            buffer.remove(0, nl + 1);
            QJsonObject message;
            const auto kind = classify(line, &message);
            if (kind == Kind::Invalid) { *error = Error::make("internal", {}, "invalid response"); return false; }
            if (kind == Kind::Response && message.value("id").toInt() == 1) {
                if (message.contains("error")) { *error = Error::fromJson(message.value("error").toObject()); return false; }
                if (!message.value("result").isObject()) { *error = Error::make("internal", {}, "invoke result is not an object"); return false; }
                *result = message.value("result").toObject();
                process.waitForFinished(1000);
                return true;
            }
            if (kind == Kind::Notification && message.value("method").toString() == "job.progress" && progress) {
                const auto params = message.value("params").toObject();
                progress(params.value("fraction").toDouble(-1), params.value("message").toString());
            }
        }
        if (buffer.size() > MaxLineBytes) { *error = Error::make("internal", {}, "response exceeds the protocol limit"); return false; }
        if (process.state() == QProcess::NotRunning) { *error = Error::make("internal", {}, "one-shot process ended without a result"); return false; }
    }
}
}

bool ShelfInvoke::run(const Locations &locations, const QJsonObject &manifest, const QJsonObject &request,
                      QJsonObject *result, Error *error, const std::function<void(double, const QString &)> &progress,
                      const std::atomic_bool *cancel, int timeoutMs, bool allowLaunch)
{
    *error = validate(manifest, request);
    if (error->isError()) return false;
    if (cancel && cancel->load()) { *error = Error::make("cancelled", {}); return false; }
    const auto line = requestLine(2, "invoke", request);
    if (line.size() > MaxLineBytes) { *error = Error::make("too_large", {}, {}, MaxLineBytes); return false; }
    const auto app = manifest.value("id").toString();
    QLocalSocket socket;
    if (connectPeer(socket, locations, app, error))
        return resident(socket, request, result, error, progress, cancel, timeoutMs);
    if (error->code == "denied" || error->code == "version_mismatch") return false;
    if (!allowLaunch) { *error = Error::make("not_running", {}); return false; }
    *error = {};
    const auto launch = manifest.value("launch").toObject();
    if (!actionFor(manifest, request).value("interactive").toBool() && launch.value("invoke").isArray())
        return oneShot(manifest, request, result, error, progress, cancel, timeoutMs);
    QStringList args;
    for (const auto &arg : launch.value("background").toArray()) args << arg.toString();
    if (progress) progress(-1, QStringLiteral("Starting %1…").arg(appName(app)));
    QProcess starter;
    starter.setProgram(manifest.value("executable").toString());
    starter.setArguments(args);
    ArcadeLink::detachFromImage(starter); // another app: it mustn't keep Shelf's image mounted
    if (!starter.startDetached()) {
        *error = Error::make("launch_failed", {}); return false;
    }
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < LaunchTimeoutMs) {
        if (cancel && cancel->load()) { *error = Error::make("cancelled", {}); return false; }
        QLocalSocket next;
        *error = {};
        if (connectPeer(next, locations, app, error))
            return resident(next, request, result, error, progress, cancel, timeoutMs);
        if (error->code == "denied" || error->code == "version_mismatch") return false;
        QThread::msleep(20); // bounded, user-initiated launch; never idle polling
    }
    *error = Error::make("launch_failed", {});
    return false;
}
