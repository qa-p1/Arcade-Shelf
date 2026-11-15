// arcade_trim() (qt/arcade_trim.h), the C twin of arcade_link::runtime::trim: clean read-only file data is dropped
// and refaults intact; a read-only mapping that was written first (RELRO, text relocations) is never reverted.
#include "arcade_trim.h"
#include <QTemporaryDir>
#include <QtTest>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

class TrimTest : public QObject {
  Q_OBJECT
#if defined(__linux__)
  QTemporaryDir m_dir;

  unsigned char *mapFile(const char *name, size_t len, int prot) {
    const QByteArray path = m_dir.filePath(QString::fromLatin1(name)).toLocal8Bit();
    QFile file(QString::fromLocal8Bit(path));
    if (!file.open(QIODevice::WriteOnly))
      return nullptr;
    file.write(QByteArray(qsizetype(len), char(7)));
    file.close();
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    void *ptr = mmap(nullptr, len, prot, MAP_PRIVATE, fd, 0);
    ::close(fd);
    return ptr == MAP_FAILED ? nullptr : static_cast<unsigned char *>(ptr);
  }

  static long residentKb(const void *addr) {
    QFile smaps(QStringLiteral("/proc/self/smaps"));
    if (!smaps.open(QIODevice::ReadOnly))
      return -1;
    const QByteArray header = QByteArray::number(quintptr(addr), 16) + '-';
    bool inside = false;
    for (const auto &line : smaps.readAll().split('\n')) {
      if (line.startsWith(header))
        inside = true;
      else if (inside && line.startsWith("Rss:"))
        return line.simplified().split(' ').value(1).toLong();
    }
    return -1;
  }
#endif

private slots:
  void cleanReadOnlyDataIsDroppedAndRefaultsIntact() {
#if defined(__linux__)
    const size_t len = 4u << 20;
    auto *ptr = mapFile("rodata", len, PROT_READ);
    QVERIFY(ptr);
    unsigned long sum = 0;
    for (size_t i = 0; i < len; i += 4096)
      sum += ptr[i];
    QCOMPARE(sum, 7ul * (len / 4096));
    QVERIFY(residentKb(ptr) >= 4096);
    arcade_trim();
    QCOMPARE(residentKb(ptr), 0L);
    QCOMPARE(int(ptr[len - 1]), 7);
#else
    QSKIP("Linux only");
#endif
  }

  void writtenReadOnlyMappingsAreNeverReverted() {
#if defined(__linux__)
    const size_t len = 64u << 10;
    const struct {
      const char *name;
      int prot;
    } cases[] = {{"relro", PROT_READ}, {"textrel", PROT_READ | PROT_EXEC}};
    for (const auto &c : cases) {
      auto *ptr = mapFile(c.name, len, PROT_READ | PROT_WRITE);
      QVERIFY(ptr);
      for (size_t i = 0; i < len; i += 4096)
        ptr[i] = 42;
      QCOMPARE(mprotect(ptr, len, c.prot), 0);
      arcade_trim();
      for (size_t i = 0; i < len; i += 4096)
        QVERIFY2(ptr[i] == 42, c.name);
    }
#else
    QSKIP("Linux only");
#endif
  }

  // W3b pages out every private mapping, written file pages included; nothing may change.
  void pagingOutKeepsEveryPrivateMappingIntact() {
#if defined(__linux__)
    const size_t len = 64u << 10;
    QByteArray heap(int(len), Qt::Uninitialized);
    for (int i = 0; i < heap.size(); ++i)
      heap[i] = char(i % 251);
    auto *relro = mapFile("pageout-relro", len, PROT_READ | PROT_WRITE);
    auto *data = mapFile("pageout-data", len, PROT_READ | PROT_WRITE);
    QVERIFY(relro && data);
    for (size_t i = 0; i < len; i += 512) {
      relro[i] = 7;
      data[i] = 9;
    }
    QCOMPARE(mprotect(relro, len, PROT_READ), 0);
    arcade_page_out_hidden();
    for (size_t i = 0; i < len; i += 512) {
      QVERIFY2(relro[i] == 7, "RELRO-like page changed");
      QVERIFY2(data[i] == 9, ".data-like page changed");
    }
    for (int i = 0; i < heap.size(); ++i)
      QVERIFY2(heap[i] == char(i % 251), "heap changed");
#else
    QSKIP("Linux only");
#endif
  }
};
QTEST_GUILESS_MAIN(TrimTest)
#include "tst_Trim.moc"
