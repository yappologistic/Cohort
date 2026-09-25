import QtQuick
import QtQuick.Shapes

// Material 3 circular progress indicator, indeterminate.
//
// An arc that turns and changes length while work Cohort cannot measure is
// under way, which is every privileged write: it is done when the kernel
// answers and not before. The stroke is 4dp with the arc's ends rounded
// (CircularProgressIndicatorTokens.ActiveThickness, ActiveShape), and the
// container is the caller's size. Qt Quick Shapes draws on the software
// renderer too, which a shader would not.
Item {
    id: ring
    property color ink: Theme.primary
    property real thickness: 4
    implicitWidth: 24; implicitHeight: 24
    Accessible.role: Accessible.ProgressBar
    Accessible.name: "Working"

    property real sweep: 90
    SequentialAnimation on sweep {
        running: ring.visible && app.motion
        loops: Animation.Infinite
        // ProgressIndicator.kt runs the indeterminate ring on RotationDuration,
        // 1332ms, with the head and the tail each taking half of it
        // (HeadAndTailAnimationDuration, 666ms); the arc grows and shrinks
        // between them while the whole ring turns. These are time-based
        // constants in Compose, not springs, so they are carried as they are.
        NumberAnimation { from: 10; to: 300; duration: 666; easing.type: Easing.InOutCubic }
        NumberAnimation { from: 300; to: 10; duration: 666; easing.type: Easing.InOutCubic }
    }
    RotationAnimator on rotation {
        running: ring.visible && app.motion
        loops: Animation.Infinite
        from: 0; to: 360; duration: 1332
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: ring.ink
            strokeWidth: ring.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: ring.width/2; centerY: ring.height/2
                radiusX: (ring.width-ring.thickness)/2; radiusY: radiusX
                startAngle: -90; sweepAngle: ring.sweep
            }
        }
    }
}
