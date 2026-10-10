#include "Backdrop.h"
#include "Backdrop_p.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QTimer>
#include <QWindow>
#include <QtMath>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace Shelf {

#ifdef Q_OS_WIN
namespace {
// Numeric values keep builds with older Windows SDKs compatible. The official
// DWMWA_SYSTEM_BACKDROP_TYPE contract starts at Windows 11 build 22621.
constexpr DWORD SystemBackdropAttribute = 38;
constexpr DWORD ImmersiveDarkAttribute = 20;
constexpr DWORD TransientWindowBackdrop = 3;

class WindowsBackdrop final : public NativeBackdrop {
public:
    ~WindowsBackdrop() override { clear(); }
    bool apply(QWindow *window, const QRectF &region, qreal, bool dark,
               QString *status) override
    {
        if (QGuiApplication::platformName() != QStringLiteral("windows")) {
            clear(); *status = QStringLiteral("Native material unavailable on this display backend"); return false;
        }
        const HWND handle = reinterpret_cast<HWND>(window->winId());
        if (m_handle != handle) clear();
        BOOL composition = FALSE;
        HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
        DWORD transparency = 1, size = sizeof(transparency);
        const bool transparencyOff = RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size) == ERROR_SUCCESS
            && transparency == 0;
        if (!handle || !IsWindow(handle) || FAILED(DwmIsCompositionEnabled(&composition)) || !composition
            || transparencyOff || (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
                                  && (contrast.dwFlags & HCF_HIGHCONTRASTON))) {
            clear(); *status = QStringLiteral("System material disabled by Windows display or accessibility settings"); return false;
        }
        // Qt 6 sets WS_EX_LAYERED on frameless alpha windows. Stripping that
        // style would break Qt's capsule transparency. The official system
        // backdrop is window-wide, not an arbitrary rounded client subregion.
        if ((GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_LAYERED)
            || region != QRectF(QPointF(), window->size())) {
            clear(); *status = QStringLiteral("Transparent capsule uses a solid tint; Windows system material requires a compatible full-window surface"); return false;
        }
        if (!m_handle) {
            m_handle = handle;
            m_havePrevious = SUCCEEDED(DwmGetWindowAttribute(handle, SystemBackdropAttribute,
                &m_previous, sizeof(m_previous)));
            m_haveDark = SUCCEEDED(DwmGetWindowAttribute(handle, ImmersiveDarkAttribute,
                &m_previousDark, sizeof(m_previousDark)));
        }
        const HRESULT applied = DwmSetWindowAttribute(handle, SystemBackdropAttribute,
            &TransientWindowBackdrop, sizeof(TransientWindowBackdrop));
        if (FAILED(applied)) {
            clear(); *status = QStringLiteral("Windows 11 system material unavailable; using a solid tint"); return false;
        }
        m_applied = true;
        const BOOL useDark = dark;
        m_darkApplied = SUCCEEDED(DwmSetWindowAttribute(handle, ImmersiveDarkAttribute, &useDark, sizeof(useDark)));
        *status = QStringLiteral("Windows system material");
        return true;
    }
    void clear() override
    {
        if (m_handle && IsWindow(m_handle)) {
            if (m_applied) {
                const DWORD previous = m_havePrevious ? m_previous : 1; // DWSBT_NONE
                DwmSetWindowAttribute(m_handle, SystemBackdropAttribute, &previous, sizeof(previous));
            }
            if (m_darkApplied && m_haveDark)
                DwmSetWindowAttribute(m_handle, ImmersiveDarkAttribute, &m_previousDark, sizeof(m_previousDark));
        }
        m_handle = nullptr; m_applied = m_darkApplied = m_havePrevious = m_haveDark = false;
    }
private:
    HWND m_handle = nullptr;
    DWORD m_previous = 1;
    BOOL m_previousDark = FALSE;
    bool m_applied = false, m_darkApplied = false, m_havePrevious = false, m_haveDark = false;
};
} // namespace

std::unique_ptr<NativeBackdrop> makeNativeBackdrop(QObject *, std::function<void()>)
{
    return std::make_unique<WindowsBackdrop>();
}
#elif !defined(Q_OS_MACOS)
namespace {
class CompositorBackdrop final : public NativeBackdrop {
public:
    bool apply(QWindow *, const QRectF &, qreal, bool, QString *status) override
    {
        *status = QStringLiteral("Blur is controlled by the desktop compositor; Shelf uses a translucent tint");
        return false; // Do not claim an unverified compositor effect is active.
    }
    void clear() override {}
};
} // namespace

std::unique_ptr<NativeBackdrop> makeNativeBackdrop(QObject *, std::function<void()>)
{
    return std::make_unique<CompositorBackdrop>();
}
#endif

Backdrop::Backdrop(QObject *parent) : QObject(parent)
{
    m_native = makeNativeBackdrop(this, [this] { scheduleRefresh(); });
#ifdef Q_OS_WIN
    if (auto *app = QCoreApplication::instance()) app->installNativeEventFilter(this);
#endif
}

Backdrop::~Backdrop()
{
#ifdef Q_OS_WIN
    if (auto *app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
#endif
    if (m_window) m_window->removeEventFilter(this);
    m_native->clear();
}

void Backdrop::attachWindow(QWindow *window)
{
    if (m_window == window) { scheduleRefresh(); return; }
    for (const auto &connection : m_connections) QObject::disconnect(connection);
    m_connections.clear();
    if (m_window) m_window->removeEventFilter(this);
    m_native->clear();
    m_window = window;
    m_surfaceReady = window && window->handle();
    if (window) {
        window->installEventFilter(this);
        m_connections.push_back(connect(window, &QWindow::destroyed, this, [this] {
            m_native->clear(); m_window = nullptr; m_surfaceReady = false;
            setState(false, QStringLiteral("No desktop surface"));
        }));
        m_connections.push_back(connect(window, &QWindow::visibleChanged, this, [this] { scheduleRefresh(); }));
        m_connections.push_back(connect(window, &QWindow::screenChanged, this, [this] { scheduleRefresh(); }));
    }
    refresh();
}

void Backdrop::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    const bool previousActive = m_active;
    const QString previousStatus = m_status;
    m_enabled = enabled; refresh();
    if (previousActive == m_active && previousStatus == m_status) emit changed();
}

void Backdrop::setDark(bool dark)
{
    if (m_dark == dark) return;
    m_dark = dark; scheduleRefresh();
}

void Backdrop::setRegion(const QRectF &region, qreal radius)
{
    if (m_region == region && m_radius == radius) return;
    m_region = region; m_radius = radius; scheduleRefresh();
}

QRectF Backdrop::boundedRegion(const QRectF &region, const QSize &windowSize)
{
    const QRectF bounds(QPointF(), windowSize);
    if (region.isNull()) return bounds;
    if (!qIsFinite(region.x()) || !qIsFinite(region.y()) || !qIsFinite(region.width())
        || !qIsFinite(region.height()) || region.width() <= 0 || region.height() <= 0) return {};
    return region.intersected(bounds);
}

qreal Backdrop::boundedRadius(qreal radius, const QRectF &region)
{
    if (region.isEmpty()) return 0;
    return qIsFinite(radius) ? qBound(qreal(0), radius, qMin(region.width(), region.height()) / 2) : 0;
}

void Backdrop::setState(bool active, const QString &status)
{
    if (m_active == active && m_status == status) return;
    m_active = active; m_status = status; emit changed();
}

void Backdrop::scheduleRefresh()
{
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] { m_refreshQueued = false; refresh(); });
}

void Backdrop::refresh()
{
    if (!m_enabled || !m_window || !m_surfaceReady || !m_window->isVisible()) {
        m_native->clear();
        setState(false, !m_enabled ? QStringLiteral("Native material disabled") : QStringLiteral("No visible desktop surface"));
        return;
    }
    const QRectF region = boundedRegion(m_region, m_window->size());
    if (region.isEmpty()) { m_native->clear(); setState(false, QStringLiteral("No visible material region")); return; }
    QString status;
    const bool applied = m_native->apply(m_window, region, boundedRadius(m_radius, region), m_dark, &status);
    setState(applied, status);
}

bool Backdrop::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window) {
        if (event->type() == QEvent::PlatformSurface) {
            auto *surface = static_cast<QPlatformSurfaceEvent *>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
                m_surfaceReady = false; m_native->clear();
                setState(false, QStringLiteral("No desktop surface"));
            } else { m_surfaceReady = true; scheduleRefresh(); }
        } else if (event->type() == QEvent::Show) {
            m_surfaceReady = bool(m_window->handle()); scheduleRefresh();
        } else if (event->type() == QEvent::Resize || event->type() == QEvent::ThemeChange
                   || event->type() == QEvent::WinIdChange) scheduleRefresh();
    }
    return QObject::eventFilter(watched, event);
}

bool Backdrop::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
#ifdef Q_OS_WIN
    const auto *event = static_cast<MSG *>(message);
    if (event && (event->message == WM_SETTINGCHANGE || event->message == WM_THEMECHANGED
                  || event->message == WM_DWMCOMPOSITIONCHANGED)) scheduleRefresh();
#else
    Q_UNUSED(message);
#endif
    return false;
}

} // namespace Shelf
