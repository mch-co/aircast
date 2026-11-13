// Receiver wiring follows UxPlay's uxplay.cpp (GPL-3.0, F. Duncanh and others).
#include "backend.h"

#include <QDir>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QScreen>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVideoFrameFormat>
#include <cmath>
#include <cstring>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

extern "C" {
#include "audio_renderer.h"
#include "dnssd.h"
#include "logger.h"
#include "raop.h"
}

namespace {

// The audio renderer keeps these pointers; unsynchronised playback is what
// keeps screen mirroring low-latency.
const bool kAudioSync = false, kVideoSync = false;
const int kFeedbackLimit = 15; // seconds of client silence before we give up

Backend *self(void *cls) { return static_cast<Backend *>(cls); }

template <typename F> void post(Backend *b, F f) { QMetaObject::invokeMethod(b, f, Qt::QueuedConnection); }

void logLine(void *, int level, const char *msg) {
    if (level <= LOGGER_ERR) qWarning("airplay: %s", msg);
    else qInfo("airplay: %s", msg);
}

void connInit(void *cls) { post(self(cls), [b = self(cls), n = ++self(cls)->connections] { b->onConnections(n); }); }
void connDestroy(void *cls) {
    Backend *b = self(cls);
    const int n = --b->connections;
    if (n == 0) {
        b->clockOffset = 0;
        audio_renderer_stop();
    }
    post(b, [b, n] { b->onConnections(n); });
}
void connFeedback(void *cls) { self(cls)->missedFeedback = 0; }
void connReset(void *cls, int) { post(self(cls), [b = self(cls)] { b->disconnectClient(); }); }

void clientRequest(void *cls, char *, char *, char *name, bool *admit) {
    *admit = true;
    post(self(cls), [b = self(cls), n = QString::fromUtf8(name)] { b->onClient(n); });
}
void displayPin(void *cls, char *pin) { post(self(cls), [b = self(cls), p = QString::fromLatin1(pin)] { b->onPin(p); }); }
const char *passwd(void *, int *len) { *len = 0; return nullptr; }
bool checkRegister(void *, const char *) { return true; }
void registerClient(void *, const char *, const char *, const char *) {}

quint64 localTime(Backend *b, quint64 local, quint64 remote) {
    quint64 offset = b->clockOffset;
    if (!offset) {
        offset = (local ? local : get_local_time()) - remote;
        b->clockOffset = offset;
    }
    return remote + offset;
}

void audioProcess(void *cls, raop_ntp_t *, audio_decode_struct *d) {
    uint64_t time = localTime(self(cls), d->ntp_time_local, d->ntp_time_remote);
    audio_renderer_render_buffer(d->data, &d->data_len, &d->seqnum, &time);
}
void audioFormat(void *cls, unsigned char *ct, unsigned short *, bool *, bool *, uint64_t *) {
    audio_renderer_start(ct);
    post(self(cls), [b = self(cls)] { b->onStreaming(); });
}
void audioFlush(void *) { audio_renderer_flush(); }
double audioClientVolume(void *) { return 0.0; }
void audioVolume(void *, float volume) { audio_renderer_set_volume(Backend::linearVolume(volume)); }

void videoProcess(void *cls, raop_ntp_t *, video_decode_struct *d) {
    if (!d->is_h265)
        self(cls)->pushVideo(d->data, d->data_len);
}
void videoRunning(void *cls, bool running) {
    if (running)
        post(self(cls), [b = self(cls)] { b->onStreaming(); });
}
void videoReset(void *cls, reset_type_t) { post(self(cls), [b = self(cls)] { b->resetVideo(); }); }
// Only H.264 is advertised, so anything else is a client we can't show.
int videoCodec(void *, video_codec_t codec) { return codec == VIDEO_CODEC_H264 ? 0 : -1; }

void nothing(void *) {}
void noBuffer(void *, const void *, int) {}
void noProgress(void *, uint32_t *, uint32_t *, uint32_t *) {}
void noRemote(void *, const char *, const char *) {}
void noSize(void *, float *, float *, float *, float *) {}

// A stable hardware address is the receiver's identity: phones remember the
// pairing by it. A random one is the fallback on a machine with no interface.
QByteArray hardwareAddress() {
    for (const auto &interface : QNetworkInterface::allInterfaces()) {
        if (interface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        const QByteArray mac = QByteArray::fromHex(interface.hardwareAddress().remove(':').toLatin1());
        if (mac.size() == 6 && mac != QByteArray(6, '\0'))
            return mac;
    }
    QByteArray mac(6, '\0');
    for (auto &byte : mac)
        byte = char(QRandomGenerator::global()->bounded(256));
    mac[0] = char((mac[0] & ~0x01) | 0x02); // locally administered
    return mac;
}

GstFlowReturn onSample(GstAppSink *sink, gpointer cls) {
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_OK;
    GstVideoInfo info;
    GstVideoFrame in;
    if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))
        && gst_video_frame_map(&in, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
        QVideoFrame out(QVideoFrameFormat(QSize(info.width, info.height), QVideoFrameFormat::Format_YUV420P));
        if (out.map(QVideoFrame::WriteOnly)) {
            for (int plane = 0; plane < 3; ++plane) {
                const int rows = GST_VIDEO_FRAME_COMP_HEIGHT(&in, plane);
                const int from = GST_VIDEO_FRAME_PLANE_STRIDE(&in, plane), to = out.bytesPerLine(plane);
                const uchar *source = static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&in, plane));
                uchar *target = out.bits(plane);
                for (int row = 0; row < rows; ++row)
                    std::memcpy(target + row * to, source + row * from, std::min(from, to));
            }
            out.unmap();
            post(self(cls), [b = self(cls), out] { b->deliver(out); });
        }
        gst_video_frame_unmap(&in);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

gboolean onBus(GstBus *, GstMessage *message, gpointer cls) {
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        self(cls)->fail(Backend::tr("Video decoding failed: %1").arg(QString::fromUtf8(error->message)));
        g_error_free(error);
    }
    return TRUE;
}

} // namespace

Backend::Backend(QObject *parent) : QObject(parent), m_name(QSysInfo::machineHostName()) {
    if (m_name.isEmpty())
        m_name = "Omarchy";
    m_logger = logger_init();
    logger_set_callback(m_logger, logLine, nullptr);
    logger_set_level(m_logger, LOGGER_INFO);
    if (!gstreamer_init())
        m_error = tr("GStreamer plugins are missing. Install gst-plugins-base, gst-plugins-good, gst-plugins-bad and gst-libav.");
    audio_renderer_init(m_logger, "autoaudiosink", &kAudioSync, &kVideoSync, "");

    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &Backend::tick);
    m_tick.start();
    // A phone that asked for a code and then walked away shouldn't leave the
    // code on screen forever.
    m_pairingTimeout.setSingleShot(true);
    m_pairingTimeout.setInterval(60000);
    connect(&m_pairingTimeout, &QTimer::timeout, this, [this] {
        if (m_phase == "pairing")
            setPhase("idle");
    });
}

Backend::~Backend() {
    stop();
    audio_renderer_destroy();
    logger_destroy(m_logger);
}

double Backend::linearVolume(float db) {
    if (db <= -30.0f)
        return 0.0; // includes the -144 mute signal
    return std::pow(10.0, 0.05 * std::min(db, 0.0f));
}

void Backend::setVisible(bool visible) {
    if (visible == this->visible())
        return;
    if (visible) {
        m_error.clear();
        if (!start())
            stop();
    } else {
        stop();
    }
    emit changed();
}

bool Backend::start() {
    const QByteArray name = m_name.toUtf8(), mac = hardwareAddress();
    int error = 0;
    // The 1 asks clients for the on-screen code the first time they connect.
    m_dnssd = dnssd_init(name.constData(), name.size(), mac.constData(), mac.size(), &error, 1);
    if (error || !m_dnssd) {
        m_dnssd = nullptr;
        m_error = tr("Could not start network discovery (error %1).").arg(error);
        return false;
    }
    // UxPlay's feature bits 0-31: mirroring, audio and FairPlay, without
    // HLS video, H.265 or legacy pairing.
    static const char features[] = "01100111011111111111111001011010";
    for (int bit = 0; bit < 32; ++bit)
        dnssd_set_airplay_features(m_dnssd, bit, features[bit] - '0');
    dnssd_set_airplay_features(m_dnssd, 27, 0);

    raop_callbacks_t callbacks;
    std::memset(&callbacks, 0, sizeof(callbacks));
    callbacks.cls = this;
    callbacks.conn_init = connInit;
    callbacks.conn_destroy = connDestroy;
    callbacks.conn_reset = connReset;
    callbacks.conn_feedback = connFeedback;
    callbacks.audio_process = audioProcess;
    callbacks.video_process = videoProcess;
    callbacks.audio_flush = audioFlush;
    callbacks.video_flush = nothing;
    callbacks.video_pause = nothing;
    callbacks.video_resume = nothing;
    callbacks.audio_set_client_volume = audioClientVolume;
    callbacks.audio_set_volume = audioVolume;
    callbacks.audio_get_format = audioFormat;
    callbacks.video_report_size = noSize;
    callbacks.audio_set_metadata = noBuffer;
    callbacks.audio_set_coverart = noBuffer;
    callbacks.audio_stop_coverart_rendering = nothing;
    callbacks.audio_remote_control_id = noRemote;
    callbacks.audio_set_progress = noProgress;
    callbacks.report_client_request = clientRequest;
    callbacks.display_pin = displayPin;
    callbacks.register_client = registerClient;
    callbacks.check_register = checkRegister;
    callbacks.passwd = passwd;
    callbacks.export_dacp = noRemote;
    callbacks.video_reset = videoReset;
    callbacks.video_set_codec = videoCodec;
    callbacks.mirror_video_running = videoRunning;

    m_raop = raop_init(&callbacks);
    if (!m_raop) {
        m_error = tr("Could not start the AirPlay server.");
        return false;
    }
    raop_set_log_callback(m_raop, logLine, nullptr);
    raop_set_log_level(m_raop, LOGGER_INFO);

    // The key is this receiver's identity for paired phones, so it persists.
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data);
    const QByteArray macText = mac.toHex(':'), keyfile = (data + "/key.pem").toUtf8();
    if (raop_init2(m_raop, 0, macText.constData(), keyfile.constData())) {
        m_error = tr("Could not start the AirPlay server.");
        return false;
    }

    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize pixels = screen->size() * screen->devicePixelRatio();
        raop_set_plist(m_raop, "width", pixels.width());
        raop_set_plist(m_raop, "height", pixels.height());
    }
    raop_set_plist(m_raop, "maxFPS", 60);
    raop_set_plist(m_raop, "pin", 0); // a fresh random code for every pairing

    unsigned short tcp[2] = {0, 0}, udp[3] = {0, 0, 0};
    raop_set_tcp_ports(m_raop, tcp);
    raop_set_udp_ports(m_raop, udp);
    unsigned short port = raop_get_port(m_raop);
    if (raop_start_httpd(m_raop, &port) < 0) {
        m_error = tr("Could not open a network port for AirPlay.");
        return false;
    }
    raop_set_port(m_raop, port);
    raop_set_dnssd(m_raop, m_dnssd);

    if (dnssd_register_raop(m_dnssd, port) || dnssd_register_airplay(m_dnssd, port)) {
        m_error = tr("Could not announce this computer on the network. Is avahi-daemon running?");
        return false;
    }
    return true;
}

void Backend::stop() {
    if (m_dnssd) {
        dnssd_unregister_raop(m_dnssd);
        dnssd_unregister_airplay(m_dnssd);
    }
    if (m_raop) {
        raop_destroy(m_raop);
        m_raop = nullptr;
    }
    if (m_dnssd) {
        dnssd_destroy(m_dnssd);
        m_dnssd = nullptr;
    }
    connections = 0;
    audio_renderer_stop();
    destroyPipeline();
    setPhase("idle");
}

void Backend::disconnectClient() {
    if (!m_raop)
        return;
    // Closing the server's sockets is what tells the phone to stop; the
    // pipeline lock must not be held while its threads wind down.
    raop_stop_httpd(m_raop);
    raop_remove_known_connections(m_raop);
    connections = 0;
    audio_renderer_stop();
    destroyPipeline();
    setPhase("idle");
    unsigned short port = raop_get_port(m_raop);
    if (raop_start_httpd(m_raop, &port) < 0)
        fail(tr("Could not open a network port for AirPlay."));
    else
        raop_set_port(m_raop, port);
}

void Backend::setVideoSink(QVideoSink *sink) {
    m_sink = sink;
    emit changed();
}

void Backend::setPhase(const QString &phase) {
    if (phase == "pairing")
        m_pairingTimeout.start();
    if (phase != "connected") {
        m_elapsed = m_fps = m_frames = 0;
        m_videoSize = {};
        if (m_sink)
            m_sink->setVideoFrame(QVideoFrame());
    }
    if (phase != "pairing")
        m_pin.clear();
    m_phase = phase;
    emit changed();
}

void Backend::onClient(const QString &name) {
    m_clientName = name;
    emit changed();
}

void Backend::onPin(const QString &pin) {
    m_pin = pin;
    setPhase("pairing");
}

void Backend::onStreaming() {
    if (m_phase != "connected")
        setPhase("connected");
}

void Backend::onConnections(int count) {
    missedFeedback = 0;
    // The phone closes its first connection while the code is being typed,
    // so pairing ends on success or on its own timeout, not here.
    if (count <= 0 && m_phase == "connected") {
        destroyPipeline();
        setPhase("idle");
    }
}

void Backend::resetVideo() { destroyPipeline(); }

void Backend::fail(const QString &message) {
    QMetaObject::invokeMethod(this, [this, message] {
        m_error = message;
        emit changed();
    });
}

void Backend::tick() {
    if (m_phase == "connected") {
        ++m_elapsed;
        m_fps = m_frames;
        m_frames = 0;
        emit changed();
    }
    // Phones send a heartbeat every two seconds; one that went out of range
    // never says goodbye.
    if (connections > 0 && ++missedFeedback > kFeedbackLimit)
        disconnectClient();
}

bool Backend::createPipeline() {
    GError *error = nullptr;
    m_pipeline = gst_parse_launch(
        "appsrc name=source is-live=true format=time stream-type=0 "
        "caps=\"video/x-h264,stream-format=(string)byte-stream,alignment=(string)au\" "
        "! queue ! h264parse ! decodebin ! videoconvert ! video/x-raw,format=I420 "
        "! appsink name=sink sync=false max-buffers=2 drop=true",
        &error);
    if (!m_pipeline || error) {
        fail(tr("Video decoding failed: %1").arg(error ? QString::fromUtf8(error->message) : QString()));
        g_clear_error(&error);
        if (m_pipeline)
            gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        return false;
    }
    m_appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "source");
    GstElement *sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);
    gst_object_unref(sink);
    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, onBus, this);
    gst_object_unref(bus);
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    return true;
}

void Backend::destroyPipeline() {
    QMutexLocker lock(&m_videoMutex);
    if (!m_pipeline)
        return;
    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_remove_watch(bus);
    gst_object_unref(bus);
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_appsrc);
    gst_object_unref(m_pipeline);
    m_pipeline = m_appsrc = nullptr;
}

void Backend::pushVideo(const unsigned char *data, int len) {
    // Valid H.264 starts with a zero byte; anything else failed to decrypt.
    if (len <= 0 || data[0])
        return;
    QMutexLocker lock(&m_videoMutex);
    if (!m_pipeline && !createPipeline())
        return;
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, len, nullptr);
    gst_buffer_fill(buffer, 0, data, len);
    gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
}

void Backend::deliver(const QVideoFrame &frame) {
    {
        // A frame still queued when the stream ended must not repaint it.
        QMutexLocker lock(&m_videoMutex);
        if (!m_pipeline)
            return;
    }
    ++m_frames;
    if (m_sink)
        m_sink->setVideoFrame(frame);
    if (frame.size() != m_videoSize) {
        m_videoSize = frame.size();
        emit changed();
    }
}
