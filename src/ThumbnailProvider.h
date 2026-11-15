#pragma once
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <memory>
namespace Shelf {
struct ThumbnailCache;
class ThumbnailProvider final : public QQuickAsyncImageProvider {
public:
    // The decoded thumbnails kept for the next request (SH1): 16 MiB unless the
    // user's preference says otherwise, and never more than 64 MiB.
    static constexpr qint64 DefaultCacheBytes = 16LL * 1024 * 1024;
    static constexpr qint64 MaxCacheBytes = 64LL * 1024 * 1024;
    ThumbnailProvider();
    ~ThumbnailProvider() override;
    // Sets the cache size, kept between 0 (no cache) and MaxCacheBytes.
    void setCacheLimitBytes(qint64 bytes);
    qint64 cacheLimitBytes() const;
    // Drops every cached thumbnail (the panel was hidden); requests decode again.
    void clearCache();
    qint64 cachedBytes() const;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    std::shared_ptr<ThumbnailCache> m_cache;
    QThreadPool m_pool;
};
} // namespace Shelf
