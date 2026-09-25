import QtQuick
import QtQuick.Layouts

// One firmware power limit: its name, its value, and a slider under them.
//
// The slider is Material's extra small one on the step the firmware
// publishes. While it moves, the value beside the name is the one it will
// send; once it rests, it is the one the firmware reports back.
MListItem {
    id: row
    property var limit: ({})
    interactive: false
    // The slider holds where it was let go until the firmware has answered,
    // then returns to what the firmware reports, whether or not it agreed.
    readonly property bool busy: machine.pending.indexOf(limit.key) >= 0
    onBusyChanged: if (!busy) slider.value = Qt.binding(() => row.limit.value || 0)
    headline: limit.label || ""
    trailingText: Math.round(slider.value) + " " + (limit.unit || "")
    Accessible.role: Accessible.Grouping
    below: MSlider {
        id: slider
        objectName: "limit_" + (row.limit.name || "")
        Layout.fillWidth: true
        from: row.limit.minimum || 0
        to: row.limit.maximum || 1
        stepSize: row.limit.step || 1
        snapMode: MSlider.SnapAlways
        value: row.limit.value || 0
        valueLabel: Math.round(value) + " " + (row.limit.unit || "")
        Accessible.name: row.headline
        onPressedChanged: if (!pressed) commit()
        Keys.onReleased: event => { if (!event.isAutoRepeat) commit() }
        function commit() { machine.setPowerLimit(row.limit.key, Math.round(value)) }
    }
}
