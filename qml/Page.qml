import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A destination: its headline and a column of sections under it.
//
// The window's margin follows its size class, and the column its reading
// width, below. The headline is display small at the emphasized
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

    // The column is held to a reading width that grows with the window's
    // size class: 720 while the window is medium and 840 once it is expanded
    // (Material's window size classes, 840dp and up). Past that, the column
    // is centred in the space beside the navigation, so a wide window frames
    // it rather than leaving it against one edge.
    readonly property real readingWidth: page.width >= 840 ? 840 : 720
    ColumnLayout {
        id: column
        width: Math.min(page.readingWidth, page.width - page.margin*2)
        x: Math.max(page.margin, (page.width - width)/2)
        y: page.margin
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
