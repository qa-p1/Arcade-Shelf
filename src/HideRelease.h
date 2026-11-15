#pragma once
#include <QObject>
#include <QTimer>

namespace Shelf {
// How long Shelf stays closed before it gives back what the open panel used.
inline constexpr int ReleaseDelayMs = 2000;

// W0 and SH1: tells when a hide is final. Closing the panel starts one
// single-shot timer; opening it again before it fires cancels it, so a quick
// close and reopen releases nothing. Nothing runs while the panel is open or
// after the timer has fired: there is no periodic wakeup.
class HideRelease final : public QObject {
    Q_OBJECT
public:
    explicit HideRelease(int delayMs = ReleaseDelayMs, QObject *parent = nullptr);
    // The panel is shown (true) or hidden (false). Repeating the current state does nothing.
    void setOpen(bool open);
    bool pending() const { return m_timer.isActive(); }
signals:
    // The panel has stayed hidden for the whole delay.
    void released();

private:
    QTimer m_timer;
    bool m_open = false;
};
} // namespace Shelf
