import QtQuick
import QtQuick.Layouts

Page {
    id: page
    objectName: "batteryPage"
    title: qsTr("Battery")

    readonly property var battery: machine.battery
    // The charge_types words, in the names Lenovo gives the modes.
    readonly property var names: ({Long_Life: qsTr("Conservation"), Standard: qsTr("Standard"), Fast: qsTr("Rapid")})
    readonly property var explanations: ({
        Long_Life: qsTr("Stops charging early to make the battery last more years"),
        Standard: qsTr("Charges fully at the normal rate"),
        Fast: qsTr("Charges faster, which wears the battery sooner")
    })
    readonly property bool busy: machine.pending.indexOf("charge") >= 0
    property string requested: ""
    readonly property string shown: busy && requested.length ? requested : machine.chargeMode

    function statusLine() {
        const b = battery
        const watts = b.watts > 0 ? qsTr("%1 W").arg(b.watts.toLocaleString(Qt.locale(), "f", 1)) : ""
        const join = (a, c) => c.length ? a + " · " + c : a
        switch (b.status) {
        case "Charging": return join(qsTr("Charging"), watts)
        case "Discharging": return join(qsTr("On battery"), watts)
        case "Full": return qsTr("Full")
        case "Not charging": return b.ac ? qsTr("Plugged in, not charging") : qsTr("Not charging")
        default: return b.ac ? qsTr("Plugged in") : ""
        }
    }

    // The charge, set as the page's figure: display medium, with what the
    // battery is doing under it.
    ColumnLayout {
        visible: page.battery.present === true
        Layout.fillWidth: true
        spacing: 0
        CohortText {
            objectName: "batteryPercent"
            text: qsTr("%1%").arg(page.battery.percent)
            font.pixelSize: Theme.displayMedium
            Layout.fillWidth: true
        }
        CohortText {
            objectName: "batteryStatus"
            text: page.statusLine()
            visible: text.length > 0
            font.pixelSize: Theme.bodyLarge
            typeRole: "bodyLarge"
            color: Theme.muted
            Layout.fillWidth: true
        }
    }

    Section {
        visible: machine.chargeModes.length > 0
        label: qsTr("Charging")
        MSegmentedControl {
            objectName: "chargeModes"
            Layout.fillWidth: true
            accessibleName: qsTr("Charging mode")
            options: machine.chargeModes.map(m => ({key: m, label: page.names[m] || m, name: "charge_" + m}))
            value: page.shown
            onChosen: key => { page.requested = key; machine.setChargeMode(key) }
        }
        CohortText {
            objectName: "chargeExplanation"
            Layout.fillWidth: true
            Layout.topMargin: Theme.space
            Layout.leftMargin: Theme.listLeadingSpace
            text: page.explanations[page.shown] || ""
            font.pixelSize: Theme.bodyMedium
            typeRole: "bodyMedium"
            color: Theme.muted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    Section {
        visible: machine.switches["usb-charging"] !== undefined || page.battery.health !== undefined
        SwitchRow {
            key: "usb-charging"
            headline: qsTr("Always-on USB")
            supporting: qsTr("Charges devices while the laptop sleeps or is off")
        }
        MListItem {
            objectName: "batteryHealth"
            visible: page.battery.health !== undefined
            interactive: false
            headline: qsTr("Health")
            // How much of its design capacity the battery still holds, and
            // how many times it has been through a full charge.
            supporting: qsTr("%1% of original capacity").arg(page.battery.health)
                        + (page.battery.cycles !== undefined ? " · " + qsTr("%n cycles", "", page.battery.cycles) : "")
        }
    }
}
