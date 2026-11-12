import QtQuick 2.15
import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.components 3.0 as PlasmaComponents3
import org.kde.plasma.core 2.0 as PlasmaCore

Item {
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

    function sync() {
        run("keyclean status")
    }

    function toggle() {
        active = !active
        run("keyclean toggle")
        verify.restart()
    }

    PlasmaCore.DataSource {
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
        onTriggered: root.sync()
    }

    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.sync()
    }

    Plasmoid.compactRepresentation: PlasmaComponents3.ToolButton {
        icon.name: root.active ? "changes-prevent-symbolic" : "input-keyboard-symbolic"
        Accessible.name: root.active ? "Keyboard cleaning active" : "Keyboard cleaning inactive"
        onClicked: root.toggle()
    }

    Component.onCompleted: root.sync()
}
