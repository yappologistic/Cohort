import QtQuick
import QtQuick.Controls

// A hue, chosen on Material's slider anatomy with the hues themselves as the
// track.
//
// Material publishes no colour picker, so this keeps the slider it does
// publish: the 16dp fully rounded track, the 4dp handle bar as tall as the
// target, narrowing to 2dp under a press, and the value shown above the handle
// while it moves, here as the colour itself. The track is the one place the
// window shows colours that are not scheme roles, because the colours are
// what is being chosen.
Slider {
    id: slider
    from: 0; to: 359; stepSize: 1
    implicitHeight: Theme.sliderHandleHeight.xsmall
    hoverEnabled: true
    readonly property color colour: Qt.hsva(value/360, 1, 1, 1)
    Accessible.name: qsTr("Hue")

    background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + (slider.availableHeight-height)/2
        width: slider.availableWidth
        height: Theme.sliderTrack.xsmall
        radius: Theme.shapeFull(height)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0/6; color: "#ff0000" }
            GradientStop { position: 1/6; color: "#ffff00" }
            GradientStop { position: 2/6; color: "#00ff00" }
            GradientStop { position: 3/6; color: "#00ffff" }
            GradientStop { position: 4/6; color: "#0000ff" }
            GradientStop { position: 5/6; color: "#ff00ff" }
            GradientStop { position: 6/6; color: "#ff0000" }
        }
    }
    handle: Item {
        x: slider.leftPadding + slider.visualPosition*(slider.availableWidth-Theme.sliderHandle)
        y: slider.topPadding + (slider.availableHeight-height)/2
        width: Theme.sliderHandle
        height: Theme.sliderHandleHeight.xsmall
        Rectangle {
            anchors.centerIn: parent
            width: slider.pressed || slider.visualFocus ? Theme.sliderHandlePressed : Theme.sliderHandle
            height: parent.height
            radius: Theme.shapeFull(width)
            color: Theme.text
            Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width+12; height: parent.height+4; radius: Theme.shapeSmall
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: slider.visualFocus
        }
        // The value indicator: Material holds it 12dp above the handle
        // (SliderTokens.ValueIndicatorActiveBottomSpace).
        Rectangle {
            visible: slider.pressed
            anchors.horizontalCenter: parent.horizontalCenter
            y: -height - 12
            width: 32; height: 32
            radius: Theme.shapeFull(width)
            color: slider.colour
            border.width: 2
            border.color: Theme.inverseSurface
        }
    }
}
