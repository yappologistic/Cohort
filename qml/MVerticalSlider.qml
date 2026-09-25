import QtQuick
import QtQuick.Controls

// Material 3 slider, vertical, at the extra small size.
//
// The same anatomy as MSlider turned on its side, as Compose's VerticalSlider
// turns it: a 16dp track fully rounded at its ends, a handle that is a 4dp
// bar 44dp across (SliderTokens.HandleWidth and HandleHeight, swapped), a 6dp
// gap held open either side of the handle, and the 2dp inside corners that
// face it (Slider.kt TrackInsideCornerSize). The track filled so far is
// primary and the rest secondary container; the handle narrows to 2dp under
// a press or focus.
Slider {
    id: slider
    orientation: Qt.Vertical
    readonly property real track: Theme.sliderTrack.xsmall
    readonly property real handleLength: Theme.sliderHandleHeight.xsmall
    implicitWidth: handleLength
    implicitHeight: 160
    padding: 0
    hoverEnabled: true
    // Where the handle's centre sits, measured down from the top.
    readonly property real handleCentre: visualPosition*(availableHeight-Theme.sliderHandle)+Theme.sliderHandle/2

    background: Item {
        x: slider.leftPadding + (slider.availableWidth-width)/2
        y: slider.topPadding
        width: slider.track; height: slider.availableHeight
        Rectangle {
            objectName: "verticalInactiveTrack"
            width: parent.width
            height: Math.max(0, slider.handleCentre-Theme.sliderHandle/2-Theme.sliderGap)
            radius: Theme.shapeFull(width)
            bottomLeftRadius: 2; bottomRightRadius: 2
            color: slider.enabled ? Theme.secondaryContainer : Theme.sliderQuiet(Theme.disabledTrackOpacity)
        }
        Rectangle {
            objectName: "verticalStop"
            // The stop indicator marks the end of the track while the value
            // is short of it, 4dp and in the active track's ink (Slider.kt
            // draws it with trackColor(active = true)), 6dp in from the end.
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.sliderGap
            width: Theme.sliderStop; height: Theme.sliderStop
            radius: Theme.shapeFull(width)
            color: slider.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
            visible: slider.handleCentre - Theme.sliderHandle/2 - Theme.sliderGap > y + height
        }
        Rectangle {
            objectName: "verticalActiveTrack"
            y: Math.min(parent.height, slider.handleCentre+Theme.sliderHandle/2+Theme.sliderGap)
            width: parent.width; height: parent.height-y
            radius: Theme.shapeFull(width)
            topLeftRadius: 2; topRightRadius: 2
            color: slider.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
        }
    }
    handle: Item {
        x: slider.leftPadding + (slider.availableWidth-width)/2
        y: slider.topPadding + slider.visualPosition*(slider.availableHeight-Theme.sliderHandle)
        width: slider.handleLength; height: Theme.sliderHandle
        Rectangle {
            objectName: "verticalHandle"
            anchors.centerIn: parent
            width: parent.width
            height: slider.pressed || slider.visualFocus ? Theme.sliderHandlePressed : Theme.sliderHandle
            radius: Theme.shapeFull(height)
            color: slider.enabled ? Theme.primary : Theme.sliderQuiet(Theme.disabledContentOpacity)
            Behavior on height { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width+4; height: parent.height+12; radius: Theme.shapeSmall
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: slider.visualFocus
        }
    }
}
