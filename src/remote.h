#pragma once

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QMap>
#include <QSet>
#include <QVariantMap>
#include <functional>

// Controls the phone. AirPlay only carries the screen one way, so this is a
// separate channel: the computer presents itself to the iPhone as a
// Bluetooth LE mouse and keyboard (HID over GATT), served through BlueZ.
// iOS shows a pointer for it once AssistiveTouch is on.
class Remote : public QDBusVirtualObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed) // off, starting, waiting, connected
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    // Tests pass register=false to build reports without touching Bluetooth.
    explicit Remote(QObject *parent = nullptr, bool useBluetooth = true);
    ~Remote() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString state() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void move(qreal dx, qreal dy);
    Q_INVOKABLE void buttons(int mask); // 1 left, 2 right, 4 middle
    Q_INVOKABLE void scroll(int steps);
    Q_INVOKABLE void key(int evdevCode, bool pressed);
    Q_INVOKABLE void home();
    Q_INVOKABLE void releaseAll();

    // The USB HID usage for a Linux key code; modifiers come back as their
    // bit in *modifier with a usage of 0. Unknown keys are 0 and 0.
    static int hidUsage(int evdevCode, quint8 *modifier);

    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

signals:
    void changed();
    void reportSent(int id, const QByteArray &report);

private:
    struct Attribute {
        QString interface;
        QVariantMap properties;
        QByteArray value;
    };
    void add(const QString &path, const QString &interface, const QVariantMap &properties, const QByteArray &value = {});
    void addCharacteristic(const QString &service, const QString &name, const QString &uuid, const QStringList &flags,
                           const QByteArray &value, const QByteArray &reportReference = {});
    QVariantMap propertiesOf(const Attribute &attribute) const;
    void sendReport(int id, const QByteArray &report);
    void sendKeyboard();
    void sendMouse(int dx, int dy, int wheel);
    void callAdapter(const char *interface, const char *method, const char *path, std::function<void()> done);
    void start();
    void stop();
    void fail(const QString &message);

    bool m_useBluetooth, m_enabled = false, m_registered = false;
    QString m_error;
    QMap<QString, Attribute> m_attributes;
    QSet<QString> m_notifying;
    quint8 m_modifiers = 0, m_buttons = 0;
    QList<quint8> m_keys;
    qreal m_restX = 0, m_restY = 0; // sub-pixel movement carried to the next report
};
