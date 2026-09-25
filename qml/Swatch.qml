import QtQuick

// A colour to pick, as a 32dp disc inside a 48dp target.
//
// The disc takes the full shape and an outline variant edge so white and
// near-surface colours still have a boundary; the state layer is a 40dp ring
// around it, Material's selection-control state layer.
Item {
    id: swatch
    property color colour: "white"
    signal picked()
    implicitWidth: Theme.selectionTarget
    implicitHeight: Theme.selectionTarget
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: colour.toString()
    Keys.onSpacePressed: picked()
    Keys.onReturnPressed: picked()

    Rectangle {
        anchors.centerIn: parent
        width: Theme.selectionStateLayer; height: width
        radius: Theme.shapeFull(width)
        color: Theme.text
        opacity: area.pressed ? Theme.pressedOpacity : area.containsMouse || swatch.activeFocus ? Theme.hoverOpacity : 0
        Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    }
    Rectangle {
        anchors.centerIn: parent
        width: 32; height: 32
        radius: Theme.shapeFull(width)
        color: swatch.colour
        border.width: 1
        border.color: Theme.outlineVariant
        scale: area.pressed ? 0.9 : 1
        Behavior on scale { enabled: app.motion; NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
    }
    Rectangle {
        anchors.centerIn: parent
        width: Theme.selectionStateLayer + 4; height: width
        radius: Theme.shapeFull(width)
        color: "transparent"; border.width: 2; border.color: Theme.focusRing
        visible: swatch.activeFocus
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: swatch.picked()
    }
}
