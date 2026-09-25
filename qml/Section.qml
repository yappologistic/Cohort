import QtQuick
import QtQuick.Layouts

// A run of related controls under an optional label.
//
// The label is a list subheader: title small, in the primary role, set 16dp in
// so it lines up with the headlines of the rows beneath it (ListTokens
// ItemLeadingSpace). A section with one obvious subject needs no label, and
// takes none. Rows inside a section sit the segmented list's 2dp apart, and
// the section decides which of the rows showing are the ends of the run, so
// a row the machine does not have never leaves a square corner behind.
ColumnLayout {
    id: section
    property string label: ""
    // An action on the whole section, set at the end of its label as a text
    // button, the way a list subheader carries one.
    property string actionText: ""
    signal action()
    default property alias content: rows.data
    Layout.fillWidth: true
    spacing: Theme.space
    RowLayout {
        visible: section.label.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: Theme.listLeadingSpace
        CohortText {
            heading: true
            text: section.label
            font.pixelSize: Theme.titleSmall
            typeRole: "titleSmall"
            color: Theme.primary
            Layout.fillWidth: true
        }
        MButton {
            objectName: "sectionAction"
            visible: section.actionText.length > 0
            size: "xsmall"
            text: section.actionText
            onClicked: section.action()
        }
    }
    ColumnLayout {
        id: rows
        Layout.fillWidth: true
        spacing: Theme.listSegmentedGap
        onVisibleChildrenChanged: section.arrange()
    }
    function arrange() {
        const shown = rows.visibleChildren.filter(child => child.first !== undefined && child.last !== undefined)
        for (let i = 0; i < shown.length; ++i) {
            shown[i].first = i === 0
            shown[i].last = i === shown.length - 1
        }
    }
}
