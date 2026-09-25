import QtQuick
import QtQuick.Layouts

// The fan curve: one vertical slider per step of the firmware's table.
//
// legion_laptop publishes the table as up to ten points, each a fan speed
// that applies until the CPU passes the point's upper temperature
// (pwm1_auto_pointN_pwm and pwm1_auto_pointN_temp). The sliders stand in that
// order, left to right, with each step's temperature under it and its speed
// over it, so the row reads as the curve it sets.
//
// Speeds are sent on the hwmon PWM scale and shown in RPM where the driver
// says what full speed is (fan1_max). The firmware stores RPM in hundreds, so
// the figure is rounded to the hundred it will actually run at.
Rectangle {
    id: curve
    readonly property var points: machine.fanCurve.points || []
    readonly property int maxRpm: machine.fanCurve.maxRpm || 0
    readonly property bool busy: machine.pending.indexOf("curve") >= 0
    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 32
    radius: Theme.listOuter
    color: Theme.container
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Fan curve")

    function speedText(pwm) {
        if (maxRpm > 0)
            return (Math.round(pwm/255*maxRpm/100)*100).toLocaleString(Qt.locale(), "f", 0)
        return qsTr("%1%").arg(Math.round(pwm/255*100))
    }
    function commit() {
        const speeds = []
        for (let i = 0; i < sliders.count; ++i)
            speeds.push(Math.round(sliders.itemAt(i).value))
        machine.setFanSpeeds(speeds)
    }
    // Every slider returns to the firmware's table once it has answered.
    onBusyChanged: if (!busy) for (let i = 0; i < sliders.count; ++i) sliders.itemAt(i).resync()

    ColumnLayout {
        id: body
        objectName: "curveBody"
        anchors.fill: parent
        anchors.margins: Theme.spaceLarge
        spacing: Theme.space
        RowLayout {
            objectName: "curveRow"
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                id: sliders
                model: curve.points
                ColumnLayout {
                    id: step
                    objectName: "curveStep"
                    required property var modelData
                    required property int index
                    property alias value: slider.value
                    function resync() { slider.value = Qt.binding(() => step.modelData.speed) }
                    // Equal shares of the card, whatever each label measures.
                    // A layout's maximum width defaults to its children's, and
                    // none of these children grows, so without lifting the
                    // maximum fillWidth has nothing to fill (Qt Quick Layouts,
                    // Layout.maximumWidth).
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.maximumWidth: Number.POSITIVE_INFINITY
                    spacing: Theme.space
                    CohortText {
                        objectName: "curveSpeed"
                        Layout.alignment: Qt.AlignHCenter
                        text: curve.speedText(slider.value)
                        font.pixelSize: Theme.labelMedium
                        labelRole: true
                        color: slider.pressed ? Theme.primary : Theme.muted
                    }
                    MVerticalSlider {
                        id: slider
                        objectName: "curvePoint" + (step.index + 1)
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredHeight: 200
                        from: 0; to: 255; stepSize: 1
                        value: step.modelData.speed
                        Accessible.name: qsTr("Fan speed up to %1 °C").arg(step.modelData.cpu)
                        onPressedChanged: if (!pressed) curve.commit()
                        Keys.onReleased: event => { if (!event.isAutoRepeat) curve.commit() }
                    }
                    CohortText {
                        objectName: "curveTemperature"
                        Layout.alignment: Qt.AlignHCenter
                        // The last step runs to the end of the scale; the
                        // firmware requires its upper temperature to be 127.
                        text: step.modelData.cpu >= 127 ? qsTr("Max") : qsTr("%1°").arg(step.modelData.cpu)
                        font.pixelSize: Theme.labelMedium
                        labelRole: true
                        color: Theme.muted
                    }
                }
            }
        }
    }
}
