#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVideoSink>
#include <QtTest>

#include "backend.h"

class AircastTests : public QObject {
    Q_OBJECT

private slots:
    // Keeps the receiver's key out of the user's real data directory.
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void volumeMapsDecibelsToGain()
    {
        QCOMPARE(Backend::linearVolume(0.0f), 1.0);
        QCOMPARE(Backend::linearVolume(-30.0f), 0.0);
        QCOMPARE(Backend::linearVolume(-144.0f), 0.0);
        QVERIFY(qAbs(Backend::linearVolume(-20.0f) - 0.1) < 1e-9);
    }

    // The path a phone's mirror stream takes: H.264 access units in, decoded
    // frames out on the window's video sink.
    void decodesMirrorVideoIntoTheSink()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("clip.h264");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc=size=390x844:rate=30", "-frames:v", "30",
                                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-x264-params", "aud=1:bframes=0",
                                "-bsf:v", "h264_mp4toannexb", "-f", "h264", file});
        QVERIFY(ffmpeg.waitForFinished(20000));
        QFile clip(file);
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray stream = clip.readAll(), delimiter = QByteArray::fromHex("0000000109");
        QVERIFY(stream.startsWith(delimiter));

        Backend backend;
        QVideoSink sink;
        backend.setVideoSink(&sink);
        int frames = 0;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &frame) { frames += frame.isValid(); });

        for (qsizetype start = 0; start < stream.size();) {
            qsizetype end = stream.indexOf(delimiter, start + 1);
            if (end < 0)
                end = stream.size();
            backend.pushVideo(reinterpret_cast<const unsigned char *>(stream.constData()) + start, int(end - start));
            start = end;
        }
        QTRY_VERIFY2_WITH_TIMEOUT(frames > 5, qPrintable(backend.error()), 10000);
        QCOMPARE(backend.videoSize(), QSize(390, 844));
    }

    void announcesAndWithdraws()
    {
        Backend backend;
        backend.setVisible(true);
        QVERIFY2(backend.visible(), qPrintable(backend.error()));
        QCOMPARE(backend.phase(), QString("idle"));
        backend.disconnectClient();
        QVERIFY(backend.visible());
        backend.setVisible(false);
        QVERIFY(!backend.visible());
    }
};

QTEST_MAIN(AircastTests)
#include "aircast_tests.moc"
