import QtQuick

// Material 3 snackbar.
//
// Material's rules for it: one line of text at 48dp tall, or two at 68dp; at
// most one action, in the inverse primary; a close affordance only when there
// is no action; and it sits against the theme rather than in it, on the
// inverse surface, because a message about what just happened should not look
// like another part of the page.
//
// It enters and leaves by fading and growing from the bottom, which is
// Material's own transition for it, and it never takes focus: a snackbar that
// stole the keyboard would interrupt the typing that produced it.
Item {
    id: snackbar
    objectName: "snackbar"

    property string message: ""
    property string action: "Undo"
    property bool actionEnabled: true
    signal triggered()
    signal dismissed()

    readonly property bool present: message.length > 0
    // Material caps the snackbar at 600dp and lets it shrink to the window,
    // keeping its 16dp margins.
    readonly property real maxWidth: Math.min(Theme.snackbarMaxWidth, parent ? parent.width - 32 : 600)

    implicitWidth: Math.min(maxWidth, body.implicitWidth + (actionButton.visible ? actionButton.width + 8 : 0) + 32)
    implicitHeight: Math.max(Theme.snackbarHeight, body.implicitHeight + 24)
    visible: opacity > 0
    opacity: present ? 1 : 0
    scale: present ? 1 : 0.9
    transformOrigin: Item.Bottom
    // A message nobody can reach is not an announcement. The live region tells
    // assistive technology that something appeared without moving focus to it.
    Accessible.role: Accessible.AlertMessage
    Accessible.name: message

    Behavior on opacity { NumberAnimation { duration: Theme.springEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springEffects } }
    Behavior on scale { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }

    Rectangle {
        anchors.fill: parent
        // Material maps the snackbar to the extra-small corner, which is the
        // one shape that reads as a strip rather than as a card.
        radius: Theme.shapeExtraSmall
        color: Theme.inverseSurface
        MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
    }

    CohortText {
        id: body
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.right: actionButton.visible ? actionButton.left : parent.right
        anchors.rightMargin: actionButton.visible ? 8 : 16
        anchors.verticalCenter: parent.verticalCenter
        text: snackbar.message
        font.pixelSize: Theme.bodyMedium
        typeRole: "bodyMedium"
        color: Theme.inverseSurfaceText
        maximumLineCount: 2
        wrapMode: Text.WordWrap
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    MButton {
        id: actionButton
        objectName: "snackbarAction"
        visible: snackbar.action.length > 0 && snackbar.actionEnabled
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        size: "xsmall"
        text: snackbar.action
        // Material draws the snackbar's action in the inverse primary, which is
        // the accent the other theme would have used, so it holds its contrast
        // against the inverse surface underneath it.
        ink: Theme.inversePrimary
        onClicked: snackbar.triggered()
    }
}
