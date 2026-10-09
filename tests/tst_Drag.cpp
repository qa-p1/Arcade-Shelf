// Native ordinary-window Qt Quick drag-out probe. Uses two OS processes and
// actual X11 pointer events. Wayland requires compositor-specific input tooling
// and is explicitly skipped; in-process QtTest events are not a valid substitute.
#include "platform/Desktop.h"
#include <QApplication>
#include <QDrag>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>
#include <iostream>

class DropRecorder : public QObject {
    Q_OBJECT
public:
    QString output;
    Q_INVOKABLE void record(const QVariantList &urls, const QString &text) {
        QJsonArray paths;
        for (auto value : urls) {
            while (value.metaType().id() == QMetaType::QVariant) value = value.value<QVariant>();
            paths.append(value.toUrl().toLocalFile());
        }
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly)) { QCoreApplication::exit(2); return; }
        file.write(QJsonDocument(QJsonObject{{"paths", paths}, {"text", text}}).toJson());
        file.close();
        QTimer::singleShot(50, qApp, [] { QCoreApplication::quit(); });
    }
};

class NativeDragWindow : public QQuickWindow {
public:
    QJsonArray contents;
    Shelf::Desktop desktop;
    NativeDragWindow() {
        QObject::connect(&desktop, &Shelf::Desktop::dragFinished, this,
                         [this](int action) { completed = Qt::DropAction(action); });
    }
    Qt::DropAction completed = Qt::IgnoreAction;
    bool started = false;
protected:
    void mousePressEvent(QMouseEvent *event) override {
        m_press = event->position().toPoint();
        m_armed = event->button() == Qt::LeftButton;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_armed && (event->buttons() & Qt::LeftButton) &&
            (event->position().toPoint() - m_press).manhattanLength() >= QApplication::startDragDistance()) {
            m_armed = false;
            started = true;
            desktop.startDrag(this, contents);
        }
        event->accept();
    }
private:
    bool m_armed = false;
    QPoint m_press;
};

class DragTest : public QObject {
    Q_OBJECT
private slots:
    void nativeUriListAcrossProcesses() {
        if (QGuiApplication::platformName() != QStringLiteral("xcb"))
            QSKIP("Native pointer injection probe requires X11/xcb; validate Wayland with compositor input tools.");
        const auto xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
        if (xdotool.isEmpty()) QSKIP("Install xdotool for native pointer-event DnD coverage.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto sourcePath = directory.filePath(QStringLiteral("reference with spaces.txt"));
        const auto folderPath = directory.filePath(QStringLiteral("folder"));
        const auto outputPath = directory.filePath(QStringLiteral("received.json"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("original remains"), 16);
        source.close();
        QVERIFY(QDir().mkdir(folderPath));
        QProcess receiver;
        receiver.setProcessChannelMode(QProcess::MergedChannels);
        receiver.start(QCoreApplication::applicationFilePath(), {"--receiver", outputPath});
        QVERIFY(receiver.waitForStarted(3000));
        QByteArray receiverOutput;
        QElapsedTimer ready;
        ready.start();
        while (!receiverOutput.contains("READY") && ready.elapsed() < 3000) {
            receiver.waitForReadyRead(50);
            receiverOutput += receiver.readAll();
            QCoreApplication::processEvents();
        }
        QVERIFY2(receiverOutput.contains("READY"), receiverOutput.constData());
        NativeDragWindow sender;
        sender.contents = {QJsonObject{{"type", "file/text"}, {"path", sourcePath}},
                           QJsonObject{{"type", "folder/reference"}, {"path", folderPath}}};
        sender.setTitle(QStringLiteral("Shelf native drag probe"));
        sender.setGeometry(100, 100, 300, 200);
        sender.setColor(QColor(QStringLiteral("#5938bd")));
        sender.show();
        QVERIFY(QTest::qWaitForWindowExposed(&sender));
        QProcess pointer;
        pointer.start(xdotool, {"mousemove", "180", "170", "mousedown", "1", "sleep", "0.15",
                               "mousemove", "230", "170", "sleep", "0.15", "mousemove", "480", "170",
                               "sleep", "0.15", "mousemove", "530", "170", "sleep", "0.15", "mouseup", "1"});
        QVERIFY(pointer.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(receiver.state() == QProcess::NotRunning, 6000);
        if (pointer.state() != QProcess::NotRunning)
            QVERIFY2(pointer.waitForFinished(3000), pointer.errorString().toUtf8().constData());
        QCOMPARE(pointer.exitCode(), 0);
        QCOMPARE(receiver.exitCode(), 0);
        QVERIFY(sender.started);
        QTRY_COMPARE_WITH_TIMEOUT(sender.completed, Qt::CopyAction, 3000);
        QFile result(outputPath);
        QVERIFY(result.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(result.readAll()).object();
        QCOMPARE(object["paths"].toArray(), QJsonArray({sourcePath, folderPath}));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("original remains"));
        QVERIFY(QFileInfo(folderPath).isDir());
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == QStringLiteral("--receiver")) {
        DropRecorder recorder;
        recorder.output = app.arguments()[2];
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("recorder"), &recorder);
        engine.loadData(R"QML(
            import QtQuick
            Window {
                title: "Shelf native drop probe"
                x: 450; y: 100; width: 300; height: 200; visible: true
                color: "#23875e"
                DropArea {
                    anchors.fill: parent
                    onDropped: function(drop) {
                        recorder.record(drop.urls, drop.text)
                        drop.acceptProposedAction()
                    }
                }
            }
        )QML");
        if (engine.rootObjects().isEmpty()) return 3;
        QTimer::singleShot(10000, &app, [] { QCoreApplication::exit(4); });
        std::cout << "READY\n" << std::flush;
        return app.exec();
    }
    DragTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_Drag.moc"
