#include "HideRelease.h"

namespace Shelf {
HideRelease::HideRelease(int delayMs, QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(delayMs);
    connect(&m_timer, &QTimer::timeout, this, &HideRelease::released);
}

void HideRelease::setOpen(bool open)
{
    if (open == m_open) return;
    m_open = open;
    if (open) m_timer.stop();
    else m_timer.start();
}
} // namespace Shelf
