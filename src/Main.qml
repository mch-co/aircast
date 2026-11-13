import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Shapes
import QtMultimedia

ApplicationWindow {
    id: win
    width: 1040
    height: 720
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "AirCast"
    color: "#0b1020"

    Material.theme: Material.Dark
    Material.accent: theme.accent

    readonly property color ink: "#eef1f8"
    readonly property color green: "#5eeaa0"
    readonly property string mono: "JetBrainsMono Nerd Font"
    readonly property bool connected: backend.phase === "connected"
    readonly property bool full: visibility === Window.FullScreen
    readonly property bool landscape: backend.videoSize.width > backend.videoSize.height
    property bool helpVisible: false
    // While the pointer is on the picture, the mouse and keyboard belong to
    // the phone, so the app's own single-key shortcuts step aside.
    readonly property bool controlling: remote.state === "connected" && control.containsMouse
    property bool expanded: true

    function dim(alpha) { return Qt.rgba(238 / 255, 241 / 255, 248 / 255, alpha) }
    function clock(seconds) {
        const pad = n => String(n).padStart(2, "0")
        return pad(Math.floor(seconds / 60)) + ":" + pad(seconds % 60)
    }
    function toggleFull() { win.visibility = win.full ? Window.Windowed : Window.FullScreen }

    // Nothing to show full screen once the phone is gone.
    onConnectedChanged: if (!connected && full) win.visibility = Window.Windowed

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.controlling; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.controlling; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.controlling; onActivated: backend.visible = !backend.visible }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: win.connected && !win.controlling; onActivated: win.toggleFull() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !win.controlling; onActivated: win.helpVisible ? win.helpVisible = false : win.visibility = Window.Windowed }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: backend.phase !== "idle" && !win.controlling; onActivated: backend.disconnectClient() }

    component Mono: Label {
        font.family: win.mono
        font.pixelSize: 10
        color: win.dim(.55)
    }
    component Caption: Mono {
        font.capitalization: Font.AllUppercase
        font.letterSpacing: .8
        color: win.dim(.5)
    }
    component Card: Rectangle {
        radius: 10
        color: Qt.rgba(0, 0, 0, .2)
    }
    component Avatar: Rectangle {
        width: 32; height: 32; radius: 8
        color: Qt.rgba(1, 1, 1, .08)
        Label { anchors.centerIn: parent; text: "iP"; font.family: win.mono; font.pixelSize: 10; font.bold: true; color: win.ink }
    }
    component FlatButton: AbstractButton {
        id: button
        property color base: Qt.rgba(1, 1, 1, .1)
        property color ink: win.ink
        implicitHeight: 32
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        background: Rectangle {
            radius: 8
            color: button.hovered ? Qt.lighter(button.base, 1.5) : button.base
            border.width: button.visualFocus ? 1 : 0
            border.color: theme.accent
        }
        contentItem: Label {
            text: button.text
            color: button.ink
            font.pixelSize: 12
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    component Toggle: AbstractButton {
        id: toggleRoot
        property bool on: false
        implicitWidth: 38; implicitHeight: 22
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.CheckBox
        Accessible.checked: on
        background: Rectangle {
            radius: 11
            color: toggleRoot.on ? win.green : Qt.rgba(1, 1, 1, .18)
            border.width: toggleRoot.visualFocus ? 1 : 0
            border.color: "white"
            Behavior on color { ColorAnimation { duration: 200 } }
            Rectangle {
                y: 3; x: toggleRoot.on ? 19 : 3
                width: 16; height: 16; radius: 8; color: "white"
                Behavior on x { NumberAnimation { duration: 200 } }
            }
        }
    }
    component Stat: Card {
        property alias label: caption.text
        property alias value: number.text
        Layout.fillWidth: true
        implicitHeight: 44
        radius: 8
        Column {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 2
            Caption { id: caption; font.pixelSize: 9 }
            Label { id: number; font.family: win.mono; font.pixelSize: 12; color: win.ink }
        }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // The stage: the phone's screen, in this window.
        Rectangle {
            id: stage
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: win.full ? parent.right : panel.left }
            anchors.margins: win.full ? 0 : 16
            radius: win.full ? 0 : 14
            color: win.full ? "black" : Qt.rgba(16 / 255, 18 / 255, 32 / 255, .72)
            border.width: win.full ? 0 : 1
            border.color: Qt.rgba(1, 1, 1, .1)

            RowLayout {
                id: stageBar
                visible: win.connected && !win.full
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 14; rightMargin: 10; topMargin: 8 }
                height: 28
                spacing: 8
                Rectangle { width: 7; height: 7; radius: 4; color: win.green }
                Label { text: backend.clientName; font.pixelSize: 13; font.bold: true; color: win.ink; elide: Text.ElideRight; Layout.maximumWidth: stage.width / 2 }
                Mono {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: backend.videoSize.width > 0
                        ? qsTr("%1×%2 · %3 fps").arg(backend.videoSize.width).arg(backend.videoSize.height).arg(backend.fps)
                        : qsTr("audio only")
                }
                FlatButton {
                    text: "⤢"; base: "transparent"; implicitWidth: 28; implicitHeight: 26
                    Accessible.name: qsTr("Full screen")
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Full screen")
                    onClicked: win.toggleFull()
                }
                FlatButton {
                    text: "✕"; base: "transparent"; implicitWidth: 28; implicitHeight: 26
                    Accessible.name: qsTr("Disconnect")
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Disconnect")
                    onClicked: backend.disconnectClient()
                }
            }

            Item {
                id: screenArea
                anchors.fill: parent
                anchors.margins: win.full ? 0 : 10
                anchors.topMargin: win.full ? 0 : 40
                visible: win.connected
                TapHandler { onDoubleTapped: win.toggleFull() }

                // Sized to the picture itself, so the rounded corners sit on
                // the phone's screen and not on the letterboxing.
                Item {
                    id: screen
                    readonly property real ratio: backend.videoSize.height > 0 ? backend.videoSize.width / backend.videoSize.height : 1
                    readonly property real corner: win.full ? 0 : (win.landscape ? 18 : 26)
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height * ratio)
                    height: width / ratio

                    VideoOutput {
                        id: video
                        anchors.fill: parent
                        fillMode: VideoOutput.Stretch
                        Component.onCompleted: backend.videoSink = video.videoSink
                        layer.enabled: screen.corner > 0
                        layer.effect: MultiEffect { maskEnabled: true; maskSource: screenMask }
                    }
                    // The picture is the trackpad: movement is relative, like
                    // any mouse, so the phone's own pointer is the one to watch.
                    MouseArea {
                        id: control
                        property point last
                        anchors.fill: parent
                        enabled: remote.state === "connected"
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        cursorShape: enabled ? Qt.CrossCursor : Qt.ArrowCursor
                        focus: containsMouse
                        function report(mouse) {
                            remote.buttons((mouse.buttons & Qt.LeftButton ? 1 : 0) | (mouse.buttons & Qt.RightButton ? 2 : 0) | (mouse.buttons & Qt.MiddleButton ? 4 : 0))
                        }
                        onEntered: { last = Qt.point(mouseX, mouseY); forceActiveFocus() }
                        onExited: remote.releaseAll()
                        onPositionChanged: mouse => {
                            remote.move(mouse.x - last.x, mouse.y - last.y)
                            last = Qt.point(mouse.x, mouse.y)
                        }
                        onPressed: mouse => report(mouse)
                        onReleased: mouse => report(mouse)
                        onWheel: wheel => remote.scroll(Math.round(wheel.angleDelta.y / 120))
                        Keys.onPressed: event => { if (!event.isAutoRepeat) remote.key(event.nativeScanCode - 8, true); event.accepted = true }
                        Keys.onReleased: event => { if (!event.isAutoRepeat) remote.key(event.nativeScanCode - 8, false); event.accepted = true }
                    }
                    Rectangle {
                        id: screenMask
                        anchors.fill: parent
                        radius: screen.corner
                        visible: false
                        layer.enabled: true
                    }
                }
            }

            Column {
                visible: !win.connected
                anchors.centerIn: parent
                spacing: 14
                width: Math.min(parent.width - 48, 360)
                Shape {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 72; height: 72
                    opacity: .35
                    ShapePath {
                        strokeColor: win.ink; strokeWidth: 3; fillColor: "transparent"
                        capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                        PathSvg { path: "M15 51H12a6 6 0 0 1-6-6V15a6 6 0 0 1 6-6h48a6 6 0 0 1 6 6v30a6 6 0 0 1-6 6h-3 M36 45l15 18H21z" }
                    }
                }
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: win.dim(.6)
                    font.pixelSize: 13
                    text: backend.visible ? qsTr("The screen of your iPhone will appear here.") : qsTr("This computer is hidden from Apple devices.")
                }
            }

            Row {
                visible: win.full
                anchors { top: parent.top; right: parent.right; margins: 18 }
                spacing: 10
                opacity: fullHover.hovered ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 200 } }
                Mono { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11; color: Qt.rgba(1, 1, 1, .5); text: qsTr("Esc to exit") }
                FlatButton { text: qsTr("Exit full screen"); leftPadding: 12; rightPadding: 12; onClicked: win.toggleFull() }
            }
            HoverHandler { id: fullHover }
        }

        // The AirPlay panel.
        Rectangle {
            id: panel
            visible: !win.full
            anchors { top: parent.top; right: parent.right; topMargin: 16; rightMargin: 16 }
            width: 340
            height: content.implicitHeight + 24
            radius: 16
            color: Qt.rgba(16 / 255, 18 / 255, 32 / 255, .72)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, .1)

            ColumnLayout {
                id: content
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                spacing: 12

                RowLayout {
                    spacing: 10
                    Layout.leftMargin: 4
                    Rectangle {
                        width: 28; height: 28; radius: 8; color: Qt.rgba(1, 1, 1, .08)
                        Shape {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            ShapePath {
                                strokeColor: "white"; strokeWidth: 1.4; fillColor: "transparent"
                                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                                PathSvg { path: "M3.3 11.3H2.7a1.3 1.3 0 0 1-1.3-1.3V3.3a1.3 1.3 0 0 1 1.3-1.3h10.6a1.3 1.3 0 0 1 1.3 1.3v6.7a1.3 1.3 0 0 1-1.3 1.3h-.6 M8 10l3.3 4H4.7z" }
                            }
                        }
                    }
                    Column {
                        Layout.fillWidth: true
                        Label { text: "AirPlay"; font.bold: true; font.pixelSize: 14; color: win.ink }
                        Row {
                            spacing: 6
                            Rectangle { width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: backend.visible ? win.green : Qt.rgba(1, 1, 1, .4) }
                            Mono {
                                font.pixelSize: 10
                                color: win.dim(.6)
                                text: !backend.visible ? qsTr("hidden")
                                    : win.connected ? qsTr("mirroring · %1").arg(backend.clientName)
                                    : backend.phase === "pairing" ? qsTr("pairing")
                                    : qsTr("waiting for a connection")
                            }
                        }
                    }
                    FlatButton {
                        text: win.expanded ? "–" : "+"; base: "transparent"; implicitWidth: 26; implicitHeight: 26
                        Accessible.name: win.expanded ? qsTr("Collapse the panel") : qsTr("Expand the panel")
                        onClicked: win.expanded = !win.expanded
                    }
                }

                Card {
                    Layout.fillWidth: true
                    visible: win.expanded
                    implicitHeight: 60
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12
                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: qsTr("Visible on the network"); font.pixelSize: 13; font.weight: Font.DemiBold; color: win.ink }
                            Mono { text: qsTr("name: %1").arg(backend.name) }
                        }
                        Toggle {
                            on: backend.visible
                            Accessible.name: qsTr("Visible on the network")
                            onClicked: backend.visible = !backend.visible
                        }
                    }
                }

                // Control: the computer as the phone's Bluetooth mouse and keyboard.
                Card {
                    Layout.fillWidth: true
                    visible: win.expanded
                    implicitHeight: controlCard.implicitHeight + 24
                    ColumnLayout {
                        id: controlCard
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 10
                        RowLayout {
                            spacing: 12
                            Column {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: qsTr("Control the iPhone"); font.pixelSize: 13; font.weight: Font.DemiBold; color: win.ink }
                                Mono {
                                    color: remote.state === "connected" ? "#9ff0c4" : win.dim(.55)
                                    text: remote.state === "connected" ? qsTr("mouse and keyboard connected")
                                        : remote.state === "waiting" ? qsTr("waiting for the iPhone")
                                        : remote.state === "starting" ? qsTr("starting Bluetooth")
                                        : qsTr("Bluetooth mouse and keyboard")
                                }
                            }
                            Toggle {
                                on: remote.enabled
                                Accessible.name: qsTr("Control the iPhone")
                                onClicked: remote.enabled = !remote.enabled
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: remote.state === "waiting" || remote.state === "connected"
                            wrapMode: Text.WordWrap
                            lineHeight: 1.3
                            font.pixelSize: 12
                            color: win.dim(.75)
                            text: remote.state === "waiting"
                                ? qsTr("On the iPhone, open Settings > Bluetooth and choose “AirCast”. For a pointer, turn on Settings > Accessibility > Touch > AssistiveTouch.")
                                : qsTr("Move the mouse over the picture to control the iPhone. Ctrl works as ⌘.")
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            visible: remote.state === "connected"
                            text: qsTr("Home")
                            onClicked: remote.home()
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: remote.error !== ""
                            wrapMode: Text.WordWrap
                            color: "#ffb3bd"
                            font.pixelSize: 12
                            text: remote.error
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: backend.error !== ""
                    wrapMode: Text.WordWrap
                    color: "#ffb3bd"
                    font.pixelSize: 12
                    text: backend.error
                }

                // First launch: the user decides about the firewall.
                Card {
                    Layout.fillWidth: true
                    visible: backend.firewall === "ask" || backend.firewall === "working"
                    implicitHeight: firewall.implicitHeight + 24
                    border.width: 1
                    border.color: Qt.alpha(theme.accent, .45)
                    ColumnLayout {
                        id: firewall
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8
                        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Allow AirPlay through the firewall?"); font.pixelSize: 13; font.weight: Font.DemiBold; color: win.ink }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            lineHeight: 1.3
                            color: win.dim(.75)
                            text: qsTr("Apple devices can't reach this computer while the firewall blocks AirPlay. This opens %1 with ufw; your password will be asked.").arg(backend.firewallPorts)
                        }
                        RowLayout {
                            spacing: 6
                            FlatButton {
                                Layout.fillWidth: true; Layout.preferredWidth: 1
                                enabled: backend.firewall === "ask"
                                base: "#eef1f8"; ink: "#10121f"
                                text: backend.firewall === "working" ? qsTr("Waiting…") : qsTr("Allow")
                                onClicked: backend.allowFirewall()
                            }
                            FlatButton {
                                Layout.fillWidth: true; Layout.preferredWidth: 1
                                enabled: backend.firewall === "ask"
                                text: qsTr("Not now")
                                onClicked: backend.declineFirewall()
                            }
                        }
                    }
                }

                // Idle: how to connect.
                Item {
                    Layout.fillWidth: true
                    visible: win.expanded && backend.phase === "idle"
                    implicitHeight: idleText.implicitHeight + 32
                    Shape {
                        id: dashes
                        anchors.fill: parent
                        ShapePath {
                            strokeColor: Qt.rgba(1, 1, 1, .16); strokeWidth: 1; fillColor: "transparent"
                            strokeStyle: ShapePath.DashLine; dashPattern: [4, 3]
                            PathRectangle { x: .5; y: .5; width: dashes.width - 1; height: dashes.height - 1; radius: 10 }
                        }
                    }
                    Label {
                        id: idleText
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                        wrapMode: Text.WordWrap
                        lineHeight: 1.3
                        font.pixelSize: 12
                        color: win.dim(.75)
                        text: backend.visible
                            ? qsTr("Waiting. On the iPhone, open Control Center, tap Screen Mirroring, then choose “%1”.").arg(backend.name)
                            : qsTr("Turn visibility on so Apple devices can see this computer.")
                    }
                }

                FlatButton {
                    Layout.fillWidth: true
                    visible: win.expanded && backend.firewall === "declined" && backend.phase === "idle"
                    base: "transparent"; ink: win.dim(.6)
                    text: qsTr("Allow AirPlay through the firewall…")
                    onClicked: backend.allowFirewall()
                }

                // Pairing: the code to type on the phone.
                Card {
                    Layout.fillWidth: true
                    visible: backend.phase === "pairing"
                    implicitHeight: pairing.implicitHeight + 28
                    color: Qt.rgba(0, 0, 0, .22)
                    border.width: 1
                    border.color: Qt.alpha(theme.accent, .45)
                    ColumnLayout {
                        id: pairing
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; topMargin: 14 }
                        spacing: 12
                        RowLayout {
                            spacing: 10
                            Avatar {}
                            Column {
                                Layout.fillWidth: true
                                Label { text: backend.clientName || qsTr("Apple device"); font.pixelSize: 13; font.weight: Font.DemiBold; color: win.ink }
                                Mono { text: qsTr("wants to mirror its screen") }
                            }
                        }
                        Caption { Layout.alignment: Qt.AlignHCenter; text: qsTr("Code to enter on the iPhone") }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: backend.pin
                            font.family: win.mono; font.pixelSize: 32; font.weight: Font.DemiBold; font.letterSpacing: 10
                            color: win.ink
                        }
                        FlatButton { Layout.fillWidth: true; base: Qt.rgba(1, 1, 1, .08); text: qsTr("Refuse"); onClicked: backend.disconnectClient() }
                    }
                }

                // Connected: who, for how long, and the way out.
                Card {
                    Layout.fillWidth: true
                    visible: win.expanded && win.connected
                    implicitHeight: live.implicitHeight + 24
                    color: Qt.rgba(94 / 255, 234 / 255, 160 / 255, .07)
                    border.width: 1
                    border.color: Qt.rgba(94 / 255, 234 / 255, 160 / 255, .28)
                    ColumnLayout {
                        id: live
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 10
                        RowLayout {
                            spacing: 10
                            Avatar {}
                            Column {
                                Layout.fillWidth: true
                                Label { text: backend.clientName; font.pixelSize: 13; font.weight: Font.DemiBold; color: win.ink }
                                Mono { text: qsTr("mirroring"); color: "#9ff0c4" }
                            }
                        }
                        RowLayout {
                            spacing: 6
                            Stat { label: qsTr("Time"); value: win.clock(backend.elapsed) }
                            Stat { label: qsTr("Frames/s"); value: backend.fps }
                            Stat {
                                label: qsTr("Orient.")
                                value: backend.videoSize.width <= 0 ? "–" : win.landscape ? qsTr("landscape") : qsTr("portrait")
                            }
                        }
                        RowLayout {
                            spacing: 6
                            FlatButton { Layout.fillWidth: true; Layout.preferredWidth: 1; text: qsTr("Full screen"); onClicked: win.toggleFull() }
                            FlatButton {
                                Layout.fillWidth: true; Layout.preferredWidth: 1
                                base: Qt.rgba(1, 90 / 255, 110 / 255, .18); ink: "#ffb3bd"
                                text: qsTr("Disconnect")
                                onClicked: backend.disconnectClient()
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#e60b1020"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
            GridLayout {
                anchors.centerIn: parent
                columns: 2
                columnSpacing: 20
                rowSpacing: 10
                Mono { text: "Space"; font.pixelSize: 12; color: win.ink } Label { text: qsTr("Show or hide this computer on the network"); color: win.ink }
                Mono { text: "F"; font.pixelSize: 12; color: win.ink } Label { text: qsTr("Full screen"); color: win.ink }
                Mono { text: "Esc"; font.pixelSize: 12; color: win.ink } Label { text: qsTr("Exit full screen"); color: win.ink }
                Mono { text: "D"; font.pixelSize: 12; color: win.ink } Label { text: qsTr("Disconnect"); color: win.ink }
                Mono { text: "?"; font.pixelSize: 12; color: win.ink } Label { text: qsTr("Show or hide this help"); color: win.ink }
                Mono { text: "Q"; font.pixelSize: 12; color: win.ink } Label { text: qsTr("Quit"); color: win.ink }
            }
        }
    }
}
