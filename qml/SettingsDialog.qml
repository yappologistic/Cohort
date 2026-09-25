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
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space
            CohortText {
                text: qsTr("Size")
                font.pixelSize: Theme.titleSmall
                typeRole: "titleSmall"
                color: Theme.muted
            }
            MSegmentedControl {
                objectName: "sizeChoice"
                Layout.fillWidth: true
                accessibleName: qsTr("Size")
                options: [{key: 0.9, label: qsTr("Small"), name: "size_small"},
                          {key: 1.0, label: qsTr("Default"), name: "size_default"},
                          {key: 1.15, label: qsTr("Large"), name: "size_large"},
                          {key: 1.3, label: qsTr("Larger"), name: "size_larger"}]
                value: app.interfaceScale
                onChosen: key => app.interfaceScale = key
            }
            // Qt sets the size as the window is made, so a new one waits for
            // the window to be made again.
            RowLayout {
                visible: app.interfaceScale !== app.appliedScale
                Layout.fillWidth: true
                CohortText {
                    Layout.fillWidth: true
                    text: qsTr("Applies when Cohort reopens")
                    font.pixelSize: Theme.bodyMedium
                    typeRole: "bodyMedium"
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
                MButton {
                    objectName: "reopenButton"
                    text: qsTr("Reopen now")
                    tonal: true
                    size: "xsmall"
                    onClicked: app.reopen()
                }
            }
        }
        MSwitch {
            objectName: "backgroundSwitch"
            Layout.fillWidth: true
            text: qsTr("Run in background")
            hint: qsTr("Keeps your fan curve and lighting after restarts and sleep, and follows the charger for the automatic power mode.")
            checked: machine.background
            onToggled: {
                machine.background = checked
                checked = Qt.binding(() => machine.background)
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
    }
}
