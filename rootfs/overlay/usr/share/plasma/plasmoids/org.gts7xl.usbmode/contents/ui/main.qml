// USB-C tray applet: port status from gts7xl-usb-mode, gadget mode switch.
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasma5support as Plasma5Support
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: root

    readonly property string tool: "/usr/local/sbin/gts7xl-usb-mode"
    readonly property string statusCmd: tool + " status"
    property var st: ({})
    property var devices: []
    property string busy: ""
    property string error: ""

    readonly property bool attached: st.partner === "yes"
    readonly property bool host: attached && st.data_role === "host"
    readonly property bool device: attached && st.data_role === "device"
    readonly property bool sink: attached && st.power_role === "sink"

    readonly property string roleText: {
        if (st.driver !== "yes")
            return i18n("USB-C driver not loaded")
        if (!attached)
            return i18n("Nothing connected")
        if (host)
            return devices.length ? i18np("USB host: %1 device", "USB host: %1 devices", devices.length)
                                  : i18n("USB host")
        return i18n("Connected to a computer")
    }
    readonly property string powerText: {
        if (!attached)
            return ""
        if (!sink)
            return i18n("Powering the connected device (5 V)")
        if (st.opmode === "usb_power_delivery" && st.voltage_mv > 0)
            return i18n("Charging at %1 V, up to %2 A", (st.voltage_mv / 1000).toFixed(0),
                        (st.current_ma / 1000).toFixed(2))
        return i18n("Charging (%1)", st.opmode || "USB")
    }

    Plasmoid.icon: host ? "drive-removable-media-usb"
                 : device ? "computer"
                 : sink ? "battery-charging"
                 : "drive-removable-media-usb"
    Plasmoid.status: attached ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.PassiveStatus
    toolTipMainText: i18n("USB-C")
    toolTipSubText: powerText ? roleText + "\n" + powerText : roleText

    Plasma5Support.DataSource {
        id: exec
        engine: "executable"
        connectedSources: []
        onNewData: (source, data) => {
            disconnectSource(source)
            if (source === root.statusCmd) {
                root.parse(data["stdout"])
            } else {
                root.busy = ""
                root.error = data["exit code"] ? (data["stderr"] || i18n("Failed")).trim() : ""
                exec.connectSource(root.statusCmd)
            }
        }
    }

    function parse(out) {
        const s = {}, d = []
        for (const line of out.split("\n")) {
            const i = line.indexOf("=")
            if (i < 0)
                continue
            const k = line.slice(0, i), v = line.slice(i + 1)
            if (k === "device")
                d.push(v)
            else
                s[k] = v
        }
        st = s
        devices = d
    }

    function setGadget(mode) {
        if (busy || mode === st.gadget)
            return
        busy = mode
        exec.connectSource("pkexec " + tool + " gadget " + mode)
    }

    Timer {
        interval: root.expanded ? 1000 : 3000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: exec.connectSource(root.statusCmd)
    }

    fullRepresentation: PlasmaExtras.Representation {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 16
        Layout.minimumHeight: Kirigami.Units.gridUnit * 14
        collapseMarginsHint: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: Plasmoid.icon
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    PlasmaExtras.Heading {
                        level: 4
                        text: root.roleText
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                    PlasmaComponents.Label {
                        visible: text !== ""
                        text: root.powerText
                        opacity: 0.7
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                }
            }

            Repeater {
                model: root.host ? root.devices : []
                PlasmaComponents.Label {
                    text: "• " + modelData
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.gridUnit * 2
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
            }

            PlasmaExtras.Heading {
                level: 5
                text: i18n("When connected to a computer")
            }

            Repeater {
                model: [
                    { mode: "debug", label: i18n("Network and serial console"), hint: i18n("Default; SSH and the serial shell") },
                    { mode: "network", label: i18n("Network only"), hint: "" },
                    { mode: "serial", label: i18n("Serial console only"), hint: "" },
                    { mode: "none", label: i18n("Charge only"), hint: i18n("No data; the computer sees nothing") }
                ]
                PlasmaComponents.RadioButton {
                    Layout.fillWidth: true
                    text: modelData.hint ? modelData.label + "  —  " + modelData.hint : modelData.label
                    checked: (root.st.gadget || "debug") === modelData.mode
                    enabled: root.busy === ""
                    onToggled: root.setGadget(modelData.mode)
                }
            }

            PlasmaComponents.Label {
                visible: root.error !== ""
                text: root.error
                color: Kirigami.Theme.negativeTextColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Item { Layout.fillHeight: true }
        }
    }
}
