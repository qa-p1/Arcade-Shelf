#pragma once
#include <QQuickAsyncImageProvider>
namespace Shelf {
class ThumbnailProvider final : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
}
