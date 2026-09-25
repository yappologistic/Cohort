import QtQuick
import QtQuick.Layouts

Page {
    id: page
    objectName: "powerPage"
    title: qsTr("Power")

    // The platform profile names the kernel uses, in the words Lenovo prints
    // on the machine: the Fn+Q modes are Quiet, Balanced and Performance, and
    // max-power is the mode Lenovo calls Extreme.
    readonly property var names: ({
        "low-power": qsTr("Quiet"), "quiet": qsTr("Quiet"), "cool": qsTr("Cool"),
        "balanced": qsTr("Balanced"), "balanced-performance": qsTr("Balanced+"),
        "performance": qsTr("Performance"), "max-power": qsTr("Extreme"), "custom": qsTr("Custom")
    })
    readonly property var explanations: ({
        "low-power": qsTr("Quiet fans and a cool chassis, at lower performance"),
        "quiet": qsTr("Quiet fans and a cool chassis, at lower performance"),
        "balanced": qsTr("Adjusts to the work, for everyday use"),
        "balanced-performance": qsTr("More performance, with the fans a little louder"),
        "performance": qsTr("Full performance, with the fans as loud as it needs"),
        "max-power": qsTr("Beyond performance, for plugged-in use"),
        "custom": qsTr("Runs to the limits and fan curve you set")
    })
    readonly property bool busy: machine.pending.indexOf("platform-profile") >= 0
    property string requested: ""
    readonly property string shown: busy && requested.length ? requested : machine.powerProfile

    MSegmentedControl {
        objectName: "profiles"
        visible: machine.powerProfiles.length > 0
        Layout.fillWidth: true
        accessibleName: qsTr("Power mode")
        options: machine.powerProfiles.map(p => ({key: p, label: page.names[p] || p, name: "profile_" + p}))
        value: page.shown
        onChosen: key => { page.requested = key; machine.setPowerProfile(key) }
    }

    CohortText {
        objectName: "profileExplanation"
        visible: text.length > 0
        Layout.fillWidth: true
        Layout.topMargin: -Theme.space
        Layout.leftMargin: Theme.listLeadingSpace
        text: page.explanations[page.shown] || ""
        font.pixelSize: Theme.bodyMedium
        typeRole: "bodyMedium"
        color: Theme.muted
        wrapMode: Text.Wrap
        elide: Text.ElideNone
    }

    EmptyState {
        visible: machine.powerProfiles.length === 0
        symbol: "speed"
        headline: qsTr("No power modes")
        supporting: qsTr("This kernel publishes no platform profile for this laptop.")
    }

    // The firmware's power limits exist only in the custom mode: it refuses a
    // write in any other, and reports the values of the mode it is in.
    Section {
        objectName: "limits"
        label: qsTr("Limits")
        visible: page.shown === "custom" && machine.powerLimits.length > 0
        Repeater {
            model: machine.powerLimits
            LimitRow {
                required property var modelData
                required property int index
                limit: modelData
            }
        }
    }

    // The automatic power mode: one mode on the charger and one on battery.
    // The mode picked by hand stays until the charger next comes or goes.
    Section {
        objectName: "automatic"
        visible: machine.powerProfiles.length > 1
        label: qsTr("Automatic")
        MListItem {
            headline: qsTr("Follow the charger")
            supporting: qsTr("Changes mode when you plug in or unplug")
            Accessible.role: Accessible.CheckBox
            Accessible.checked: machine.automatic
            onClicked: machine.automatic = !machine.automatic
            MSwitch {
                objectName: "automaticSwitch"
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: qsTr("Follow the charger")
                checked: machine.automatic
                onToggled: {
                    machine.automatic = checked
                    checked = Qt.binding(() => machine.automatic)
                }
            }
        }
    }
    ColumnLayout {
        visible: machine.automatic && machine.powerProfiles.length > 1
        Layout.fillWidth: true
        spacing: Theme.space
        CohortText {
            text: qsTr("Plugged in")
            font.pixelSize: Theme.titleSmall
            typeRole: "titleSmall"
            color: Theme.muted
            Layout.leftMargin: Theme.listLeadingSpace
        }
        MSegmentedControl {
            objectName: "automaticAc"
            Layout.fillWidth: true
            accessibleName: qsTr("Mode when plugged in")
            options: machine.powerProfiles.map(p => ({key: p, label: page.names[p] || p, name: "ac_" + p}))
            value: machine.automaticAc
            onChosen: key => machine.automaticAc = key
        }
        CohortText {
            text: qsTr("On battery")
            font.pixelSize: Theme.titleSmall
            typeRole: "titleSmall"
            color: Theme.muted
            Layout.leftMargin: Theme.listLeadingSpace
            Layout.topMargin: Theme.space
        }
        MSegmentedControl {
            objectName: "automaticBattery"
            Layout.fillWidth: true
            accessibleName: qsTr("Mode on battery")
            options: machine.powerProfiles.map(p => ({key: p, label: page.names[p] || p, name: "battery_" + p}))
            value: machine.automaticBattery
            onChosen: key => machine.automaticBattery = key
        }
    }

    Section {
        objectName: "graphics"
        label: qsTr("Graphics")
        readonly property var keys: ["legion/gsync", "legion/overdrive"].filter(k => machine.switches[k] !== undefined)
        visible: keys.length > 0
        SwitchRow {
            key: "legion/gsync"
            headline: qsTr("Hybrid graphics")
            supporting: qsTr("Applies after a restart")
        }
        SwitchRow {
            key: "legion/overdrive"
            headline: qsTr("Display overdrive")
        }
    }
}
