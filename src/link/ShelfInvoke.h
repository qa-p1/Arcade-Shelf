// Adapted from Arcade Wheel 550efe1a56270ed85ad1bb1fe080b37005e65c84.
// App-owned bounded Link v1 invocation; vendored protocol remains unchanged.
#pragma once

#include "ArcadeLink.h"

namespace ShelfInvoke {
// App-owned bounded invocation: the vendored v1 client has no job deadline.
// All calls, including validation of file sizes, run on an invocation worker.
ArcadeLink::Error validate(const QJsonObject &manifest, const QJsonObject &request);
QJsonObject actionFor(const QJsonObject &manifest, const QJsonObject &payload);
bool run(const ArcadeLink::Locations &locations, const QJsonObject &manifest,
         const QJsonObject &request, QJsonObject *result, ArcadeLink::Error *error,
         const std::function<void(double, const QString &)> &progress,
         const std::atomic_bool *cancel, int timeoutMs = 120000, bool allowLaunch = true);
}
