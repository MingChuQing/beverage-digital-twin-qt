import QtQuick 2.14
import QtQuick3D 1.14

// Origin is the container's base. Dimensions come from visualization.json.
Node {
    id: container
    orientation: Node.RightHanded
    property var shape: ({height: .5, radius: .14, kind: "bottle"})
    property real fillFraction: 0
    property color juiceColor: "#e5ab50"
    property string deviceId: ""
    property bool capped: false
    readonly property real h: Number(shape.height)
    readonly property real r: Number(shape.radius)
    readonly property bool isCan: shape.kind === "can"
    readonly property bool isJug: shape.kind === "jug"
    readonly property real bodyHeight: h * (isCan ? .94 : .76)
    readonly property real fillHeight: Math.max(.008, bodyHeight * .88 * Math.min(1, Math.max(0, fillFraction)))

    CylinderPart {
        visible: !container.isJug
        position: Qt.vector3d(0, container.bodyHeight / 2, 0)
        radius: container.r; length: container.bodyHeight
        tint: container.isCan ? "#adc5d2" : "#bedfe6"
        opacityFactor: container.isCan ? .48 : .32
        deviceId: container.deviceId
    }
    BoxPart {
        enabled: container.isJug
        position: Qt.vector3d(0, container.bodyHeight / 2, 0)
        size: Qt.vector3d(container.r * 1.8, container.bodyHeight, container.r * 1.55)
        tint: "#badbe2"; opacityFactor: .35; deviceId: container.deviceId
    }
    CylinderPart {
        visible: !container.isJug && container.fillFraction > .001
        position: Qt.vector3d(0, .015 + container.fillHeight / 2, 0)
        radius: container.r * .84; length: container.fillHeight
        tint: container.juiceColor; deviceId: container.deviceId
    }
    BoxPart {
        enabled: container.isJug && container.fillFraction > .001
        position: Qt.vector3d(0, .015 + container.fillHeight / 2, 0)
        size: Qt.vector3d(container.r * 1.52, container.fillHeight, container.r * 1.28)
        tint: container.juiceColor; deviceId: container.deviceId
    }
    CylinderPart {
        visible: !container.isCan
        position: Qt.vector3d(0, container.bodyHeight + container.h * .085, 0)
        radius: container.r * .55; length: container.h * .17
        tint: "#b1d2dc"; opacityFactor: .65; deviceId: container.deviceId
    }
    CylinderPart {
        visible: container.capped || container.isCan
        position: Qt.vector3d(0, container.h * .965, 0)
        radius: container.r * (container.isCan ? 1.02 : .59)
        length: container.h * .065
        tint: container.isCan ? "#e1e8eb" : container.juiceColor
        deviceId: container.deviceId
    }
    CylinderPart {
        position: Qt.vector3d(0, .018, 0)
        radius: container.r * .97; length: .028
        tint: "#b9d2db"; deviceId: container.deviceId
    }
    // The handle differentiates the 2 L family container from the small bottle.
    BoxPart {
        enabled: container.isJug
        position: Qt.vector3d(container.r * 1.13, container.h * .65, 0)
        size: Qt.vector3d(container.r * .25, container.h * .29, .055)
        tint: "#c3dae1"; deviceId: container.deviceId
    }
    BoxPart {
        enabled: container.isJug
        position: Qt.vector3d(container.r * .93, container.h * .80, 0)
        size: Qt.vector3d(container.r * .65, .06, .055)
        tint: "#c3dae1"; deviceId: container.deviceId
    }
    BoxPart {
        enabled: container.isJug
        position: Qt.vector3d(container.r * .93, container.h * .51, 0)
        size: Qt.vector3d(container.r * .65, .06, .055)
        tint: "#c3dae1"; deviceId: container.deviceId
    }
}
