// Canonical accelerators, mirrored by Rust and the stdlib validator.
#include "ArcadeAccelerator.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace ArcadeLink::Accelerator {
static const QHash<QString, QString> modifiers{
    {QStringLiteral("ctrl"), QStringLiteral("Ctrl")},
    {QStringLiteral("control"), QStringLiteral("Ctrl")},
    {QStringLiteral("ctl"), QStringLiteral("Ctrl")},
    {QStringLiteral("control_l"), QStringLiteral("Ctrl")},
    {QStringLiteral("control_r"), QStringLiteral("Ctrl")},
    {QStringLiteral("alt"), QStringLiteral("Alt")},
    {QStringLiteral("option"), QStringLiteral("Alt")},
    {QStringLiteral("opt"), QStringLiteral("Alt")},
    {QStringLiteral("alt_l"), QStringLiteral("Alt")},
    {QStringLiteral("alt_r"), QStringLiteral("Alt")},
    {QStringLiteral("shift"), QStringLiteral("Shift")},
    {QStringLiteral("shift_l"), QStringLiteral("Shift")},
    {QStringLiteral("shift_r"), QStringLiteral("Shift")},
    {QStringLiteral("super"), QStringLiteral("Super")},
    {QStringLiteral("cmd"), QStringLiteral("Super")},
    {QStringLiteral("command"), QStringLiteral("Super")},
    {QStringLiteral("win"), QStringLiteral("Super")},
    {QStringLiteral("windows"), QStringLiteral("Super")},
    {QStringLiteral("meta"), QStringLiteral("Super")},
    {QStringLiteral("logo"), QStringLiteral("Super")},
    {QStringLiteral("mod4"), QStringLiteral("Super")},
    {QStringLiteral("super_l"), QStringLiteral("Super")},
    {QStringLiteral("super_r"), QStringLiteral("Super")},
};
static const QHash<QString, QString> keys{
    {QStringLiteral("space"), QStringLiteral("Space")},
    {QStringLiteral("enter"), QStringLiteral("Enter")},
    {QStringLiteral("tab"), QStringLiteral("Tab")},
    {QStringLiteral("escape"), QStringLiteral("Escape")},
    {QStringLiteral("backspace"), QStringLiteral("Backspace")},
    {QStringLiteral("delete"), QStringLiteral("Delete")},
    {QStringLiteral("insert"), QStringLiteral("Insert")},
    {QStringLiteral("home"), QStringLiteral("Home")},
    {QStringLiteral("end"), QStringLiteral("End")},
    {QStringLiteral("pageup"), QStringLiteral("PageUp")},
    {QStringLiteral("pagedown"), QStringLiteral("PageDown")},
    {QStringLiteral("up"), QStringLiteral("Up")},
    {QStringLiteral("down"), QStringLiteral("Down")},
    {QStringLiteral("left"), QStringLiteral("Left")},
    {QStringLiteral("right"), QStringLiteral("Right")},
    {QStringLiteral("minus"), QStringLiteral("Minus")},
    {QStringLiteral("equal"), QStringLiteral("Equal")},
    {QStringLiteral("bracketleft"), QStringLiteral("BracketLeft")},
    {QStringLiteral("bracketright"), QStringLiteral("BracketRight")},
    {QStringLiteral("backslash"), QStringLiteral("Backslash")},
    {QStringLiteral("semicolon"), QStringLiteral("Semicolon")},
    {QStringLiteral("quote"), QStringLiteral("Quote")},
    {QStringLiteral("backquote"), QStringLiteral("Backquote")},
    {QStringLiteral("comma"), QStringLiteral("Comma")},
    {QStringLiteral("period"), QStringLiteral("Period")},
    {QStringLiteral("slash"), QStringLiteral("Slash")},
    {QStringLiteral("print"), QStringLiteral("Print")},
    {QStringLiteral("pause"), QStringLiteral("Pause")},
    {QStringLiteral("scrolllock"), QStringLiteral("ScrollLock")},
    {QStringLiteral("capslock"), QStringLiteral("CapsLock")},
    {QStringLiteral("numlock"), QStringLiteral("NumLock")},
    {QStringLiteral("menu"), QStringLiteral("Menu")},
    {QStringLiteral("numpadadd"), QStringLiteral("NumpadAdd")},
    {QStringLiteral("numpadsubtract"), QStringLiteral("NumpadSubtract")},
    {QStringLiteral("numpadmultiply"), QStringLiteral("NumpadMultiply")},
    {QStringLiteral("numpaddivide"), QStringLiteral("NumpadDivide")},
    {QStringLiteral("numpaddecimal"), QStringLiteral("NumpadDecimal")},
    {QStringLiteral("numpadenter"), QStringLiteral("NumpadEnter")},
    {QStringLiteral("volumeup"), QStringLiteral("VolumeUp")},
    {QStringLiteral("volumedown"), QStringLiteral("VolumeDown")},
    {QStringLiteral("volumemute"), QStringLiteral("VolumeMute")},
    {QStringLiteral("micmute"), QStringLiteral("MicMute")},
    {QStringLiteral("mediaplaypause"), QStringLiteral("MediaPlayPause")},
    {QStringLiteral("medianext"), QStringLiteral("MediaNext")},
    {QStringLiteral("mediaprevious"), QStringLiteral("MediaPrevious")},
    {QStringLiteral("mediastop"), QStringLiteral("MediaStop")},
    {QStringLiteral("brightnessup"), QStringLiteral("BrightnessUp")},
    {QStringLiteral("brightnessdown"), QStringLiteral("BrightnessDown")},
    {QStringLiteral("mouseleft"), QStringLiteral("MouseLeft")},
    {QStringLiteral("mouseright"), QStringLiteral("MouseRight")},
    {QStringLiteral("mousemiddle"), QStringLiteral("MouseMiddle")},
    {QStringLiteral("mouseback"), QStringLiteral("MouseBack")},
    {QStringLiteral("mouseforward"), QStringLiteral("MouseForward")},
    {QStringLiteral("wheelup"), QStringLiteral("WheelUp")},
    {QStringLiteral("wheeldown"), QStringLiteral("WheelDown")},
    {QStringLiteral("wheelleft"), QStringLiteral("WheelLeft")},
    {QStringLiteral("wheelright"), QStringLiteral("WheelRight")},
    {QStringLiteral("/"), QStringLiteral("Slash")},
    {QStringLiteral("-"), QStringLiteral("Minus")},
    {QStringLiteral("="), QStringLiteral("Equal")},
    {QStringLiteral("plus"), QStringLiteral("Equal")},
    {QStringLiteral(","), QStringLiteral("Comma")},
    {QStringLiteral("."), QStringLiteral("Period")},
    {QStringLiteral(";"), QStringLiteral("Semicolon")},
    {QStringLiteral("'"), QStringLiteral("Quote")},
    {QStringLiteral("apostrophe"), QStringLiteral("Quote")},
    {QStringLiteral("`"), QStringLiteral("Backquote")},
    {QStringLiteral("grave"), QStringLiteral("Backquote")},
    {QStringLiteral("["), QStringLiteral("BracketLeft")},
    {QStringLiteral("]"), QStringLiteral("BracketRight")},
    {QStringLiteral("\\"), QStringLiteral("Backslash")},
    {QStringLiteral("return"), QStringLiteral("Enter")},
    {QStringLiteral("esc"), QStringLiteral("Escape")},
    {QStringLiteral("del"), QStringLiteral("Delete")},
    {QStringLiteral("ins"), QStringLiteral("Insert")},
    {QStringLiteral("pgup"), QStringLiteral("PageUp")},
    {QStringLiteral("prior"), QStringLiteral("PageUp")},
    {QStringLiteral("pgdn"), QStringLiteral("PageDown")},
    {QStringLiteral("next"), QStringLiteral("PageDown")},
    {QStringLiteral("arrowup"), QStringLiteral("Up")},
    {QStringLiteral("arrowdown"), QStringLiteral("Down")},
    {QStringLiteral("arrowleft"), QStringLiteral("Left")},
    {QStringLiteral("arrowright"), QStringLiteral("Right")},
    {QStringLiteral("xf86audioraisevolume"), QStringLiteral("VolumeUp")},
    {QStringLiteral("xf86audiolowervolume"), QStringLiteral("VolumeDown")},
    {QStringLiteral("xf86audiomute"), QStringLiteral("VolumeMute")},
    {QStringLiteral("xf86audioplay"), QStringLiteral("MediaPlayPause")},
    {QStringLiteral("xf86audionext"), QStringLiteral("MediaNext")},
    {QStringLiteral("xf86audioprev"), QStringLiteral("MediaPrevious")},
    {QStringLiteral("xf86audiostop"), QStringLiteral("MediaStop")},
    {QStringLiteral("xf86monbrightnessup"), QStringLiteral("BrightnessUp")},
    {QStringLiteral("xf86monbrightnessdown"), QStringLiteral("BrightnessDown")},
    {QStringLiteral("mouse:272"), QStringLiteral("MouseLeft")},
    {QStringLiteral("mouse:273"), QStringLiteral("MouseRight")},
    {QStringLiteral("mouse:274"), QStringLiteral("MouseMiddle")},
    {QStringLiteral("mouse:275"), QStringLiteral("MouseBack")},
    {QStringLiteral("mouse:276"), QStringLiteral("MouseForward")},
    {QStringLiteral("mouse_up"), QStringLiteral("WheelUp")},
    {QStringLiteral("mouse_down"), QStringLiteral("WheelDown")},
    {QStringLiteral("mouse_left"), QStringLiteral("WheelLeft")},
    {QStringLiteral("mouse_right"), QStringLiteral("WheelRight")},
    {QStringLiteral("xf86audiomicmute"), QStringLiteral("MicMute")},
    {QStringLiteral("page_up"), QStringLiteral("PageUp")},
    {QStringLiteral("page_down"), QStringLiteral("PageDown")},
    {QStringLiteral("iso_left_tab"), QStringLiteral("Tab")},
    {QStringLiteral("kp_0"), QStringLiteral("Numpad0")},
    {QStringLiteral("kp_1"), QStringLiteral("Numpad1")},
    {QStringLiteral("kp_2"), QStringLiteral("Numpad2")},
    {QStringLiteral("kp_3"), QStringLiteral("Numpad3")},
    {QStringLiteral("kp_4"), QStringLiteral("Numpad4")},
    {QStringLiteral("kp_5"), QStringLiteral("Numpad5")},
    {QStringLiteral("kp_6"), QStringLiteral("Numpad6")},
    {QStringLiteral("kp_7"), QStringLiteral("Numpad7")},
    {QStringLiteral("kp_8"), QStringLiteral("Numpad8")},
    {QStringLiteral("kp_9"), QStringLiteral("Numpad9")},
    {QStringLiteral("kp_add"), QStringLiteral("NumpadAdd")},
    {QStringLiteral("kp_subtract"), QStringLiteral("NumpadSubtract")},
    {QStringLiteral("kp_multiply"), QStringLiteral("NumpadMultiply")},
    {QStringLiteral("kp_divide"), QStringLiteral("NumpadDivide")},
    {QStringLiteral("kp_decimal"), QStringLiteral("NumpadDecimal")},
    {QStringLiteral("kp_enter"), QStringLiteral("NumpadEnter")},
};
static const QHash<QString, QString> friendly{
    {QStringLiteral("Slash"), QStringLiteral("/")},
    {QStringLiteral("Minus"), QStringLiteral("-")},
    {QStringLiteral("Equal"), QStringLiteral("=")},
    {QStringLiteral("Comma"), QStringLiteral(",")},
    {QStringLiteral("Period"), QStringLiteral(".")},
    {QStringLiteral("Semicolon"), QStringLiteral(";")},
    {QStringLiteral("Quote"), QStringLiteral("'")},
    {QStringLiteral("Backquote"), QStringLiteral("`")},
    {QStringLiteral("BracketLeft"), QStringLiteral("[")},
    {QStringLiteral("BracketRight"), QStringLiteral("]")},
    {QStringLiteral("Backslash"), QStringLiteral("\\")},
    {QStringLiteral("Up"), QStringLiteral("↑")},
    {QStringLiteral("Down"), QStringLiteral("↓")},
    {QStringLiteral("Left"), QStringLiteral("←")},
    {QStringLiteral("Right"), QStringLiteral("→")},
};
static const QHash<QString, QString> mac{
    {QStringLiteral("Ctrl"), QStringLiteral("⌃")},
    {QStringLiteral("Alt"), QStringLiteral("⌥")},
    {QStringLiteral("Shift"), QStringLiteral("⇧")},
    {QStringLiteral("Super"), QStringLiteral("⌘")},
    {QStringLiteral("Enter"), QStringLiteral("↩")},
    {QStringLiteral("Tab"), QStringLiteral("⇥")},
    {QStringLiteral("Escape"), QStringLiteral("⎋")},
    {QStringLiteral("Backspace"), QStringLiteral("⌫")},
    {QStringLiteral("Delete"), QStringLiteral("⌦")},
};

static QString key(const QString &input) {
  const auto lower = input.toLower();
  if (keys.contains(lower))
    return keys.value(lower);
  if (QRegularExpression(QStringLiteral("\\A[A-Za-z0-9]\\z"))
          .match(input)
          .hasMatch())
    return input.toUpper();
  auto match =
      QRegularExpression(QStringLiteral("\\Af([0-9]+)\\z")).match(lower);
  if (match.hasMatch()) {
    bool ok = false;
    const int n = match.captured(1).toInt(&ok);
    if (ok && n >= 1 && n <= 24)
      return QStringLiteral("F%1").arg(n);
  }
  match =
      QRegularExpression(QStringLiteral("\\Anumpad([0-9])\\z")).match(lower);
  if (match.hasMatch())
    return QStringLiteral("Numpad") + match.captured(1);
  match =
      QRegularExpression(QStringLiteral("\\Acode:?([0-9]+)\\z")).match(lower);
  if (match.hasMatch()) {
    QString digits = match.captured(1);
    while (digits.size() > 1 && digits.startsWith(QLatin1Char('0')))
      digits.remove(0, 1);
    return QStringLiteral("Code") + digits;
  }
  return {};
}
QString normalize(const QString &input, QString *error) {
  if (error)
    error->clear();
  const auto fail = [&](const QString &message) {
    if (error)
      *error = message;
    return QString();
  };
  if (input.trimmed().isEmpty())
    return fail(QStringLiteral("empty accelerator"));
  QStringList compact;
  for (const auto &part : input.trimmed().split(QLatin1Char('+')))
    compact << part.trimmed();
  const auto chords = compact.join(QLatin1Char('+'))
                          .split(QRegularExpression(QStringLiteral("\\s+")),
                                 Qt::SkipEmptyParts);
  QStringList result;
  const QStringList order{QStringLiteral("Ctrl"), QStringLiteral("Alt"),
                          QStringLiteral("Shift"), QStringLiteral("Super")};
  for (const auto &chord : chords) {
    QSet<QString> mods;
    QString main;
    const auto parts = chord.split(QLatin1Char('+'));
    for (int i = 0; i < parts.size(); ++i) {
      const auto lower = parts[i].toLower();
      if (modifiers.contains(lower)) {
        const auto mod = modifiers.value(lower);
        if (mods.contains(mod) &&
            !(i + 1 == parts.size() && main.isEmpty() &&
              (lower.endsWith(u"_l") || lower.endsWith(u"_r"))))
          return fail(QStringLiteral("repeated modifier"));
        mods.insert(mod);
      } else {
        if (!main.isEmpty())
          return fail(QStringLiteral("more than one key"));
        main = key(parts[i]);
        if (main.isEmpty())
          return fail(QStringLiteral("unknown key: %1").arg(parts[i]));
      }
    }
    QStringList out;
    for (const auto &mod : order)
      if (mods.contains(mod))
        out << mod;
    if (!main.isEmpty())
      out << main;
    else if (mods.size() != 1)
      return fail(QStringLiteral("a chord needs a key"));
    result << out.join(QLatin1Char('+'));
  }
  return result.join(QLatin1Char(' '));
}
bool isCanonical(const QString &input) {
  const auto value = normalize(input);
  return !value.isEmpty() && value == input;
}
QString display(const QString &input, Platform platform, QString *error) {
  const auto value = normalize(input, error);
  if (value.isEmpty())
    return {};
  QStringList result;
  for (const auto &chord : value.split(QLatin1Char(' '))) {
    QStringList out;
    for (const auto &part : chord.split(QLatin1Char('+'))) {
      if (platform == Platform::Macos && mac.contains(part))
        out << mac.value(part);
      else if (platform == Platform::Windows && part == u"Super")
        out << QStringLiteral("Win");
      else
        out << friendly.value(part, part);
    }
    result << out.join(platform == Platform::Macos ? QString()
                                                   : QStringLiteral("+"));
  }
  return result.join(QLatin1Char(' '));
}
bool conflicts(const QString &a, const QString &b, QString *error) {
  const auto first = normalize(a, error);
  if (first.isEmpty())
    return false;
  const auto second = normalize(b, error);
  return !second.isEmpty() &&
         (first == second || first.startsWith(second + QLatin1Char(' ')) ||
          second.startsWith(first + QLatin1Char(' ')));
}
} // namespace ArcadeLink::Accelerator
