import QtQuick
import QtQuick.Layouts

Page {
    id: page
    objectName: "keyboardPage"
    title: qsTr("Keyboard")

    readonly property var lighting: machine.lighting
    readonly property string effect: lighting.effect || "static"
    // Static and breathing show the colours chosen for each zone; wave and
    // smooth cycle through colours of their own.
    readonly property bool coloured: effect === "static" || effect === "breath"
    readonly property bool animated: effect === "breath" || effect === "wave" || effect === "smooth"
    // Which zones a colour lands on. All of them until the person narrows it.
    property var selected: [true, true, true, true]
    // Colours a four-zone keyboard shows well: its LEDs are red, green and
    // blue at full strength, so the saturated hues and white read truest.
    readonly property var presets: ["#ffffff", "#ff0000", "#ff6a00", "#ffd000", "#00ff40",
                                    "#00e0ff", "#0040ff", "#8000ff", "#ff00a0"]

    function paint(colour) {
        const zones = (lighting.zones || []).slice()
        for (let i = 0; i < 4; ++i)
            if (selected[i]) zones[i] = colour
        machine.setLighting({zones: zones})
    }

    Section {
        visible: machine.lightingAvailable
        label: qsTr("Lighting")
        MSegmentedControl {
            objectName: "effects"
            Layout.fillWidth: true
            accessibleName: qsTr("Effect")
            options: [
                {key: "off", label: qsTr("Off"), name: "effect_off"},
                {key: "static", label: qsTr("Static"), name: "effect_static"},
                {key: "breath", label: qsTr("Breath"), name: "effect_breath"},
                {key: "wave", label: qsTr("Wave"), name: "effect_wave"},
                {key: "smooth", label: qsTr("Smooth"), name: "effect_smooth"}
            ]
            value: page.effect
            onChosen: key => machine.setLighting({effect: key})
        }
    }

    ColumnLayout {
        objectName: "colours"
        visible: machine.lightingAvailable && page.coloured
        Layout.fillWidth: true
        spacing: Theme.spaceLarge
        ZoneStrip {
            objectName: "zones"
            zones: page.lighting.zones || []
            selected: page.selected
            onToggled: index => {
                const next = page.selected.slice()
                next[index] = !next[index]
                // A colour has to land somewhere, so the last chosen zone
                // cannot be let go of.
                if (next.some(v => v)) page.selected = next
            }
        }
        Flow {
            objectName: "presets"
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: page.presets
                Swatch {
                    required property var modelData
                    required property int index
                    objectName: "preset" + index
                    colour: modelData
                    onPicked: page.paint(modelData)
                }
            }
        }
        HueSlider {
            objectName: "hue"
            Layout.fillWidth: true
            onPressedChanged: if (!pressed) page.paint(colour.toString())
            onMoved: if (pressed) page.paint(colour.toString())
        }
    }

    Section {
        visible: machine.lightingAvailable && page.effect !== "off"
        label: qsTr("Brightness")
        MSegmentedControl {
            objectName: "brightness"
            Layout.fillWidth: true
            accessibleName: qsTr("Brightness")
            options: [{key: 1, label: qsTr("Low"), name: "brightness_low"},
                      {key: 2, label: qsTr("High"), name: "brightness_high"}]
            value: page.lighting.brightness
            onChosen: key => machine.setLighting({brightness: key})
        }
    }

    Section {
        visible: machine.lightingAvailable && page.effect === "wave"
        label: qsTr("Direction")
        MSegmentedControl {
            objectName: "direction"
            Layout.fillWidth: true
            accessibleName: qsTr("Direction")
            options: [{key: "left", label: qsTr("Left"), name: "direction_left"},
                      {key: "right", label: qsTr("Right"), name: "direction_right"}]
            value: page.lighting.direction
            onChosen: key => machine.setLighting({direction: key})
        }
    }

    Section {
        visible: machine.lightingAvailable && page.animated
        label: qsTr("Speed")
        MSlider {
            objectName: "speed"
            Layout.fillWidth: true
            from: 1; to: 4; stepSize: 1
            snapMode: MSlider.SnapAlways
            value: page.lighting.speed || 1
            Accessible.name: qsTr("Speed")
            onMoved: machine.setLighting({speed: Math.round(value)})
        }
    }

    Section {
        readonly property var keys: ["fn-lock", "legion/winkey", "legion/touchpad"].filter(k => machine.switches[k] !== undefined)
        visible: keys.length > 0
        label: qsTr("Keys")
        SwitchRow {
            key: "fn-lock"
            headline: qsTr("Fn lock")
            supporting: qsTr("The top row acts as F1 to F12 without holding Fn")
        }
        SwitchRow {
            key: "legion/winkey"
            headline: qsTr("Windows key")
        }
        SwitchRow {
            key: "legion/touchpad"
            headline: qsTr("Touchpad")
        }
    }

    Section {
        readonly property var keys: ["led/platform::ylogo", "led/platform::ioport"].filter(k => machine.switches[k] !== undefined)
        visible: keys.length > 0
        label: qsTr("Lights")
        SwitchRow {
            key: "led/platform::ylogo"
            headline: qsTr("Logo")
        }
        SwitchRow {
            key: "led/platform::ioport"
            headline: qsTr("Ports")
        }
    }

    EmptyState {
        visible: !machine.lightingAvailable && machine.switches["fn-lock"] === undefined
        symbol: "keyboard"
        headline: qsTr("No keyboard controls")
        supporting: qsTr("This laptop's keyboard has no lighting or keys Cohort can change.")
    }
}
