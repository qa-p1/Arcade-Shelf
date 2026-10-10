#pragma once

#include <QRectF>
#include <QString>
#include <functional>
#include <memory>

class QObject;
class QWindow;

namespace Shelf {

class NativeBackdrop {
public:
    virtual ~NativeBackdrop() = default;
    virtual bool apply(QWindow *window, const QRectF &region, qreal radius,
                       bool dark, QString *status) = 0;
    virtual void clear() = 0;
};

std::unique_ptr<NativeBackdrop> makeNativeBackdrop(QObject *context,
                                                 std::function<void()> refresh);

} // namespace Shelf
