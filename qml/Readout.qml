import QtQuick
import QtQuick.Layouts

// One figure the machine reports, on a filled card.
//
// Material's filled card is the highest surface container at the medium
// corner (FilledCardTokens). The figure is the card's subject, so it is set
// large, in headline small, and the name of what it measures sits under it in
// label medium on the variant ink. A figure the machine does not give is a
// dash, never a zero.
Rectangle {
    id: readout
    property string label: ""
    property string value: "–"
    property string symbol: ""
    Layout.fillWidth: true
    Layout.minimumWidth: 120
    implicitHeight: 88
    radius: Theme.shapeMedium
    color: Theme.highest
    Accessible.role: Accessible.StaticText
    Accessible.name: label + " " + value

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLarge
        spacing: Theme.spaceSmall
        RowLayout {
            spacing: Theme.space
            Icon {
                visible: readout.symbol.length > 0
                name: readout.symbol
                size: 20
                ink: Theme.muted
                Accessible.ignored: true
            }
            CohortText {
                Layout.fillWidth: true
                text: readout.label
                font.pixelSize: Theme.labelMedium
                labelRole: true
                color: Theme.muted
                Accessible.ignored: true
            }
        }
        CohortText {
            Layout.fillWidth: true
            text: readout.value
            font.pixelSize: Theme.headlineSmall
            Accessible.ignored: true
        }
    }
}
