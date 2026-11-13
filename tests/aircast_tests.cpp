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
    void decodesMirrorVideoIntoTheSink_data()
    {
        QTest::addColumn<QString>("encoder");
        QTest::addColumn<QString>("format");
        QTest::addColumn<QByteArray>("delimiter");
        QTest::newRow("h264") << "libx264" << "h264" << QByteArray::fromHex("0000000109");
        QTest::newRow("h265") << "libx265" << "hevc" << QByteArray::fromHex("000000014601");
    }

    void decodesMirrorVideoIntoTheSink()
    {
        QFETCH(QString, encoder);
        QFETCH(QString, format);
        QFETCH(QByteArray, delimiter);
        QTemporaryDir dir;
        const QString file = dir.filePath("clip.h264");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc=size=390x844:rate=30", "-frames:v", "30",
                                "-c:v", encoder, "-pix_fmt", "yuv420p", "-x264-params", "aud=1:bframes=0",
                                "-x265-params", "aud=1:bframes=0:log-level=none", "-f", format, file});
        QVERIFY(ffmpeg.waitForFinished(20000));
        QFile clip(file);
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray stream = clip.readAll();
        QVERIFY(stream.startsWith(delimiter));

        Backend backend;
        backend.h265 = format == "hevc";
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

    void blocksIdleWhileAsked()
    {
        auto locks = [] {
            QProcess list;
            list.start("systemd-inhibit", {"--list", "--no-pager"});
            list.waitForFinished(5000);
            return list.readAllStandardOutput();
        };
        Backend backend;
        backend.setIdleInhibited(true);
        QVERIFY(locks().contains("AirCast"));
        backend.setIdleInhibited(false);
        QVERIFY(!locks().contains("AirCast"));
    }

    void announcesAndWithdraws()
    {
        // Two receivers can't announce the same identity on one machine.
        if (QProcess::execute("pgrep", {"-x", "aircast"}) == 0)
            QSKIP("AirCast is already running on this machine");
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
