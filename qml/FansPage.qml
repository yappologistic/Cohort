import QtQuick
import QtQuick.Layouts

Page {
    id: page
    objectName: "fansPage"
    title: qsTr("Fans")

    function celsius(value) { return isNaN(value) ? "–" : qsTr("%1 °C").arg(Math.round(value)) }

    // What the fans are answering to, and how fast they are answering.
    GridLayout {
        objectName: "readouts"
        Layout.fillWidth: true
        columns: page.width < 600 ? 2 : 4
        columnSpacing: Theme.space
        rowSpacing: Theme.space
        Readout {
            objectName: "cpuTemperature"
            symbol: "cpu"
            label: qsTr("CPU")
            value: page.celsius(machine.cpuTemperature)
        }
        Readout {
            objectName: "gpuTemperature"
            visible: machine.gpuPresent
            symbol: "gpu"
            label: qsTr("GPU")
            // A discrete GPU that has powered down has no temperature to give
            // without being woken, and is left asleep.
            value: machine.gpuAsleep ? qsTr("Asleep") : page.celsius(machine.gpuTemperature)
        }
        Repeater {
            model: machine.fans
            Readout {
                required property var modelData
                symbol: "fan"
                label: modelData.label
                value: qsTr("%1 rpm").arg(modelData.rpm.toLocaleString(Qt.locale(), "f", 0))
            }
        }
    }

    Section {
        visible: machine.fanCurve.available === true
        label: qsTr("Curve")
        FanCurve { objectName: "fanCurve" }
    }

    Section {
        readonly property bool any: machine.switches["legion/fan_fullspeed"] !== undefined
                                    || machine.switches["legion/lockfancontroller"] !== undefined
        visible: any
        SwitchRow {
            key: "legion/fan_fullspeed"
            headline: qsTr("Full speed")
            supporting: qsTr("Runs every fan at its maximum")
        }
        SwitchRow {
            key: "legion/lockfancontroller"
            headline: qsTr("Hold current speed")
            supporting: qsTr("Stops the fans reacting to temperature")
        }
    }

    // Fan control reaches Linux only through LenovoLegionLinux's kernel
    // module; the mainline drivers read the fans but do not curve them.
    EmptyState {
        objectName: "fanModuleMissing"
        visible: !machine.legionModule
        symbol: "fan"
        headline: qsTr("Fan curves need LenovoLegionLinux")
        supporting: qsTr("Install its kernel module to choose how the fans respond to temperature.")
        actionText: qsTr("Learn how")
        onAction: Qt.openUrlExternally("https://github.com/johnfanv2/LenovoLegionLinux#installation")
    }
}
