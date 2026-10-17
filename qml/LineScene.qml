import QtQuick 2.14
import QtQuick3D 1.14

Node {
    id: world
    orientation: Node.RightHanded
    property real aspect: 1.5
    property real yaw: 27
    property real elevation: 34
    property real distance: 20
    property alias activeCamera: camera
    readonly property real fillLane: Number(twin.pointCards[1].sz)
    readonly property real workingTop: 2.3 - Number(twin.pointCards[1].lift)
    readonly property vector3d rinsePoint: Qt.vector3d(2*twin.routeSegments[0].sx-twin.pointCards[0].sx,0,2*twin.routeSegments[0].sz-twin.pointCards[0].sz)
    readonly property vector3d closurePoint: Qt.vector3d(2*twin.routeSegments[2].sx-twin.pointCards[1].sx,0,2*twin.routeSegments[2].sz-twin.pointCards[1].sz)
    function resetCamera() { yaw = 27; elevation = 34; distance = 20 }
    function tint(id, base) {
        var revision = twin.revision
        var state = twin.device(id)
        return state.statusKind === "fault" ? "#ef6b7c" : state.statusKind === "running" ? "#46cfac" : base
    }
    function selectedPosition() {
        var id = twin.selectedId
        if (id === "X" || id === "Z" || id === "Gripper")
            return Qt.vector3d(twin.axisX, .03, twin.axisY)
        if (id === "Infeed" || id === "ContainerSensor")
            return Qt.vector3d(twin.pointCards[0].sx, .03, twin.pointCards[0].sz)
        if (id === "Outfeed")
            return Qt.vector3d(twin.pointCards[2].sx, .03, twin.pointCards[2].sz)
        return Qt.vector3d(Number(twin.selectedDevice.x || 0), .03, Number(twin.selectedDevice.z || 0))
    }
    Node {
        orientation: Node.RightHanded
        position: Qt.vector3d(0, 1.2, -.15)
        rotation: Qt.vector3d(-world.elevation, world.yaw, 0)
        PerspectiveCamera {
            id: camera
            orientation: Node.RightHanded
            z: world.distance * Math.max(1, 1.35 / Math.max(.6, world.aspect))
            clipNear: .05; clipFar: 200; fieldOfView: 40
        }
    }
    DirectionalLight {
        orientation: Node.RightHanded; rotation: Qt.vector3d(-40,-30,0)
        color: "#dceeff"; ambientColor: "#667a90"; brightness: 80
    }
    PointLight {
        orientation: Node.RightHanded; position: Qt.vector3d(-5,7,5)
        color: "#a7d4f5"; brightness: 50; constantFade: 1; linearFade: 0; quadraticFade: 0
    }
    BoxPart { position: Qt.vector3d(0,-.15,0); size: Qt.vector3d(17.2,.22,7.4); tint: "#1d3347" }
    Repeater3D {
        model: 19
        delegate: BoxPart { position: Qt.vector3d(-8.1+index*.9,-.027,0); size: Qt.vector3d(.012,.012,7); tint: "#2b455b" }
    }
    Repeater3D {
        model: 9
        delegate: BoxPart { position: Qt.vector3d(0,-.026,-3.6+index*.9); size: Qt.vector3d(16.6,.012,.012); tint: "#2b455b" }
    }
    // Common work deck. The colored route follows the configured PR points.
    BoxPart { position: Qt.vector3d(0,.85,0); size: Qt.vector3d(14.4,.18,2.5); tint: "#344c60" }
    BoxPart { position: Qt.vector3d(0,.96,-1.27); size: Qt.vector3d(14.4,.08,.09); tint: "#8ea8bc" }
    BoxPart { position: Qt.vector3d(0,.96,1.27); size: Qt.vector3d(14.4,.08,.09); tint: "#8ea8bc" }
    Repeater3D {
        model: 16
        delegate: BoxPart { position: Qt.vector3d(-6.9+Math.floor(index/2)*1.95,.38,index%2===0?-1:1); size: Qt.vector3d(.11,.76,.11); tint: "#58758a" }
    }
    Repeater3D {
        model: twin.routeSegments
        delegate: BoxPart {
            position: Qt.vector3d(modelData.sx,.958,modelData.sz)
            rotation: Qt.vector3d(0,modelData.angle,0)
            size: Qt.vector3d(modelData.length,.018,.045)
            tint: twin.flavorColor
        }
    }

    // Each format adds two distinct locations, not another filling head.
    Repeater3D {
        model: twin.formatStations
        delegate: Node {
            id: formatStation
            orientation: Node.RightHanded
            property var specification: modelData
            property bool chosen: specification.formatIndex === twin.formatIndex
            BoxPart {
                position: Qt.vector3d(formatStation.specification.empty.sx,.98,formatStation.specification.empty.sz)
                size: Qt.vector3d(.68,.07,.68)
                tint: formatStation.chosen ? "#54cfba" : "#547587"; deviceId: "Infeed"
            }
            ContainerModel {
                position: Qt.vector3d(formatStation.specification.empty.sx,1.025,formatStation.specification.empty.sz)
                shape: formatStation.specification; juiceColor: twin.flavorColor; deviceId: "Infeed"
                visible: !(formatStation.chosen && twin.hasContainer)
            }
            BoxPart {
                position: Qt.vector3d(formatStation.specification.finished.sx,.98,formatStation.specification.finished.sz)
                size: Qt.vector3d(.70,.07,.70)
                tint: formatStation.chosen ? "#54cfba" : "#547587"; deviceId: "Outfeed"
            }
            ContainerModel {
                position: Qt.vector3d(formatStation.specification.finished.sx,1.025,formatStation.specification.finished.sz)
                shape: formatStation.specification; juiceColor: twin.flavorColor; fillFraction: 1
                capped: true; deviceId: "Outfeed"
            }
            BoxPart {
                position: Qt.vector3d(formatStation.specification.empty.sx,.985,formatStation.specification.empty.sz+.37)
                size: Qt.vector3d(.65,.035,.035); tint: formatStation.chosen ? "#b8fff0" : "#91aab8"
            }
            BoxPart {
                position: Qt.vector3d(formatStation.specification.finished.sx,.985,formatStation.specification.finished.sz+.37)
                size: Qt.vector3d(.65,.035,.035); tint: formatStation.chosen ? "#b8fff0" : "#91aab8"
            }
        }
    }
    BoxPart { position: Qt.vector3d(-6.9,.64,.62); size: Qt.vector3d(.48,.40,.43); tint: world.tint("Infeed","#5d829e"); deviceId: "Infeed" }
    BoxPart { position: Qt.vector3d(6.8,.64,.62); size: Qt.vector3d(.48,.40,.43); tint: world.tint("Outfeed","#5d829e"); deviceId: "Outfeed" }
    BoxPart { position: Qt.vector3d(-5.85,1.18,1.05); size: Qt.vector3d(.22,.34,.22); tint: world.tint("ContainerSensor","#65a0b7"); deviceId: "ContainerSensor" }
    BoxPart { position: Qt.vector3d(-4.85,1.16,1.05); size: Qt.vector3d(.24,.30,.22); tint: world.tint("BottleSensor","#65a0b7"); deviceId: "BottleSensor" }

    // The same three tanks and heads remain present for every recipe set.
    Repeater3D {
        model: twin.flavorStations
        delegate: Node {
            id: flavorStation
            orientation: Node.RightHanded
            property var flavor: modelData
            property bool active: flavor.device === twin.activeFillDevice
            property color metal: world.tint(flavor.device, "#90a7b9")
            position: Qt.vector3d(flavor.sx,0,0)
            CylinderPart { position: Qt.vector3d(0,1.64,-2.2); radius: .63; length: 2.02; tint: flavorStation.metal; deviceId: flavorStation.flavor.device }
            CylinderPart { position: Qt.vector3d(0,2.70,-2.2); radius: .66; length: .14; tint: flavorStation.flavor.color; deviceId: flavorStation.flavor.device }
            CylinderPart { position: Qt.vector3d(0,.52,-2.2); radius: .17; length: .28; tint: "#9ab1c0"; deviceId: flavorStation.flavor.device }
            BoxPart { position: Qt.vector3d(-.45,.32,-2.2); size: Qt.vector3d(.1,.64,.12); tint: "#7894a9" }
            BoxPart { position: Qt.vector3d(.45,.32,-2.2); size: Qt.vector3d(.1,.64,.12); tint: "#7894a9" }
            BoxPart { position: Qt.vector3d(0,1.75,-1.555); size: Qt.vector3d(.62,.52,.04); tint: flavorStation.flavor.color; deviceId: flavorStation.flavor.device }
            BoxPart { position: Qt.vector3d(0,2.4,-1.18); size: Qt.vector3d(.12,.12,2.0); tint: "#a9becc"; deviceId: flavorStation.flavor.device }
            BoxPart { position: Qt.vector3d(0,2.32,0); size: Qt.vector3d(.48,.42,.76); tint: world.tint(flavorStation.flavor.device,flavorStation.flavor.color); deviceId: flavorStation.flavor.device }
            // A short guide supports each package's Y position on the shared head.
            BoxPart { position: Qt.vector3d(0,2.09,0); size: Qt.vector3d(.13,.10,.86); tint: "#a9bfcd"; deviceId: flavorStation.flavor.device }
            CylinderPart { position: Qt.vector3d(0,1.98,world.fillLane); radius: .075; length: .18; tint: "#d5e5ed"; deviceId: flavorStation.flavor.device }
            BoxPart {
                enabled: flavorStation.active && twin.fillingOn
                position: Qt.vector3d(0,(1.88+world.workingTop)/2,world.fillLane)
                size: Qt.vector3d(.035,Math.max(.04,1.88-world.workingTop),.035)
                tint: flavorStation.flavor.color; deviceId: flavorStation.flavor.device
            }
            BoxPart {
                position: Qt.vector3d(0,.975,world.fillLane)
                size: Qt.vector3d(.8,.025,.62)
                tint: flavorStation.active ? flavorStation.flavor.color : "#506d82"
                deviceId: flavorStation.flavor.device
            }
        }
    }

    // X/Y/Z gantry and the container carried by its gripper.
    Repeater3D {
        model: 4
        delegate: BoxPart { position: Qt.vector3d(index<2?-7.05:7.05,1.68,index%2===0?-1.40:1.50); size: Qt.vector3d(.14,3.36,.14); tint: "#62849c" }
    }
    BoxPart { position: Qt.vector3d(0,3.36,-1.4); size: Qt.vector3d(14.3,.16,.18); tint: "#9cb2c3" }
    BoxPart { position: Qt.vector3d(0,3.36,1.5); size: Qt.vector3d(14.3,.16,.18); tint: "#9cb2c3" }
    BoxPart { position: Qt.vector3d(-7.04,3.35,-1.4); size: Qt.vector3d(.48,.35,.38); tint: world.tint("Y1","#507991"); deviceId: "Y1" }
    BoxPart { position: Qt.vector3d(-7.04,3.35,1.5); size: Qt.vector3d(.48,.35,.38); tint: world.tint("Y2","#507991"); deviceId: "Y2" }
    BoxPart { position: Qt.vector3d(twin.axisX,3.18,.05); size: Qt.vector3d(.23,.22,3.0); tint: "#a5bdcc"; deviceId: "X" }
    Node {
        orientation: Node.RightHanded
        position: Qt.vector3d(twin.axisX,0,twin.axisY)
        BoxPart { position: Qt.vector3d(0,3.02,0); size: Qt.vector3d(.43,.34,.4); tint: world.tint("X","#527d9a"); deviceId: "X" }
        BoxPart { position: Qt.vector3d(0,2.68-twin.axisZ/2,0); size: Qt.vector3d(.16,.60+twin.axisZ,.16); tint: world.tint("Z","#668ba5"); deviceId: "Z" }
        BoxPart { position: Qt.vector3d(0,2.33-twin.axisZ,0); size: Qt.vector3d(.50,.10,.26); tint: world.tint("Gripper","#bfd1dc"); deviceId: "Gripper" }
        BoxPart { position: Qt.vector3d(-.20,2.23-twin.axisZ,0); size: Qt.vector3d(.055,.20,.11); tint: world.tint("Gripper","#bfd1dc"); deviceId: "Gripper" }
        BoxPart { position: Qt.vector3d(.20,2.23-twin.axisZ,0); size: Qt.vector3d(.055,.20,.11); tint: world.tint("Gripper","#bfd1dc"); deviceId: "Gripper" }
        ContainerModel {
            visible: twin.hasContainer
            position: Qt.vector3d(0,2.3-twin.axisZ-Number(twin.containerShape.height),0)
            shape: twin.containerShape; juiceColor: twin.flavorColor
            fillFraction: twin.fillFraction; capped: twin.currentStep >= 8; deviceId: "Gripper"
        }
    }

    // Shared rinse and closure machinery use the selected package's lane.
    BoxPart { position: Qt.vector3d(-3.8,1.54,-1.0); size: Qt.vector3d(.55,1.2,.4); tint: "#66879f"; deviceId: "RinseValve" }
    BoxPart { position: Qt.vector3d(-3.8,2.17,0); size: Qt.vector3d(.55,.17,2.05); tint: world.tint("RinseValve","#91aebe"); deviceId: "RinseValve" }
    CylinderPart { position: Qt.vector3d(world.rinsePoint.x,1.94,world.rinsePoint.z); radius: .075; length: .35; tint: "#b7d2df"; deviceId: "RinseValve" }
    BoxPart { enabled: twin.rinseOn; position: Qt.vector3d(world.rinsePoint.x,1.50,world.rinsePoint.z); size: Qt.vector3d(.04,.5,.04); tint: "#66dbec"; deviceId: "RinseValve" }
    BoxPart { position: Qt.vector3d(3.52,1.42,-1.02); size: Qt.vector3d(.62,2.1,.46); tint: world.tint("Closure","#648aa3"); deviceId: "Closure" }
    BoxPart { position: Qt.vector3d(3.52,2.5,0); size: Qt.vector3d(1,.18,2.2); tint: world.tint("Closure","#a8bfce"); deviceId: "Closure" }
    CylinderPart { position: Qt.vector3d(world.closurePoint.x,2.03,world.closurePoint.z); radius: .20; length: .75; tint: world.tint("Torque","#8caabc"); angle: twin.capAngle; deviceId: "Torque" }
    BoxPart { position: Qt.vector3d(world.closurePoint.x,1.64,world.closurePoint.z); size: Qt.vector3d(.44,.12,.11); rotation: Qt.vector3d(0,twin.capAngle,0); tint: "#c4d4de"; deviceId: "Closure" }
    CylinderPart { position: Qt.vector3d(3.55,2.0,-2.0); radius: .40; length: .42; tint: world.tint("CapFeeder","#839fb4"); deviceId: "CapFeeder" }
    BoxPart { position: Qt.vector3d(3.55,1.65,-1.58); size: Qt.vector3d(.25,.16,1.1); tint: "#a9c0ce"; deviceId: "CapFeeder" }
    BoxPart { position: Qt.vector3d(4.4,1.42,-1.05); size: Qt.vector3d(.44,1.9,.4); tint: "#6888a0"; deviceId: "Relay" }
    BoxPart { position: Qt.vector3d(4.4,1.58,-.35); size: Qt.vector3d(.46,.32,1.0); tint: world.tint("Relay","#8aaabd"); deviceId: "Relay" }
    BoxPart { position: Qt.vector3d(5.7,.72,-2.12); size: Qt.vector3d(1.2,1.44,.6); tint: world.tint("Relay","#829caf"); deviceId: "Relay" }
    BoxPart { position: Qt.vector3d(5.7,.88,-1.8); size: Qt.vector3d(.75,.5,.035); tint: "#255d72"; deviceId: "Relay" }
    BoxPart { position: Qt.vector3d(-2.9,2.16,-1.85); size: Qt.vector3d(.34,.42,.34); tint: world.tint("FlowMeter","#518da5"); deviceId: "FlowMeter" }
    CylinderPart { position: Qt.vector3d(-.88,2.32,-1.92); radius: .19; length: .25; tint: world.tint("PressureSensor","#8caabc"); deviceId: "PressureSensor" }
    BoxPart { position: Qt.vector3d(Number(twin.pointCards[1].sx)-.45,1.22,.92); size: Qt.vector3d(.22,.38,.23); tint: world.tint("LevelSensor","#6695ae"); deviceId: "LevelSensor" }
    BoxPart { position: Qt.vector3d(0,.28,-3.03); size: Qt.vector3d(5.2,.12,.12); tint: twin.cipOn?"#51d9ba":"#7398ae"; deviceId: "CIPValve" }
    BoxPart { position: Qt.vector3d(-1.7,.45,-3.03); size: Qt.vector3d(.38,.33,.36); tint: world.tint("CIPValve","#638da5"); deviceId: "CIPValve" }

    Node {
        orientation: Node.RightHanded
        position: world.selectedPosition()
        BoxPart { position: Qt.vector3d(-.46,0,0); size: Qt.vector3d(.035,.02,.95); tint: "#81ebcd" }
        BoxPart { position: Qt.vector3d(.46,0,0); size: Qt.vector3d(.035,.02,.95); tint: "#81ebcd" }
        BoxPart { position: Qt.vector3d(0,0,-.46); size: Qt.vector3d(.95,.02,.035); tint: "#81ebcd" }
        BoxPart { position: Qt.vector3d(0,0,.46); size: Qt.vector3d(.95,.02,.035); tint: "#81ebcd" }
    }
}
