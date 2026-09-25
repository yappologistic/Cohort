import QtQuick
import QtQuick.Layouts

// The keyboard's four lighting zones, left to right as they sit under the
// keys, drawn as a connected group so the row reads as one keyboard.
//
// The shapes are the connected button group's (ConnectedButtonGroupSmall
// Tokens, ButtonGroupDefaults): full outer corners on the ends, 8dp inner
// corners, 4dp while pressed, 2dp between members, and a chosen member
// rounding fully. Each member is filled with its zone's colour, because the
// colour is the content. A chosen zone also carries a check, since Material
// asks for selection to be shown by more than colour.
Item {
    id: strip
    property var zones: ["#ffffff", "#ffffff", "#ffffff", "#ffffff"]
    property var selected: [true, true, true, true]
    signal toggled(int index)
    Layout.fillWidth: true
    implicitHeight: 56
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Zones")

    // Ink that holds its contrast on a zone's own colour.
    function inkOn(c) {
        const lum = 0.2126*c.r + 0.7152*c.g + 0.0722*c.b
        return lum > 0.5 ? "#000000" : "#ffffff"
    }

    Row {
        anchors.fill: parent
        spacing: 2
        Repeater {
            model: 4
            Zone {
                required property int index
                width: (strip.width - 6)/4
                height: strip.height
                zoneIndex: index
            }
        }
    }

    component Zone: Item {
        id: zone
        property int zoneIndex: 0
        readonly property bool chosen: strip.selected[zoneIndex] === true
        readonly property color fill: strip.zones[zoneIndex]
        readonly property bool leading: zoneIndex === 0
        readonly property bool trailing: zoneIndex === 3
        objectName: "zone" + (zoneIndex + 1)
        activeFocusOnTab: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: qsTr("Zone %1").arg(zoneIndex + 1)
        Accessible.checkable: true
        Accessible.checked: chosen
        Keys.onSpacePressed: strip.toggled(zoneIndex)
        Keys.onReturnPressed: strip.toggled(zoneIndex)

        Rectangle {
            id: shape
            anchors.fill: parent
            readonly property real full: Theme.shapeFull(height)
            readonly property real inner: zone.chosen ? full : press.pressed ? Theme.shapeExtraSmall : Theme.shapeSmall
            topLeftRadius: zone.leading || zone.chosen ? full : inner
            bottomLeftRadius: topLeftRadius
            topRightRadius: zone.trailing || zone.chosen ? full : inner
            bottomRightRadius: topRightRadius
            color: zone.fill
            border.width: 1
            // A zone set to the surface's own colour would vanish without an
            // edge; the outline variant keeps every zone findable.
            border.color: Theme.outlineVariant
            Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.effectsCurve } }
            // ToggleButton.kt morphs connected shapes on FastSpatial.
            Behavior on topLeftRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            Behavior on topRightRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            Rectangle {
                anchors.fill: parent
                topLeftRadius: parent.topLeftRadius; bottomLeftRadius: parent.bottomLeftRadius
                topRightRadius: parent.topRightRadius; bottomRightRadius: parent.bottomRightRadius
                color: strip.inkOn(zone.fill)
                opacity: press.pressed ? Theme.pressedOpacity : press.containsMouse ? Theme.hoverOpacity : 0
            }
            Icon {
                anchors.centerIn: parent
                name: "check"
                size: 24
                ink: strip.inkOn(zone.fill)
                opacity: zone.chosen ? 1 : 0
                scale: zone.chosen ? 1 : 0.6
                Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
                Behavior on scale { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
                Accessible.ignored: true
            }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: Theme.shapeInside(shape.topLeftRadius, -3)
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: zone.activeFocus
        }
        MouseArea {
            id: press
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { zone.forceActiveFocus(); strip.toggled(zone.zoneIndex) }
        }
    }
}
