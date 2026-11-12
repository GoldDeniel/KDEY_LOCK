import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.plasma5support as P5Support

PlasmoidItem {
    id: root

    property bool active: false

    toolTipMainText: "Keyboard Cleaning"
    toolTipSubText: active
        ? "Keyboard ignored - click to restore"
        : "Keyboard active - click to ignore keys"

    Plasmoid.title: "Keyboard Cleaning"
    Plasmoid.icon: active ? "changes-prevent-symbolic" : "input-keyboard-symbolic"

    function run(command) {
        commandSource.connectSource(command)
    }

    function toggle() {
        active = !active
        run("keyclean toggle")
        verify.restart()
    }

    P5Support.DataSource {
        id: commandSource
        engine: "executable"
        connectedSources: []
        onNewData: (sourceName, data) => {
            disconnectSource(sourceName)
            if (sourceName === "keyclean status")
                root.active = (data.stdout || "").trim() === "on"
        }
    }

    Timer {
        id: verify
        interval: 500
        onTriggered: root.run("keyclean status")
    }

    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.run("keyclean status")
    }

    compactRepresentation: PlasmaComponents3.ToolButton {
        icon.name: root.active ? "changes-prevent-symbolic" : "input-keyboard-symbolic"
        Accessible.name: root.active ? "Keyboard cleaning active" : "Keyboard cleaning inactive"
        onClicked: root.toggle()
    }

    Component.onCompleted: root.run("keyclean status")
}
