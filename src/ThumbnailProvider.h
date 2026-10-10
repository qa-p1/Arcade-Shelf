#pragma once
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <memory>
namespace Shelf {
struct ThumbnailCache;
class ThumbnailProvider final : public QQuickAsyncImageProvider {
public:
    ThumbnailProvider();
    ~ThumbnailProvider() override;
    void setCacheLimitBytes(qint64 bytes);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
private:
    std::shared_ptr<ThumbnailCache> m_cache;
    QThreadPool m_pool;
};
}
