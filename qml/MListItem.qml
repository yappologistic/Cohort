import QtQuick
import QtQuick.Layouts

// Material 3 list item, in a segmented list.
//
// A settings page here is a few runs of rows, and Material's expressive list
// draws a run as one group set apart by a 2dp gap rather than divided by rules:
// the first row takes the large corner at the top, the last one at the bottom,
// and the rows between rest at the extra small corner (ListTokens
// ContainerShape and ItemContainerShape, ListItemDefaults.segmentedShapes).
// The group sits on the surface container so it reads against the page; the
// low container is too close to the surface in some desktop palettes to set
// a row apart from the page it is on.
//
// The row follows ListTokens: 56dp for one line and 72dp for two, 16dp clear
// at each end, 12dp between its parts, 10dp above and below, the headline in
// body large on the surface ink, the supporting text in body medium and the
// trailing supporting text in label small, both on the variant ink.
//
// A trailing control, usually a switch, is the row's default content. A row
// with one action takes it from anywhere on the row, as Material asks. What a
// row carries under its text, a slider for instance, goes in `below`.
Rectangle {
    id: row
    property string headline: ""
    property string supporting: ""
    property string trailingText: ""
    property string symbol: ""
    // Where in its run this row sits, which decides its corners.
    property bool first: true
    property bool last: true
    default property alias trailing: trailingSlot.data
    property alias below: belowSlot.data
    property bool interactive: true
    signal clicked()

    Layout.fillWidth: true
    // The height is ListTokens' for the lines of text, or the text and what
    // sits under it with 10dp above and below. A trailing control's 48dp
    // target is allowed to reach into that padding, as Compose's ListItem
    // lets it, rather than pushing a one-line row past 56dp.
    implicitHeight: Math.max(supporting.length ? Theme.rowHeightTwoLine : Theme.rowHeight,
                             texts.implicitHeight + (belowSlot.visible ? belowSlot.implicitHeight : 0) + 20)
    color: Theme.container
    topLeftRadius: first ? Theme.listOuter : Theme.listRest
    topRightRadius: topLeftRadius
    bottomLeftRadius: last ? Theme.listOuter : Theme.listRest
    bottomRightRadius: bottomLeftRadius
    Accessible.role: Accessible.ListItem
    Accessible.name: headline
    Accessible.description: supporting

    Rectangle {
        anchors.fill: parent
        topLeftRadius: parent.topLeftRadius; topRightRadius: parent.topRightRadius
        bottomLeftRadius: parent.bottomLeftRadius; bottomRightRadius: parent.bottomRightRadius
        color: Theme.text
        opacity: !row.interactive || !row.enabled ? 0 : area.pressed ? Theme.pressedOpacity : area.containsMouse ? Theme.hoverOpacity : 0
        Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        enabled: row.interactive && row.enabled
        hoverEnabled: true
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: row.clicked()
    }
    ColumnLayout {
        id: body
        anchors.left: parent.left; anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.listLeadingSpace
        anchors.rightMargin: Theme.listTrailingSpace
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.listBetweenSpace
            Icon {
                visible: row.symbol.length > 0
                name: row.symbol
                // ListTokens.ItemLeadingIconColor is the variant ink, 24dp.
                ink: Theme.muted
                opacity: row.enabled ? 1 : Theme.disabledContentOpacity
                Accessible.ignored: true
            }
            ColumnLayout {
                id: texts
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
                CohortText {
                    Layout.fillWidth: true
                    text: row.headline
                    font.pixelSize: Theme.bodyLarge
                    typeRole: "bodyLarge"
                    opacity: row.enabled ? 1 : Theme.disabledContentOpacity
                    Accessible.ignored: true
                }
                CohortText {
                    Layout.fillWidth: true
                    visible: row.supporting.length > 0
                    text: row.supporting
                    font.pixelSize: Theme.bodyMedium
                    typeRole: "bodyMedium"
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    opacity: row.enabled ? 1 : Theme.disabledContentOpacity
                    Accessible.ignored: true
                }
            }
            CohortText {
                objectName: "trailingText"
                visible: row.trailingText.length > 0
                text: row.trailingText
                font.pixelSize: Theme.labelSmall
                labelRole: true
                color: Theme.muted
                Accessible.ignored: true
            }
            // The trailing control centres itself in this slot, which takes
            // the text's height: its 48dp target overhangs into the row's
            // padding instead of pushing a one-line row past 56dp.
            Item {
                id: trailingSlot
                Layout.fillHeight: true
                implicitWidth: childrenRect.width
                visible: children.length > 0
            }
        }
        ColumnLayout {
            id: belowSlot
            Layout.fillWidth: true
            visible: children.length > 0
            spacing: 0
        }
    }
}
