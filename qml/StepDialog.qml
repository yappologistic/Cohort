import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One step of the fan curve: the temperatures it runs up to and how quickly
// the fans move to its speed.
//
// Each threshold starts at the step below it, because the firmware takes
// temperatures that rise from step to step and nothing else
// (legion-laptop.c). Raising one past the steps above carries them up with
// it: the machine layer raises every later step to at least the one before,
// so a curve whose first steps share a threshold can still be spread out.
// The last step runs to the top of the scale, so it has no thresholds to
// set. The ramp is the first fan's accel and decel, from 2 to 5 where lower
// is faster, set together.
MDialog {
    id: dialog
    objectName: "stepDialog"
    property int step: 0
    readonly property var points: machine.fanCurve.points || []
    readonly property var point: points[step] || ({})
    readonly property bool last: step === points.length - 1
    width: fitWidth(420)
    modal: true
    title: last ? qsTr("Last step") : qsTr("Step %1").arg(step + 1)
    standardButtons: Dialog.Cancel | Dialog.Ok
    acceptText: qsTr("Save")

    function openFor(index) {
        step = index
        for (const row of [cpu, gpu, ic]) row.reset()
        ramp.value = point.accel || 2
        open()
    }
    onAccepted: {
        const next = points.map(p => Object.assign({}, p))
        const edited = next[step]
        if (!last) {
            edited.cpu = Math.round(cpu.value)
            edited.gpu = Math.round(gpu.value)
            edited.ic = Math.round(ic.value)
        }
        if (point.accel > 0) {
            edited.accel = Math.round(ramp.value)
            edited.decel = Math.round(ramp.value)
        }
        machine.setFanCurve(next)
    }

    component Threshold: ColumnLayout {
        id: row
        property string sensor: ""
        property string label: ""
        property alias value: slider.value
        function reset() { slider.value = dialog.point[sensor] || 0 }
        visible: !dialog.last
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            CohortText {
                Layout.fillWidth: true
                text: row.label
                font.pixelSize: Theme.bodyLarge
                typeRole: "bodyLarge"
            }
            CohortText {
                text: qsTr("%1 °C").arg(Math.round(slider.value))
                font.pixelSize: Theme.bodyLarge
                typeRole: "bodyLarge"
                color: Theme.muted
            }
        }
        MSlider {
            id: slider
            objectName: "step_" + row.sensor
            Layout.fillWidth: true
            // From the step below to the top of the scale but one, which is
            // the last step's.
            from: dialog.step > 0 ? (dialog.points[dialog.step - 1] || {})[row.sensor] || 0 : 0
            to: 126
            stepSize: 1
            snapMode: MSlider.SnapAlways
            valueLabel: qsTr("%1 °C").arg(Math.round(value))
            Accessible.name: row.label
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceLarge
        Threshold { id: cpu; sensor: "cpu"; label: qsTr("CPU up to") }
        Threshold { id: gpu; sensor: "gpu"; label: qsTr("GPU up to") }
        Threshold { id: ic; sensor: "ic"; label: qsTr("Chipset up to") }
        ColumnLayout {
            visible: dialog.point.accel > 0
            Layout.fillWidth: true
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                CohortText {
                    Layout.fillWidth: true
                    text: qsTr("Ramp")
                    font.pixelSize: Theme.bodyLarge
                    typeRole: "bodyLarge"
                }
                CohortText {
                    text: [qsTr("Fastest"), qsTr("Fast"), qsTr("Slow"), qsTr("Slowest")][Math.round(ramp.value) - 2] || ""
                    font.pixelSize: Theme.bodyLarge
                    typeRole: "bodyLarge"
                    color: Theme.muted
                }
            }
            MSlider {
                id: ramp
                objectName: "step_ramp"
                Layout.fillWidth: true
                from: 2; to: 5; stepSize: 1
                snapMode: MSlider.SnapAlways
                Accessible.name: qsTr("Ramp")
            }
        }
    }
}
