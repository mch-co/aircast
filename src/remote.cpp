#include "remote.h"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QSettings>
#include <cmath>

using Interfaces = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, Interfaces>;
Q_DECLARE_METATYPE(Interfaces)
Q_DECLARE_METATYPE(ManagedObjects)

namespace {

const char kRoot[] = "/org/aircast/remote";
const char kAdvertisement[] = "/org/aircast/remote/advertisement";
const char kKeyboard[] = "/org/aircast/remote/hid/keyboard";
const char kMouse[] = "/org/aircast/remote/hid/mouse";
// shortcut: the first adapter only, look the path up through BlueZ's
// ObjectManager when machines with several adapters matter.
const char kAdapter[] = "/org/bluez/hci0";
const char kService[] = "org.bluez.GattService1", kCharacteristic[] = "org.bluez.GattCharacteristic1",
           kDescriptor[] = "org.bluez.GattDescriptor1", kAdvertisementInterface[] = "org.bluez.LEAdvertisement1",
           kProperties[] = "org.freedesktop.DBus.Properties";

QString uuid(const char *shortId) { return QString("0000%1-0000-1000-8000-00805f9b34fb").arg(shortId); }

// Report 1 is a keyboard (modifiers, reserved, six keys); report 2 a
// three-button mouse with relative X, Y and wheel.
const QByteArray kReportMap = QByteArray::fromHex(
    "05010906a1018501050719e029e71500250175019508810295017508810195067508150025650507190029658100c0"
    "05010902a10185020901a1000509190129031500250195037501810295017505810105010930093109381581257f750895038106c0c0");

struct Key { int evdev; quint8 usage; quint8 modifier; };
const Key kKeys[] = {
    {1, 0x29, 0}, {2, 0x1e, 0}, {3, 0x1f, 0}, {4, 0x20, 0}, {5, 0x21, 0}, {6, 0x22, 0}, {7, 0x23, 0}, {8, 0x24, 0},
    {9, 0x25, 0}, {10, 0x26, 0}, {11, 0x27, 0}, {12, 0x2d, 0}, {13, 0x2e, 0}, {14, 0x2a, 0}, {15, 0x2b, 0},
    {16, 0x14, 0}, {17, 0x1a, 0}, {18, 0x08, 0}, {19, 0x15, 0}, {20, 0x17, 0}, {21, 0x1c, 0}, {22, 0x18, 0},
    {23, 0x0c, 0}, {24, 0x12, 0}, {25, 0x13, 0}, {26, 0x2f, 0}, {27, 0x30, 0}, {28, 0x28, 0}, {30, 0x04, 0},
    {31, 0x16, 0}, {32, 0x07, 0}, {33, 0x09, 0}, {34, 0x0a, 0}, {35, 0x0b, 0}, {36, 0x0d, 0}, {37, 0x0e, 0},
    {38, 0x0f, 0}, {39, 0x33, 0}, {40, 0x34, 0}, {41, 0x35, 0}, {43, 0x31, 0}, {44, 0x1d, 0}, {45, 0x1b, 0},
    {46, 0x06, 0}, {47, 0x19, 0}, {48, 0x05, 0}, {49, 0x11, 0}, {50, 0x10, 0}, {51, 0x36, 0}, {52, 0x37, 0},
    {53, 0x38, 0}, {57, 0x2c, 0}, {58, 0x39, 0}, {59, 0x3a, 0}, {60, 0x3b, 0}, {61, 0x3c, 0}, {62, 0x3d, 0},
    {63, 0x3e, 0}, {64, 0x3f, 0}, {65, 0x40, 0}, {66, 0x41, 0}, {67, 0x42, 0}, {68, 0x43, 0}, {86, 0x64, 0},
    {87, 0x44, 0}, {88, 0x45, 0}, {102, 0x4a, 0}, {103, 0x52, 0}, {104, 0x4b, 0}, {105, 0x50, 0}, {106, 0x4f, 0},
    {107, 0x4d, 0}, {108, 0x51, 0}, {109, 0x4e, 0}, {110, 0x49, 0}, {111, 0x4c, 0},
    {42, 0, 0x02}, {54, 0, 0x20}, {56, 0, 0x04}, {100, 0, 0x40}, {125, 0, 0x08}, {126, 0, 0x80},
    // Ctrl is sent as Command: the desktop keeps Super for itself, and
    // Command is what copy, paste and Home (⌘H) need on the phone.
    {29, 0, 0x08}, {97, 0, 0x80},
};

} // namespace

int Remote::hidUsage(int evdevCode, quint8 *modifier) {
    *modifier = 0;
    for (const Key &key : kKeys) {
        if (key.evdev == evdevCode) {
            *modifier = key.modifier;
            return key.usage;
        }
    }
    return 0;
}

Remote::Remote(QObject *parent, bool useBluetooth) : QDBusVirtualObject(parent), m_useBluetooth(useBluetooth) {
    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<ManagedObjects>();

    const QString root = kRoot;
    // Reports and the map require an encrypted link, which is what makes the
    // phone offer to pair.
    const QStringList read{"encrypt-read"}, input{"encrypt-read", "encrypt-notify"};
    add(root + "/hid", kService, {{"UUID", uuid("1812")}, {"Primary", true}});
    addCharacteristic("hid", "information", "2a4a", read, QByteArray::fromHex("11010002"));
    addCharacteristic("hid", "map", "2a4b", read, kReportMap);
    addCharacteristic("hid", "control", "2a4c", {"write-without-response"}, {});
    addCharacteristic("hid", "protocol", "2a4e", {"read", "write-without-response"}, QByteArray::fromHex("01"));
    addCharacteristic("hid", "keyboard", "2a4d", input, QByteArray(8, '\0'), QByteArray::fromHex("0101"));
    addCharacteristic("hid", "mouse", "2a4d", input, QByteArray(4, '\0'), QByteArray::fromHex("0201"));
    addCharacteristic("hid", "leds", "2a4d", {"encrypt-read", "encrypt-write", "write-without-response"},
                      QByteArray(1, '\0'), QByteArray::fromHex("0102"));
    add(root + "/device", kService, {{"UUID", uuid("180a")}, {"Primary", true}});
    addCharacteristic("device", "maker", "2a29", {"read"}, "AirCast");
    addCharacteristic("device", "pnp", "2a50", {"read"}, QByteArray::fromHex("026b1d46020001"));
    add(root + "/battery", kService, {{"UUID", uuid("180f")}, {"Primary", true}});
    addCharacteristic("battery", "level", "2a19", {"read"}, QByteArray::fromHex("64"));

    add(kAdvertisement, kAdvertisementInterface,
        {{"Type", "peripheral"},
         {"ServiceUUIDs", QStringList{uuid("1812")}},
         {"LocalName", "AirCast"},
         {"Appearance", QVariant::fromValue<quint16>(0x03c0)}, // generic HID
         {"Discoverable", true}});

    if (m_useBluetooth && QSettings("aircast", "aircast").value("control").toBool())
        setEnabled(true);
}

Remote::~Remote() { stop(); }

void Remote::add(const QString &path, const QString &interface, const QVariantMap &properties, const QByteArray &value) {
    m_attributes.insert(path, {interface, properties, value});
}

void Remote::addCharacteristic(const QString &service, const QString &name, const QString &id,
                               const QStringList &flags, const QByteArray &value, const QByteArray &reportReference) {
    const QString servicePath = QString(kRoot) + "/" + service, path = servicePath + "/" + name;
    add(path, kCharacteristic,
        {{"UUID", uuid(id.toLatin1())}, {"Service", QVariant::fromValue(QDBusObjectPath(servicePath))}, {"Flags", flags}},
        value);
    // Tells the phone which report of the map this characteristic carries.
    if (!reportReference.isEmpty())
        add(path + "/reference", kDescriptor,
            {{"UUID", uuid("2908")}, {"Characteristic", QVariant::fromValue(QDBusObjectPath(path))},
             {"Flags", QStringList{"encrypt-read"}}},
            reportReference);
}

QVariantMap Remote::propertiesOf(const Attribute &attribute) const {
    QVariantMap properties = attribute.properties;
    if (attribute.interface == kCharacteristic || attribute.interface == kDescriptor)
        properties.insert("Value", attribute.value);
    return properties;
}

QString Remote::state() const {
    if (!m_enabled)
        return "off";
    if (!m_registered)
        return "starting";
    return m_notifying.isEmpty() ? "waiting" : "connected";
}

void Remote::setEnabled(bool enabled) {
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    QSettings("aircast", "aircast").setValue("control", enabled);
    m_error.clear();
    enabled ? start() : stop();
    emit changed();
}

void Remote::fail(const QString &message) {
    stop();
    m_enabled = false;
    m_error = message;
    emit changed();
}

void Remote::start() {
    if (!m_useBluetooth)
        return;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerVirtualObject(kRoot, this, QDBusConnection::SubPath)) {
        fail(tr("Could not reach Bluetooth: %1").arg(bus.lastError().message()));
        return;
    }
    callAdapter("org.bluez.GattManager1", "RegisterApplication", kRoot, [this] {
        callAdapter("org.bluez.LEAdvertisingManager1", "RegisterAdvertisement", kAdvertisement, [this] {
            m_registered = true;
            emit changed();
        });
    });
}

// BlueZ reads our services back while it answers, so this must not block.
void Remote::callAdapter(const char *interface, const char *method, const char *path, std::function<void()> done) {
    QDBusMessage message = QDBusMessage::createMethodCall("org.bluez", kAdapter, interface, method);
    message << QVariant::fromValue(QDBusObjectPath(path)) << QVariantMap();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, done] {
        watcher->deleteLater();
        if (!m_enabled)
            return;
        if (watcher->isError())
            fail(tr("Bluetooth is off or can't act as a mouse and keyboard: %1").arg(watcher->error().message()));
        else
            done();
    });
}

void Remote::stop() {
    m_notifying.clear();
    m_registered = false;
    if (!m_useBluetooth)
        return;
    QDBusConnection bus = QDBusConnection::systemBus();
    auto call = [&](const char *interface, const char *method, const char *path) {
        QDBusMessage message = QDBusMessage::createMethodCall("org.bluez", kAdapter, interface, method);
        message << QVariant::fromValue(QDBusObjectPath(path));
        bus.send(message);
    };
    call("org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement", kAdvertisement);
    call("org.bluez.GattManager1", "UnregisterApplication", kRoot);
    bus.unregisterObject(kRoot, QDBusConnection::UnregisterTree);
}

bool Remote::handleMessage(const QDBusMessage &message, const QDBusConnection &connection) {
    const QString path = message.path(), interface = message.interface(), member = message.member();
    const QVariantList arguments = message.arguments();
    auto reply = [&](const QVariantList &values = {}) {
        connection.send(message.createReply(values));
        return true;
    };

    if (path == kRoot && interface == "org.freedesktop.DBus.ObjectManager" && member == "GetManagedObjects") {
        ManagedObjects objects;
        for (auto it = m_attributes.constBegin(); it != m_attributes.constEnd(); ++it)
            if (it.key() != kAdvertisement)
                objects.insert(QDBusObjectPath(it.key()), {{it->interface, propertiesOf(*it)}});
        return reply({QVariant::fromValue(objects)});
    }

    auto attribute = m_attributes.find(path);
    if (attribute == m_attributes.end())
        return false;

    if (interface == kProperties && member == "GetAll")
        return reply({propertiesOf(*attribute)});
    if (interface == kProperties && member == "Get" && arguments.size() == 2) {
        const QVariantMap properties = propertiesOf(*attribute);
        if (!properties.contains(arguments.at(1).toString()))
            return false;
        return reply({QVariant::fromValue(QDBusVariant(properties.value(arguments.at(1).toString())))});
    }
    if (interface == kAdvertisementInterface && member == "Release")
        return reply();
    if (interface != kCharacteristic && interface != kDescriptor)
        return false;

    if (member == "ReadValue") {
        // Long values such as the report map are read in pieces.
        QVariantMap options;
        if (!arguments.isEmpty())
            arguments.at(0).value<QDBusArgument>() >> options;
        return reply({attribute->value.mid(options.value("offset").toUInt())});
    }
    if (member == "WriteValue") {
        if (!arguments.isEmpty())
            attribute->value = arguments.at(0).toByteArray();
        return reply();
    }
    if (member == "StartNotify" || member == "StopNotify") {
        // The phone subscribing to our reports is the sign it is connected
        // and paired; BlueZ stops the subscription when it leaves.
        member == "StartNotify" ? void(m_notifying.insert(path)) : void(m_notifying.remove(path));
        if (m_notifying.isEmpty())
            m_modifiers = m_buttons = 0, m_keys.clear();
        emit changed();
        return reply();
    }
    return false;
}

void Remote::sendReport(int id, const QByteArray &report) {
    const QString path = id == 1 ? kKeyboard : kMouse;
    m_attributes[path].value = report;
    emit reportSent(id, report);
    if (!m_notifying.contains(path))
        return;
    // BlueZ turns a change of Value into a notification to the phone.
    QDBusMessage signal = QDBusMessage::createSignal(path, kProperties, "PropertiesChanged");
    signal << QString(kCharacteristic) << QVariantMap{{"Value", report}} << QStringList();
    QDBusConnection::systemBus().send(signal);
}

void Remote::sendKeyboard() {
    QByteArray report(8, '\0');
    report[0] = char(m_modifiers);
    for (int i = 0; i < m_keys.size() && i < 6; ++i)
        report[2 + i] = char(m_keys.at(i));
    sendReport(1, report);
}

void Remote::sendMouse(int dx, int dy, int wheel) {
    QByteArray report(4, '\0');
    report[0] = char(m_buttons);
    report[1] = char(qint8(dx));
    report[2] = char(qint8(dy));
    report[3] = char(qint8(wheel));
    sendReport(2, report);
}

void Remote::move(qreal dx, qreal dy) {
    m_restX += dx;
    m_restY += dy;
    int x = int(std::trunc(m_restX)), y = int(std::trunc(m_restY));
    m_restX -= x;
    m_restY -= y;
    // A report moves at most 127 counts per axis; a fast flick takes several.
    while (x || y) {
        const int stepX = qBound(-127, x, 127), stepY = qBound(-127, y, 127);
        sendMouse(stepX, stepY, 0);
        x -= stepX;
        y -= stepY;
    }
}

void Remote::buttons(int mask) {
    if (quint8(mask & 7) == m_buttons)
        return;
    m_buttons = quint8(mask & 7);
    sendMouse(0, 0, 0);
}

void Remote::scroll(int steps) {
    if (steps)
        sendMouse(0, 0, qBound(-127, steps, 127));
}

void Remote::key(int evdevCode, bool pressed) {
    quint8 modifier = 0;
    const quint8 usage = quint8(hidUsage(evdevCode, &modifier));
    if (modifier) {
        m_modifiers = pressed ? (m_modifiers | modifier) : (m_modifiers & ~modifier);
    } else if (usage) {
        m_keys.removeAll(usage);
        if (pressed)
            m_keys.append(usage);
    } else {
        return;
    }
    sendKeyboard();
}

void Remote::home() {
    // ⌘H is Home for a hardware keyboard on iOS.
    sendReport(1, QByteArray::fromHex("0800000b00000000"));
    sendKeyboard();
}

void Remote::releaseAll() {
    if (m_modifiers || !m_keys.isEmpty()) {
        m_modifiers = 0;
        m_keys.clear();
        sendKeyboard();
    }
    buttons(0);
}
