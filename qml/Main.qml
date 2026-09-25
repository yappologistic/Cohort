import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "window"
    width: 1000
    height: 700
    minimumWidth: 360
    minimumHeight: 480
    visible: true
    title: "Cohort"
    color: Theme.surface

    // The destination on screen. Four questions a laptop tool answers: how
    // hard it runs, how it charges, how it cools and how its keyboard looks.
    property string page: "power"
    readonly property var destinations: [
        {key: "power", symbol: "speed", label: qsTr("Power")},
        {key: "battery", symbol: "battery", label: qsTr("Battery")},
        {key: "fans", symbol: "fan", label: qsTr("Fans")},
        {key: "keyboard", symbol: "keyboard", label: qsTr("Keyboard")}
    ]
    // Material's window size classes: below 600dp is compact, where the
    // destinations move from a rail at the side to a bar along the bottom.
    readonly property bool compact: width < 600

    // Sampling follows what can be seen.
    Binding {
        target: machine
        property: "active"
        value: window.visible && window.visibility !== Window.Minimized
    }

    Shortcut { sequences: [StandardKey.Quit]; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settings.open() }
    Repeater {
        model: window.destinations.length
        Item {
            required property int index
            Shortcut { sequence: "Ctrl+" + (index+1); onActivated: window.page = window.destinations[index].key }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Material's collapsed rail: 96dp, destinations from 44dp down with
        // 4dp between them (NavigationRailCollapsedTokens, WideNavigationRail
        // WNRTopPadding), on the surface itself. The window's own action,
        // settings, sits at its foot.
        ColumnLayout {
            objectName: "navigationRail"
            visible: !window.compact
            Layout.preferredWidth: 96
            Layout.fillHeight: true
            Layout.topMargin: 44
            spacing: 4
            Accessible.role: Accessible.PageTabList
            Accessible.name: qsTr("Navigation")
            Repeater {
                model: window.destinations
                MNavigationItem {
                    required property var modelData
                    objectName: "rail_" + modelData.key
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 64
                    symbol: modelData.symbol
                    text: modelData.label
                    selected: window.page === modelData.key
                    onClicked: window.page = modelData.key
                }
            }
            Item { Layout.fillHeight: true }
            MButton {
                objectName: "railSettings"
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 20
                symbol: "settings"
                tip: qsTr("Settings")
                onClicked: settings.open()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                Repeater {
                    model: window.destinations
                    Loader {
                        id: pageLoader
                        required property var modelData
                        readonly property bool current: window.page === modelData.key
                        anchors.fill: parent
                        // A destination is built the first time it is visited
                        // and kept, so returning to it keeps its scroll.
                        active: current || item !== null
                        visible: opacity > 0
                        // Material's fade through between top-level
                        // destinations: the outgoing page fades out quickly,
                        // then the incoming one fades in while it settles
                        // from 92% scale (motion guidelines, Transitions,
                        // Fade through). Opacity is an effect and takes the
                        // effects springs; the scale is spatial.
                        opacity: current ? 1 : 0
                        scale: current ? 1 : 0.92
                        Behavior on opacity {
                            NumberAnimation {
                                duration: pageLoader.current ? Theme.springEffectsMs : Theme.springFastEffectsMs
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: pageLoader.current ? Theme.springEffects : Theme.springFastEffects
                            }
                        }
                        Behavior on scale {
                            enabled: app.motion
                            NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial }
                        }
                        sourceComponent: ({power: powerPage, battery: batteryPage, fans: fansPage, keyboard: keyboardPage})[modelData.key]
                    }
                }
            }

            // Material's flexible navigation bar, 64dp on the surface
            // container, with the stacked destination items the rail uses.
            Rectangle {
                objectName: "navigationBar"
                visible: window.compact
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: Theme.container
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Repeater {
                        model: window.destinations
                        MNavigationItem {
                            required property var modelData
                            objectName: "bar_" + modelData.key
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            symbol: modelData.symbol
                            text: modelData.label
                            selected: window.page === modelData.key
                            onClicked: window.page = modelData.key
                        }
                    }
                }
            }
        }
    }

    // A navigation bar holds destinations and nothing else, so in a compact
    // window the window's own action moves to the top trailing corner, where
    // a small app bar keeps its actions (AppBarTokens: 4dp from the edge,
    // on a 64dp bar).
    MButton {
        objectName: "compactSettings"
        visible: window.compact
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 4
        anchors.topMargin: 8
        symbol: "settings"
        tip: qsTr("Settings")
        onClicked: settings.open()
    }

    MSnackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: window.compact ? 64 + 16 : 16
        action: ""
        Timer { id: snackbarTimer; interval: Theme.snackbarDuration; onTriggered: snackbar.message = "" }
        Connections {
            target: machine
            function onFailed(message) { snackbar.message = message; snackbarTimer.restart() }
        }
    }

    Component { id: powerPage; PowerPage {} }
    Component { id: batteryPage; BatteryPage {} }
    Component { id: fansPage; FansPage {} }
    Component { id: keyboardPage; KeyboardPage {} }

    SettingsDialog { id: settings }
}
