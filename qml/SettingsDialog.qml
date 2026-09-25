import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// How Cohort itself looks and moves. The laptop's settings live on the pages;
// this is only the window's.
MDialog {
    id: dialog
    objectName: "settingsDialog"
    title: qsTr("Settings")
    width: fitWidth(420)
    modal: true
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: Theme.spaceLarge
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space
            CohortText {
                text: qsTr("Theme")
                font.pixelSize: Theme.titleSmall
                typeRole: "titleSmall"
                color: Theme.muted
            }
            MSegmentedControl {
                objectName: "themeChoice"
                Layout.fillWidth: true
                accessibleName: qsTr("Theme")
                options: [{key: "system", label: qsTr("System"), name: "theme_system"},
                          {key: "light", label: qsTr("Light"), name: "theme_light"},
                          {key: "dark", label: qsTr("Dark"), name: "theme_dark"}]
                value: app.theme
                onChosen: key => app.theme = key
            }
        }
        MSwitch {
            objectName: "motionSwitch"
            Layout.fillWidth: true
            text: qsTr("Animations")
            checked: app.motion
            onToggled: app.motion = checked
        }
        MSwitch {
            objectName: "expressiveSwitch"
            Layout.fillWidth: true
            enabled: app.motion
            text: qsTr("Expressive motion")
            checked: app.motionScheme === "expressive"
            onToggled: app.motionScheme = checked ? "expressive" : "standard"
        }
        CohortText {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space
            text: qsTr("Cohort %1 · %2").arg(Qt.application.version).arg(machine.model)
                  + "\n" + qsTr("Free software under the GNU GPL, version 3 or later. Not affiliated with Lenovo.")
            font.pixelSize: Theme.bodySmall
            color: Theme.muted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }
}
