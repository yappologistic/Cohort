import QtQuick

// A list row that carries a machine switch.
//
// The row reads its state from the machine rather than holding one, so what
// it shows is what the kernel last reported. While a change is under way the
// switch shows where it is going and further presses wait for it; if the
// firmware refuses, the next read puts the switch back where it really is.
MListItem {
    id: row
    // The helper's key for the control, like "fn-lock" or "legion/winkey".
    property string key: ""
    readonly property bool present: machine.switches[key] !== undefined
    readonly property bool busy: machine.pending.indexOf(key) >= 0
    property bool requested: false
    readonly property bool shown: busy ? requested : machine.switches[key] === true
    visible: present
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: shown
    onClicked: request(!shown)
    function request(on) {
        if (busy) return
        requested = on
        machine.setSwitch(key, on)
    }
    MSwitch {
        objectName: "switch_" + row.key
        anchors.verticalCenter: parent.verticalCenter
        Accessible.name: row.headline
        checked: row.shown
        // A Switch flips itself on a press, which would cut it loose from
        // the machine. The press becomes a request and the binding returns.
        onToggled: {
            row.request(checked)
            checked = Qt.binding(() => row.shown)
        }
    }
}
