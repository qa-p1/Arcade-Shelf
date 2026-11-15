#pragma once
#include <QString>
#include <QStringList>

namespace Shelf {
// Whether a start with these arguments (argv without the program name) becomes
// or keeps the resident Shelf. Only those starts run from the unpacked copy of
// the AppImage (U1, RT-7): a one-shot call never keeps a process of its own, so
// copying the image for it, or letting its exit status be replaced by the
// launcher's 0, would only cost. These are the options a resident start takes;
// anything else (the install commands, --status, --quit, --hide, --restart,
// --version, --help, a malformed command line) runs from the mount and reports
// itself as before.
inline bool startsResident(const QStringList &arguments)
{
    static const QStringList flags{QStringLiteral("--background"), QStringLiteral("--show"), QStringLiteral("--toggle"), QStringLiteral("--settings")};
    static const QStringList valued{QStringLiteral("--add"), QStringLiteral("--wait-for-exit")};
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (flags.contains(argument)) continue;
        if (valued.contains(argument)) {
            // The value is the next argument; without one the parser reports an error.
            if (++i >= arguments.size()) return false;
            continue;
        }
        const qsizetype equals = argument.indexOf(u'=');
        if (equals > 0 && valued.contains(argument.left(equals)) && equals + 1 < argument.size()) continue;
        return false;
    }
    return true;
}
} // namespace Shelf
