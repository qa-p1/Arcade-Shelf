#include "ThumbnailProvider.h"
#include <QCache>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <QDateTime>

namespace Shelf {
namespace {
QMutex cacheMutex;
QCache<QString, QImage> cache(32 * 1024); // Costs in KiB, bounded to 32 MiB.
class Response final : public QQuickImageResponse, public QRunnable {
public:
    Response(QString path, QSize size) : m_path(std::move(path)), m_size(size) { setAutoDelete(false); }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
    void cancel() override { m_cancelled.store(true); }
    void run() override {
        if (m_cancelled.load()) { emit finished(); return; }
        const QFileInfo info(m_path);
        const QString key = m_path + QString::number(info.lastModified().toMSecsSinceEpoch()) + QString::number(info.size()) + QString::number(m_size.width()) + "x" + QString::number(m_size.height());
        {
            QMutexLocker lock(&cacheMutex);
            if (const auto *hit = cache.object(key)) m_image = *hit;
        }
        if (m_image.isNull()) {
            QImageReader reader(m_path); reader.setAutoTransform(true);
            const QSize original = reader.size();
            if (original.isValid() && qint64(original.width()) * original.height() <= 100000000) {
                reader.setScaledSize(original.scaled(m_size, Qt::KeepAspectRatio));
                m_image = reader.read();
            }
            if (m_image.isNull()) m_error = "Thumbnail unavailable";
            else { QMutexLocker lock(&cacheMutex); cache.insert(key,new QImage(m_image),int((m_image.sizeInBytes()+1023)/1024)); }
        }
        emit finished();
    }
private:
    QString m_path, m_error;
    QSize m_size;
    QImage m_image;
    std::atomic_bool m_cancelled{false};
};
}
QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QSize size = requestedSize.isValid() ? requestedSize : QSize(320,240);
    size = size.boundedTo(QSize(640,480));
    auto *response = new Response(QUrl::fromPercentEncoding(id.toUtf8()),size);
    QThreadPool::globalInstance()->start(response);
    return response;
}
}
