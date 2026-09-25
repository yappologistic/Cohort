import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A destination: its headline and a column of sections under it.
//
// The column is held to a reading width and kept at the leading edge, as
// Material's canonical layouts keep a single pane, and the window's margin
// follows its size class. The headline is display small at the emphasized
// weight, the same top-level title the other apps in this family use.
Flickable {
    id: page
    property string title: ""
    default property alias content: column.data
    readonly property real margin: Theme.margin(width)

    contentWidth: width
    contentHeight: column.implicitHeight + column.y + margin*2
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: MScrollBar {}
    Accessible.role: Accessible.Pane
    Accessible.name: title

    ColumnLayout {
        id: column
        x: page.margin
        y: page.margin
        width: Math.min(720, page.width - page.margin*2)
        spacing: Theme.spaceExtraLarge
        CohortText {
            heading: true
            text: page.title
            font.pixelSize: Theme.displaySmall
            emphasized: true
            Layout.fillWidth: true
            Layout.topMargin: Theme.space
        }
    }
}
