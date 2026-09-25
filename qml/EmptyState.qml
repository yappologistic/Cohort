import QtQuick
import QtQuick.Layouts

// What a section says when the machine cannot offer it, and, where there is
// one, the step that would change that.
//
// It sits on the surface container at the large corner, the same surface
// as the lists around it, so a missing feature reads as part of the page
// rather than as an error. The glyph is the destination's own, in the
// secondary role; the words are title medium over body medium.
Rectangle {
    id: state
    property string symbol: ""
    property string headline: ""
    property string supporting: ""
    property string actionText: ""
    signal action()
    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 48
    radius: Theme.listOuter
    color: Theme.container
    Accessible.role: Accessible.StaticText
    Accessible.name: headline
    Accessible.description: supporting

    ColumnLayout {
        id: body
        anchors.left: parent.left; anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spaceExtraLarge; anchors.rightMargin: Theme.spaceExtraLarge
        spacing: Theme.space
        Icon {
            visible: state.symbol.length > 0
            name: state.symbol
            ink: Theme.secondary
            Accessible.ignored: true
        }
        CohortText {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceSmall
            text: state.headline
            font.pixelSize: Theme.titleMedium
            typeRole: "titleMedium"
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }
        CohortText {
            Layout.fillWidth: true
            visible: state.supporting.length > 0
            text: state.supporting
            font.pixelSize: Theme.bodyMedium
            typeRole: "bodyMedium"
            color: Theme.muted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Accessible.ignored: true
        }
        MButton {
            visible: state.actionText.length > 0
            Layout.topMargin: Theme.space
            text: state.actionText
            tonal: true
            onClicked: state.action()
        }
    }
}
