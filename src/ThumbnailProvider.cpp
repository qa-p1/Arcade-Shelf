#include "ThumbnailProvider.h"
#include <QCache>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <QDateTime>
#include <atomic>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Shelf {
struct ThumbnailCache {
    QMutex mutex;
    QCache<QString,QImage> images{64 * 1024}; // Costs in KiB.
};
namespace {
class Response final : public QQuickImageResponse, public QRunnable {
public:
    Response(QString path, QSize size, std::shared_ptr<ThumbnailCache> cache)
        : m_path(std::move(path)), m_size(size), m_cache(std::move(cache)) { setAutoDelete(false); }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
    void cancel() override { m_cancelled.store(true); }
    void run() override {
        if (m_cancelled.load()) { emit finished(); return; }
        const QFileInfo info(m_path);
        if (!info.isFile() || !info.isReadable()) { m_error = "Thumbnail unavailable"; emit finished(); return; }
        // NUL cannot occur in a local path. Delimit fields to avoid collisions
        // between a numeric filename suffix, timestamps and dimensions.
        const QString separator(QChar(0));
        const QString key = info.canonicalFilePath() + separator
            + QString::number(info.lastModified().toMSecsSinceEpoch()) + separator
            + QString::number(info.size()) + separator
            + QString::number(m_size.width()) + "x" + QString::number(m_size.height());
        {
            QMutexLocker lock(&m_cache->mutex);
            if (const auto *hit = m_cache->images.object(key)) m_image = *hit;
        }
        if (m_image.isNull()) {
            QFile source(m_path);
#ifdef Q_OS_UNIX
            // A previously staged file can be replaced by a FIFO. Nonblocking
            // open plus fstat prevents it from hanging either decoder worker.
            const int fd = ::open(QFile::encodeName(m_path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
            struct stat state{};
            if (fd < 0 || ::fstat(fd,&state) != 0 || !S_ISREG(state.st_mode)) {
                if (fd >= 0) ::close(fd);
                m_error = "Thumbnail unavailable"; emit finished(); return;
            }
            if (!source.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) {
                ::close(fd); m_error = "Thumbnail unavailable"; emit finished(); return;
            }
#else
            if (!source.open(QIODevice::ReadOnly)) { m_error = "Thumbnail unavailable"; emit finished(); return; }
#endif
            QImageReader reader(&source); reader.setAutoTransform(true);
            const QSize original = reader.size();
            if (original.isValid() && qint64(original.width()) * original.height() <= 100000000) {
                reader.setScaledSize(original.scaled(m_size, Qt::KeepAspectRatio));
                m_image = reader.read();
            }
            if (m_cancelled.load()) { m_image = {}; emit finished(); return; }
            if (m_image.isNull()) m_error = "Thumbnail unavailable";
            else {
                // Auto orientation or a format plugin can change the decoded
                // dimensions. Enforce the response/cache bounds afterwards too.
                if (m_image.width() > m_size.width() || m_image.height() > m_size.height())
                    m_image = m_image.scaled(m_size,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                QMutexLocker lock(&m_cache->mutex);
                if (m_cache->images.maxCost() > 0)
                    m_cache->images.insert(key,new QImage(m_image),int((m_image.sizeInBytes()+1023)/1024));
            }
        }
        if (m_cancelled.load()) m_image = {};
        emit finished();
    }
private:
    QString m_path, m_error;
    QSize m_size;
    QImage m_image;
    std::shared_ptr<ThumbnailCache> m_cache;
    std::atomic_bool m_cancelled{false};
};
}
ThumbnailProvider::ThumbnailProvider() : m_cache(std::make_shared<ThumbnailCache>())
{
    // Decode work must not grow with the desktop's CPU count or compete with
    // unrelated Qt jobs. Threads are created lazily and expire when unused.
    m_pool.setMaxThreadCount(2);
    m_pool.setExpiryTimeout(5000);
}
ThumbnailProvider::~ThumbnailProvider() { m_pool.waitForDone(); }
void ThumbnailProvider::setCacheLimitBytes(qint64 bytes)
{
    const int kib = int(qBound<qint64>(0,bytes,1024LL * 1024 * 1024) / 1024);
    QMutexLocker lock(&m_cache->mutex);
    if (m_cache->images.maxCost() != kib) m_cache->images.setMaxCost(kib);
}
QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QSize size = !requestedSize.isEmpty() ? requestedSize : QSize(320,240);
    size = size.boundedTo(QSize(640,480));
    auto *response = new Response(QUrl::fromPercentEncoding(id.toUtf8()),size,m_cache);
    m_pool.start(response);
    return response;
}
}
