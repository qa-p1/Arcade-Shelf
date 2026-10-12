// Canonical key notation (SPEC §8.5). No platform registration or I/O.
#pragma once
#include <QString>

namespace ArcadeLink::Accelerator {
enum class Platform { Linux, Windows, Macos };
// Invalid input returns an empty string and a diagnostic in error.
QString normalize(const QString &input, QString *error = nullptr);
bool isCanonical(const QString &input);
QString display(const QString &input, Platform platform,
                QString *error = nullptr);
// Invalid inputs return false with error set; they must not be treated as free
// bindings.
bool conflicts(const QString &a, const QString &b, QString *error = nullptr);
} // namespace ArcadeLink::Accelerator
