#include "platform/Desktop.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
#include <memory>

using Shelf::Desktop;

class DesktopTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("ARCADE_SHELF_TESTING", "1");
    }

    void nativeUriListPreservesPathsAndInputOrder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first = directory.filePath(QStringLiteral("résumé #1.png"));
        const QString second = directory.filePath(QStringLiteral("two.txt"));
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("original"), qint64(8));
        }
        const QJsonArray inputs{
            QJsonObject{{"type", "file/any[]"}, {"paths", QJsonArray{first, second}}},
            QJsonObject{{"type", "folder/reference"}, {"path", directory.path()}},
            QJsonObject{{"type", "file/image"}, {"path", first}}};
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents(inputs));
        QVERIFY(mime->hasFormat(QStringLiteral("text/uri-list")));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second),
                                         QUrl::fromLocalFile(directory.path())}));
        QVERIFY(mime->data(QStringLiteral("text/uri-list")).contains("%23"));
        QCOMPARE(mime->text(), QStringList({first, second, directory.path()}).join(QLatin1Char('\n')));
        QFile original(first);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));
    }

    void richTextAndUrlsUseNativeFormats()
    {
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "text/rich"}, {"text", "Hello"}, {"html", "<b>Hello</b>"}},
            QJsonObject{{"type", "text/url"}, {"text", "https://example.com/a?q=one"}},
            QJsonObject{{"type", "text/plain"}, {"text", "last"}}}));
        QCOMPARE(mime->text(), QStringLiteral("Hello\nhttps://example.com/a?q=one\nlast"));
        QCOMPARE(mime->html(), QStringLiteral("<b>Hello</b>"));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl(QStringLiteral("https://example.com/a?q=one"))}));
    }

    void reorderMarkerCoexistsWithNativeExternalTransfer()
    {
        QTemporaryDir directory;
        const QJsonObject marker{{"profile", "profile-hash"}, {"ids", QJsonArray{"it_a", "it_b"}}};
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "folder/reference"}, {"path", directory.path()}},
            QJsonObject{{"type", "structured/shelf-reorder"}, {"data", marker}}}));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(directory.path())}));
        QCOMPARE(QJsonDocument::fromJson(mime->data(QStringLiteral("application/x-arcade-shelf-items"))).object(), marker);
        std::unique_ptr<QMimeData> invalid(Desktop::mimeDataForContents({
            QJsonObject{{"type", "structured/shelf-reorder"}, {"data", QJsonObject{{"profile", "hash"}, {"ids", QJsonArray{123}}}}}}));
        QVERIFY(invalid->formats().isEmpty());
    }

    void symlinkDragPreservesLinkPath()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("target.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString symlink = directory.filePath(QStringLiteral("reference.txt"));
        QVERIFY(QFile::link(file.fileName(), symlink));
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "file/text"}, {"path", symlink}}}));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(symlink)}));
#else
        QSKIP("QFile::link creates shortcuts, not POSIX symlinks, on Windows");
#endif
    }

    void invalidAndMissingReferencesAreExcluded()
    {
        QTemporaryDir directory;
        const QString embeddedNul = directory.path() + QChar::Null + QStringLiteral("suffix");
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "file/any"}, {"path", "relative.txt"}},
            QJsonObject{{"type", "file/any"}, {"path", directory.filePath("missing.txt")}},
            QJsonObject{{"type", "file/any"}, {"path", embeddedNul}},
            QJsonObject{{"type", "structured/arcade-action"}, {"text", "execute-me"}}}));
        QVERIFY(mime->formats().isEmpty());
    }

    void stagedTextFileRemainsTransferable()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("note.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("A small staged note"), qint64(19));
        file.close();
        std::unique_ptr<QMimeData> text(Desktop::mimeDataForContents({
            QJsonObject{{"type", "text/plain"}, {"path", file.fileName()}}}));
        QCOMPARE(text->text(), QStringLiteral("A small staged note"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(300 * 1024, 'x')), qint64(300 * 1024));
        file.close();
        std::unique_ptr<QMimeData> large(Desktop::mimeDataForContents({
            QJsonObject{{"type", "text/plain"}, {"path", file.fileName()}}}));
        QCOMPARE(large->urls(), QList<QUrl>({QUrl::fromLocalFile(file.fileName())}));
        QCOMPARE(QFileInfo(file).size(), qint64(300 * 1024));
    }

    void geometryClampsAcrossNegativeMonitorCoordinates()
    {
        const QRect leftMonitor(-1920, -200, 1920, 1080);
        QCOMPARE(Desktop::clampGeometry(QRect(-2300, -500, 480, 400), leftMonitor),
                 QRect(-1920, -200, 480, 400));
        QCOMPARE(Desktop::clampGeometry(QRect(-100, 800, 480, 400), leftMonitor),
                 QRect(-480, 480, 480, 400));
        QCOMPARE(Desktop::clampGeometry(QRect(-3000, -3000, 6000, 6000), leftMonitor), leftMonitor);
        QCOMPARE(Desktop::clampGeometry(QRect(40, 40, 10, 10), QRect(0, 0, 100, 50)), QRect(0, 0, 100, 50));
    }

    void shortcutValidationProtectsStandardClipboardShortcuts()
    {
        for (const QString &shortcut : {QStringLiteral("Ctrl+C"), QStringLiteral("Ctrl+V"),
             QStringLiteral("Ctrl+X"), QStringLiteral("Meta+C"), QStringLiteral("Shift+Insert"),
             QStringLiteral("Ctrl+Insert")}) {
            QVERIFY2(!Desktop::validateShortcut(shortcut).isEmpty(), qPrintable(shortcut));
        }
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Ctrl+Alt+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Control+Option+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Super+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Cmd+Shift+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("F8")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QString()).isEmpty());
        QVERIFY(!Desktop::validateShortcut(QStringLiteral("S")).isEmpty());
        QVERIFY(!Desktop::validateShortcut(QStringLiteral("Ctrl+K, Ctrl+S")).isEmpty());
        QVERIFY(!Desktop::validateShortcut(QStringLiteral("Ctrl+NotAKey")).isEmpty());
    }

    void isolatedInstanceCannotInstallShortcutOrLoginEntry()
    {
        Desktop desktop;
        QSignalSpy activation(&desktop, &Desktop::activated);
        desktop.configureShortcut(QStringLiteral("Ctrl+Alt+S"), QStringLiteral("/usr/bin/arcade-shelf"));
        QVERIFY(desktop.shortcutStatus().contains(QStringLiteral("isolated")));
        QVERIFY(!desktop.setStartAtLogin(true, QStringLiteral("/usr/bin/arcade-shelf")));
        QVERIFY(desktop.lastError().contains(QStringLiteral("isolated")));
        QCOMPARE(activation.count(), 0);
    }
};

QTEST_MAIN(DesktopTest)
#include "tst_Desktop.moc"
