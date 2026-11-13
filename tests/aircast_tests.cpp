#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVideoSink>
#include <QtTest>

#include "backend.h"
#include "remote.h"

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

    void asksOnceAboutTheFirewall()
    {
        QSettings("aircast", "aircast").clear();
        QTemporaryDir dir;
        const QString rules = dir.filePath("user.rules"), elevate = dir.filePath("elevate");
        QFile script(elevate);
        QVERIFY(script.open(QIODevice::WriteOnly));
        // Stands in for pkexec: records the command where ufw would write its rules.
        script.write("#!/bin/sh\necho \"$3\" | sed 's|ufw allow \\([0-9,]*\\)/tcp|allow tcp \\1|' > '" + rules.toUtf8() + "'\n");
        script.close();
        QFile::setPermissions(elevate, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        Backend backend(nullptr, rules, elevate);
        QCOMPARE(backend.firewall(), QString("ask"));
        backend.declineFirewall();
        QCOMPARE(backend.firewall(), QString("declined"));
        backend.allowFirewall();
        QTRY_COMPARE(backend.firewall(), QString("allowed"));
        QFile written(rules);
        QVERIFY(written.open(QIODevice::ReadOnly));
        const QByteArray command = written.readAll();
        QVERIFY2(command.contains("allow tcp 7000,7100 comment AirCast"), command.constData());
        QVERIFY(command.contains("ufw allow 6000,6001,7011/udp") && command.contains("ufw allow 5353/udp"));
        QSettings("aircast", "aircast").clear();
    }

    void mapsKeysToHidUsages()
    {
        quint8 modifier = 0;
        QCOMPARE(Remote::hidUsage(30, &modifier), 0x04); // A
        QCOMPARE(modifier, quint8(0));
        QCOMPARE(Remote::hidUsage(28, &modifier), 0x28); // Enter
        QCOMPARE(Remote::hidUsage(42, &modifier), 0);    // left Shift
        QCOMPARE(modifier, quint8(0x02));
        QCOMPARE(Remote::hidUsage(29, &modifier), 0);    // Ctrl goes out as Command
        QCOMPARE(modifier, quint8(0x08));
        QCOMPARE(Remote::hidUsage(999, &modifier), 0);
        QCOMPARE(modifier, quint8(0));
    }

    void buildsKeyboardAndMouseReports()
    {
        Remote remote(nullptr, false);
        QSignalSpy sent(&remote, &Remote::reportSent);
        auto last = [&] { return sent.last().at(1).toByteArray().toHex(); };

        remote.key(42, true);
        remote.key(30, true);
        QCOMPARE(last(), QByteArray("0200040000000000"));
        remote.key(30, false);
        remote.key(42, false);
        QCOMPARE(last(), QByteArray("0000000000000000"));

        remote.buttons(1);
        QCOMPARE(last(), QByteArray("01000000"));
        sent.clear();
        remote.move(300, -2.5); // too far for one report, and half a pixel left over
        QCOMPARE(sent.size(), 3);
        QCOMPARE(sent.at(0).at(1).toByteArray().toHex(), QByteArray("017ffe00"));
        QCOMPARE(last(), QByteArray("012e0000"));
        remote.move(0, -0.5);
        QCOMPARE(last(), QByteArray("0100ff00"));
        remote.scroll(-1);
        QCOMPARE(last(), QByteArray("010000ff"));

        remote.key(30, true);
        remote.releaseAll();
        QCOMPARE(last(), QByteArray("00000000"));
        QCOMPARE(sent.at(sent.size() - 2).at(1).toByteArray().toHex(), QByteArray("0000000000000000"));
    }

    void blocksIdleWhileAsked()
    {
        auto locks = [] {
            QProcess list;
            list.start("systemd-inhibit", {"--list", "--no-pager"});
            list.waitForFinished(5000);
            return list.readAllStandardOutput().count("AirCast");
        };
        // Counted, because an installed AirCast that is mirroring holds its own.
        const int before = locks();
        Backend backend;
        backend.setIdleInhibited(true);
        QCOMPARE(locks(), before + 1);
        backend.setIdleInhibited(false);
        QCOMPARE(locks(), before);
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
