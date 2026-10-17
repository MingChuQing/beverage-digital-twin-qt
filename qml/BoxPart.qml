import QtQuick 2.14
import QtQuick3D 1.14

Model {
    id: part
    orientation: Node.RightHanded
    property vector3d size: Qt.vector3d(1,1,1)
    property color tint: "#71879d"
    property real opacityFactor: 1
    property string deviceId: ""
    property bool enabled: true
    property bool selected: deviceId !== "" && twin.selectedId === deviceId
    visible: enabled
    source: "#Cube"
    scale: Qt.vector3d(size.x/100,size.y/100,size.z/100)
    pickable: deviceId !== ""
    materials: DefaultMaterial {
        diffuseColor: part.selected ? Qt.lighter(part.tint,1.25) : part.tint
        opacity: part.opacityFactor
        specularAmount: 0.35
        specularRoughness: 0.4
    }
}
