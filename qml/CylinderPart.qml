import QtQuick 2.14
import QtQuick3D 1.14

Model {
    id: part
    orientation: Node.RightHanded
    property real radius: 0.2
    property real length: 1
    property color tint: "#8fa7bc"
    property real opacityFactor: 1
    property string deviceId: ""
    property real angle: 0
    source: "#Cylinder"
    scale: Qt.vector3d(radius/50,length/100,radius/50)
    rotation: Qt.vector3d(0,angle,0)
    pickable: deviceId !== ""
    materials: DefaultMaterial {
        diffuseColor: part.deviceId !== "" && twin.selectedId === part.deviceId ? Qt.lighter(part.tint,1.25) : part.tint
        opacity: part.opacityFactor
        specularAmount: 0.5
        specularRoughness: 0.25
    }
}
