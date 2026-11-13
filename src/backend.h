#pragma once

#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <atomic>

struct raop_s;
struct dnssd_s;
struct logger_s;
typedef struct _GstElement GstElement;

// The AirPlay receiver. The protocol (pairing, FairPlay, the mirror and audio
// streams) is UxPlay's library, built into this binary; this class runs it,
// decodes the video into the window's own VideoOutput, and tells QML who is
// connected.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString phase READ phase NOTIFY changed) // idle, pairing, connected
    Q_PROPERTY(QString pin READ pin NOTIFY changed)
    Q_PROPERTY(QString clientName READ clientName NOTIFY changed)
    Q_PROPERTY(int elapsed READ elapsed NOTIFY changed)
    Q_PROPERTY(int fps READ fps NOTIFY changed)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    bool visible() const { return m_raop != nullptr; }
    void setVisible(bool visible);
    QString name() const { return m_name; }
    QString phase() const { return m_phase; }
    QString pin() const { return m_pin; }
    QString clientName() const { return m_clientName; }
    int elapsed() const { return m_elapsed; }
    int fps() const { return m_fps; }
    QSize videoSize() const { return m_videoSize; }
    QString error() const { return m_error; }
    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);

    // Drops whoever is connected (or pairing) and keeps listening.
    Q_INVOKABLE void disconnectClient();

    // AirPlay volume is -30 dB (silent) to 0 dB (full), -144 for mute;
    // GStreamer wants a linear gain.
    static double linearVolume(float airplayDb);

    // Called by the protocol library from its own threads.
    void pushVideo(const unsigned char *data, int len);
    void deliver(const QVideoFrame &frame);
    void onClient(const QString &name);
    void onPin(const QString &pin);
    void onStreaming();
    void onConnections(int count);
    void resetVideo();
    void fail(const QString &message);
    std::atomic<int> connections{0};
    std::atomic<int> missedFeedback{0};
    std::atomic<quint64> clockOffset{0};

signals:
    void changed();

private:
    bool start();
    void stop();
    void tick();
    void setPhase(const QString &phase);
    bool createPipeline();
    void destroyPipeline();

    QString m_name, m_phase = "idle", m_pin, m_clientName, m_error;
    int m_elapsed = 0, m_fps = 0, m_frames = 0;
    QSize m_videoSize;
    QPointer<QVideoSink> m_sink;
    QTimer m_tick, m_pairingTimeout;

    raop_s *m_raop = nullptr;
    dnssd_s *m_dnssd = nullptr;
    logger_s *m_logger = nullptr;

    QMutex m_videoMutex; // the pipeline is fed from the library's mirror thread
    GstElement *m_pipeline = nullptr, *m_appsrc = nullptr;
};
