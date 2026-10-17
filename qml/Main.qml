import QtQuick 2.14
import QtQuick.Window 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import QtQuick3D 1.14

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    visible: true
    width: demoSmallWindow ? 1120 : Math.min(1480, Screen.desktopAvailableWidth-60)
    height: demoSmallWindow ? 840 : Math.min(1000, Screen.desktopAvailableHeight-60)
    minimumWidth: 1120; minimumHeight: 840
    title: "饮料灌装数字孪生 · 演示版 | Qt 5.14"
    color: "#0b1726"
    font.family: "Microsoft YaHei"
    font.pixelSize: 13
    palette.window: "#102033"
    palette.base: "#15283c"
    palette.button: "#15283c"
    palette.text: "#dbe9f5"
    palette.buttonText: "#dbe9f5"
    palette.windowText: "#dbe9f5"
    palette.highlight: "#27665c"
    palette.highlightedText: "#e6fff5"
    property color muted: "#879eb5"
    property color ink: "#dbe9f5"
    property color accent: "#6ee3c3"
    property color line: "#263d53"
    property var selected: twin.selectedDevice
    function statusColor(kind) { return kind === "fault" ? "#ff8796" : kind === "running" ? "#6ee3c3" : "#8ca6be" }
    function selectAt(x, y) {
        var hit=sceneView.pick(x,y)
        if(hit.objectHit&&hit.objectHit.deviceId){twin.selectDevice(hit.objectHit.deviceId);return hit.objectHit.deviceId}
        return ""
    }
    function smokePickDevice() {
        var p=sceneView.mapFrom3DScene(Qt.vector3d(-2,2.3,-2.2))
        return selectAt(p.x,p.y)
    }
    function smokeSelectRecipe(flavor, format) {
        flavorBox.currentIndex=flavor;flavorBox.activated(flavor)
        formatBox.currentIndex=format;formatBox.activated(format)
        return twin.flavorIndex===flavor && twin.formatIndex===format
    }
    Connections {
        target:twin
        onRecipeChanged:{flavorBox.currentIndex=twin.flavorIndex;formatBox.currentIndex=twin.formatIndex}
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 64
            color: "#101f30"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 22; spacing: 14
                Rectangle {
                    Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 9; color: "#143d3e"; border.color: "#285d59"
                    Text {anchors.centerIn:parent;text:"DT";color:accent;font.pixelSize:16;font.bold:true}
                }
                Column {
                    spacing: 3
                    Text {text:"饮料灌装 · 数字孪生";color:ink;font.pixelSize:20;font.bold:true}
                    Text {text:"BEVERAGE LINE   /   CONFIGURATION DRIVEN";color:muted;font.pixelSize:10;font.letterSpacing:1.2}
                }
                Item {Layout.fillWidth:true}
                Rectangle {
                    Layout.preferredWidth: 166; Layout.preferredHeight: 28; radius: 14; color: "#163833"; border.color: "#2a5c51"
                    Text {anchors.centerIn:parent;text:bus.serialMode?"●  Modbus RTU 接收":"●  离线模拟 · 演示版";color:accent;font.pixelSize:12}
                }
                ActionButton {text:bus.serialMode?"切换离线演示":"切换串口接收";onClicked:bus.serialMode=!bus.serialMode}
                Text {text:"Qt 5.14 / Quick 3D";color:muted;font.pixelSize:12}
            }
        }
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:88;color:"#132a3b"
            ColumnLayout {
                anchors.fill:parent;anchors.leftMargin:18;anchors.rightMargin:18;anchors.topMargin:8;anchors.bottomMargin:8;spacing:5
                RowLayout {
                    spacing:10
                    Rectangle {
                        Layout.preferredWidth:142;Layout.preferredHeight:34;radius:5;color:twin.profileIndex?"#294336":"#203b51";border.color:twin.profileIndex?"#587a52":"#405c74"
                        Text {anchors.centerIn:parent;text:twin.profileIndex?"扩展版 · 9 个配方":"原版 · 6 个配方";color:twin.profileIndex?"#b4df99":ink;font.bold:true;font.pixelSize:12}
                    }
                    Text {text:"口味";color:muted;font.pixelSize:12}
                    ComboBox {id:flavorBox;objectName:"flavorBox";enabled:!bus.serialMode;Layout.preferredWidth:120;Layout.preferredHeight:34;model:twin.flavorLabels;currentIndex:twin.flavorIndex;onActivated:twin.selectRecipe(currentIndex,twin.formatIndex)}
                    Text {text:"包装";color:muted;font.pixelSize:12}
                    ComboBox {id:formatBox;objectName:"formatBox";enabled:!bus.serialMode;Layout.preferredWidth:164;Layout.preferredHeight:34;model:twin.formatLabels;currentIndex:twin.formatIndex;onActivated:twin.selectRecipe(twin.flavorIndex,currentIndex)}
                    Item {Layout.fillWidth:true}
                    ActionButton {objectName:"profileButton";enabled:!bus.serialMode;text:twin.profileIndex?"切回原版两规格":"为三种口味增加 250 mL";primary:true;onClicked:twin.loadProfile(twin.profileIndex?0:1)}
                    ActionButton {objectName:"reloadButton";enabled:!bus.serialMode;text:"重载配置";onClicked:twin.reloadConfig();ToolTip.visible:hovered;ToolTip.text:twin.configLocation}
                }
                RowLayout {
                    spacing:12
                    Text {text:"3 个共用灌装头  ·  "+twin.formatLabels.length+" 种包装  ·  "+twin.deviceCount+" 个设备端点";color:accent;font.pixelSize:11}
                    Text {text:twin.recipe.id+"  /  "+twin.volumeMl+" mL  /  设定 "+(bus.serialMode?bus.targetTime:twin.recipe.fill_time_ms)+" ms";color:ink;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideRight}
                    Text {text:twin.loadMessage;color:muted;font.pixelSize:11}
                }
            }
        }
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:54;color:"#102033"
            RowLayout {
                visible:bus.serialMode
                anchors.fill:parent;anchors.leftMargin:18;anchors.rightMargin:18;spacing:8
                Text {text:"接收串口";color:muted;font.pixelSize:12}
                ComboBox {id:receiverPort;objectName:"receiverPort";editable:true;model:bus.availablePorts;currentIndex:bus.availablePorts.indexOf(bus.portName);editText:bus.portName;Component.onCompleted:editText=bus.portName;Layout.preferredWidth:115;Layout.preferredHeight:34;enabled:!bus.connected}
                ComboBox {id:receiverBaud;model:["9600","19200","38400","57600","115200"];currentIndex:model.indexOf(String(bus.baudRate));Layout.preferredWidth:104;Layout.preferredHeight:34;enabled:!bus.connected}
                ActionButton {text:"刷新";onClicked:bus.refreshPorts()}
                ActionButton {objectName:"connectSerialButton";text:bus.connected?"关闭串口":"打开串口";primary:!bus.connected;onClicked:bus.connected?bus.closePort():bus.openPort(receiverPort.editText,Number(receiverBaud.currentText))}
                Text {text:bus.status;color:bus.connected?accent:muted;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideRight}
                Text {text:"RX "+bus.rxCount+" / TX "+bus.txCount+" / CRC "+bus.crcErrors;color:muted;font.pixelSize:11}
                ActionButton {text:"打开发送端";onClicked:bus.launchSender()}
            }
            RowLayout {
                visible:!bus.serialMode
                anchors.fill:parent;anchors.leftMargin:18;anchors.rightMargin:18;spacing:8
                ActionButton {objectName:"startButton";text:twin.running?"运行中":"开始 / 继续";primary:true;enabled:!twin.running&&twin.fault==="";onClicked:twin.start()}
                ActionButton {objectName:"pauseButton";text:"暂停";enabled:twin.running;onClicked:twin.pause()}
                ActionButton {objectName:"stepButton";text:"推进一步";enabled:twin.fault==="";onClicked:twin.nextStep()}
                ActionButton {objectName:"resetButton";text:"重置";onClicked:twin.reset()}
                Rectangle {Layout.preferredWidth:1;Layout.preferredHeight:24;color:line;Layout.leftMargin:6;Layout.rightMargin:6}
                Text {text:"演示倍速";color:muted;font.pixelSize:12}
                ComboBox {
                    id:speedBox;objectName:"speedBox";Layout.preferredWidth:80;Layout.preferredHeight:34
                    model:["0.5×","1×","2×","4×"];currentIndex:1
                    onActivated:twin.speed=[.5,1,2,4][currentIndex]
                }
                CheckBox {text:"循环";checked:twin.loop;onToggled:twin.loop=checked;palette.windowText:ink;font.pixelSize:12}
                Item {Layout.fillWidth:true}
                ComboBox {
                    id:faultBox;objectName:"faultBox";Layout.preferredWidth:150;Layout.preferredHeight:34
                    model:["流量计离线","Y2 轴报警","瓶位检测超时"]
                }
                ActionButton {objectName:"injectButton";text:"注入故障";danger:true;enabled:twin.fault==="";onClicked:twin.injectFault(["offline","axis","bottle"][faultBox.currentIndex])}
            }
        }
        Rectangle {Layout.fillWidth:true;Layout.preferredHeight:1;color:line}
        Rectangle {
            visible:twin.fault!=="";Layout.fillWidth:true;Layout.preferredHeight:visible?42:0;color:"#3a2432"
            RowLayout {
                anchors.fill:parent;anchors.leftMargin:20;anchors.rightMargin:20
                Text {text:"●  "+twin.fault;color:"#ffacb5";Layout.fillWidth:true;font.pixelSize:12}
                ActionButton {objectName:"clearFaultButton";text:"清除故障";danger:true;onClicked:twin.clearFault()}
            }
        }
        RowLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:400;spacing:0
            // Stable C++ list model keeps selection and pointer interactions intact while telemetry changes.
            Rectangle {
                Layout.preferredWidth:190;Layout.fillHeight:true;color:"#0f1e2f"
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:12;spacing:9
                    RowLayout {
                        Text {text:"设备目录";color:ink;font.bold:true;Layout.fillWidth:true}
                        Text {text:twin.deviceCount;color:muted;font.pixelSize:12}
                    }
                    Text {text:"点击列表或三维部件查看";color:muted;font.pixelSize:11}
                    ListView {
                        id:deviceList;objectName:"deviceList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:3
                        model:twin.deviceModel
                        ScrollBar.vertical: ScrollBar {policy:ScrollBar.AsNeeded}
                        delegate: Rectangle {
                            width:deviceList.width;height:35;radius:5
                            color:twin.selectedId===deviceId?"#1b3a45":mouse.containsMouse?"#172b3e":"transparent"
                            border.color:twin.selectedId===deviceId?"#37665f":"transparent"
                            RowLayout {
                                anchors.fill:parent;anchors.leftMargin:8;anchors.rightMargin:8;spacing:8
                                Rectangle {Layout.preferredWidth:6;Layout.preferredHeight:6;radius:3;color:statusColor(statusKind)}
                                Column {
                                    Layout.fillWidth:true
                                    Text {text:displayName;color:twin.selectedId===deviceId?accent:ink;font.pixelSize:12}
                                    Text {text:deviceId;color:muted;font.pixelSize:10}
                                }
                                Text {text:statusKind==="fault"?"异常":statusKind==="running"?"运行":"待机";color:statusColor(statusKind);font.pixelSize:10}
                            }
                            MouseArea {id:mouse;anchors.fill:parent;hoverEnabled:true;cursorShape:Qt.PointingHandCursor;onClicked:twin.selectDevice(deviceId)}
                        }
                    }
                    Text {text:bus.serialMode?"单串口站号来自统一配置\n"+bus.portName+" / Modbus RTU":"离线演示模式\n未打开串口";color:muted;font.pixelSize:11;lineHeight:1.5}
                }
            }
            Rectangle {Layout.preferredWidth:1;Layout.fillHeight:true;color:line}
            ColumnLayout {
                Layout.fillWidth:true;Layout.fillHeight:true;spacing:0
                Rectangle {
                    Layout.fillWidth:true;Layout.preferredHeight:48;color:"#102033"
                    RowLayout {
                        anchors.fill:parent;anchors.leftMargin:16;anchors.rightMargin:12
                        Text {text:"产线总览";color:ink;font.bold:true}
                        Text {text:"/ "+twin.currentStepName;color:accent;font.pixelSize:12;Layout.fillWidth:true}
                        ActionButton {text:"复位视角";implicitHeight:28;onClicked:lineScene.resetCamera()}
                    }
                }
                Item {
                    id:sceneContainer;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:200
                    View3D {
                        id:sceneView;objectName:"sceneView";anchors.fill:parent;focus:true
                        camera: lineScene.activeCamera
                        environment:SceneEnvironment {backgroundMode:SceneEnvironment.Color;clearColor:"#101f30";multisampleAAMode:SceneEnvironment.X4}
                        LineScene {id:lineScene;aspect:sceneView.width/Math.max(1,sceneView.height)}
                    }
                    MouseArea {
                        anchors.fill:parent;acceptedButtons:Qt.LeftButton|Qt.RightButton
                        property real oldX:0;property real oldY:0
                        property bool dragging:false
                        onPressed:{oldX=mouse.x;oldY=mouse.y;dragging=false}
                        onPositionChanged:if(pressed&&pressedButtons===Qt.RightButton){dragging=true;lineScene.yaw-=(mouse.x-oldX)*.4;lineScene.elevation=Math.max(15,Math.min(75,lineScene.elevation+(mouse.y-oldY)*.35));oldX=mouse.x;oldY=mouse.y}
                        onClicked:if(mouse.button===Qt.LeftButton&&!dragging)window.selectAt(mouse.x,mouse.y)
                        onWheel:{lineScene.distance=Math.max(13,Math.min(34,lineScene.distance-wheel.angleDelta.y/160));wheel.accepted=true}
                    }
                    Repeater {
                        model:twin.flavorStations
                        delegate:Rectangle {
                            property vector3d projected:{var revision=twin.revision;var a=lineScene.yaw;var e=lineScene.elevation;var d=lineScene.distance;var w=sceneView.width;var h=sceneView.height;return sceneView.mapFrom3DScene(Qt.vector3d(modelData.sx,3.6,-2.2))}
                            x:projected.x-width/2+(index-1)*40;y:projected.y-height/2
                            width:stationText.implicitWidth+14;height:24;radius:4
                            color:twin.activeFillDevice===modelData.device?"#253f48":"#142c41"
                            border.color:modelData.color
                            visible:projected.x>20&&projected.x<sceneContainer.width-20&&projected.y>55&&projected.y<sceneContainer.height-25
                            Text {id:stationText;anchors.centerIn:parent;text:modelData.label+"灌装";color:modelData.color;font.pixelSize:11}
                            MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:twin.selectDevice(modelData.device)}
                        }
                    }
                    Repeater {
                        model:[{point:twin.pointCards[0],label:"上瓶",device:"Infeed"},{point:twin.pointCards[2],label:"成品",device:"Outfeed"}]
                        delegate:Rectangle {
                            property vector3d projected:{var revision=twin.revision;var a=lineScene.yaw;var e=lineScene.elevation;var d=lineScene.distance;var w=sceneView.width;var h=sceneView.height;return sceneView.mapFrom3DScene(Qt.vector3d(modelData.point.sx,1.0,modelData.point.sz))}
                            x:projected.x-width/2;y:projected.y+18
                            width:pointText.implicitWidth+14;height:22;radius:4;color:"#122e3c";border.color:"#4e8f88"
                            visible:projected.x>30&&projected.x<sceneContainer.width-30&&y>75&&y<sceneContainer.height-45
                            Text {id:pointText;anchors.centerIn:parent;text:modelData.label+" · "+modelData.point.index;color:accent;font.pixelSize:10}
                            MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:twin.selectDevice(modelData.device)}
                        }
                    }
                    Column {
                        anchors.left:parent.left;anchors.top:parent.top;anchors.margins:16;spacing:5
                        Text {text:twin.recipeCaption;color:"#c9dbea";font.pixelSize:13;font.bold:true}
                        Text {text:twin.modeText+"  /  "+twin.batchCaption;color:twin.fault!==""?"#ff8796":accent;font.pixelSize:11}
                    }
                    Rectangle {
                        anchors.right:parent.right;anchors.top:parent.top;anchors.margins:14
                        width:110;height:48;radius:6;color:"#152c3f";border.color:"#2b455c"
                        Column {anchors.centerIn:parent;spacing:1
                            Text {text:bus.serialMode?"主站成品计数":"模拟成品";color:muted;font.pixelSize:11;anchors.horizontalCenter:parent.horizontalCenter}
                            Text {text:bus.serialMode?twin.completed+" 件":twin.completed+" / "+(twin.recipe.batches*twin.recipe.units_per_batch)+" 件";color:ink;font.pixelSize:17;font.bold:true;anchors.horizontalCenter:parent.horizontalCenter}
                        }
                    }
                    Row {
                        anchors.left:parent.left;anchors.bottom:parent.bottom;anchors.margins:14;spacing:12
                        Text {text:"● 运行";color:accent;font.pixelSize:11}
                        Text {text:"● 待机";color:muted;font.pixelSize:11}
                        Text {text:"● 故障";color:"#ff8796";font.pixelSize:11}
                    }
                    Text {anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.margins:14;text:"左键选择 · 右键拖动 · 滚轮缩放";color:muted;font.pixelSize:11}
                }
                Rectangle {
                    Layout.fillWidth:true;Layout.preferredHeight:64;color:"#132a3b"
                    RowLayout {
                        anchors.fill:parent;anchors.leftMargin:10;anchors.rightMargin:10;spacing:8
                        Repeater {
                            model:twin.pointCards
                            delegate:Rectangle {
                                Layout.fillWidth:true;Layout.preferredHeight:48;radius:5;color:"#102235";border.color:index===1?twin.flavorColor:"#304b61"
                                Column {
                                    anchors.fill:parent;anchors.margins:7;spacing:4
                                    Text {text:modelData.label+" · 点 "+modelData.index;color:index===1?twin.flavorColor:ink;font.pixelSize:11;font.bold:true}
                                    Text {width:parent.width;text:modelData.prText;color:muted;font.pixelSize:10;elide:Text.ElideRight}
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth:true;Layout.preferredHeight:42;color:"#102337"
                    RowLayout {
                        anchors.fill:parent;anchors.leftMargin:16;anchors.rightMargin:16;spacing:18
                        Column {spacing:4
                            Text {text:"龙门示意坐标 / 展示单位";color:muted;font.pixelSize:10}
                            Text {text:"X "+(twin.axisX*100).toFixed(1)+"    Y "+(twin.axisY*100).toFixed(1)+"    Z "+(twin.axisZ*100).toFixed(1);color:ink;font.pixelSize:12;font.family:"Consolas"}
                        }
                        Item {Layout.fillWidth:true}
                        Column {spacing:4
                            Text {text:"容器液面 · 模拟体积";color:muted;font.pixelSize:10}
                            Text {text:(twin.fillFraction*100).toFixed(0)+"%   /   "+twin.volumeMl+" mL";color:accent;font.pixelSize:12}
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth:true;Layout.preferredHeight:64;color:"#0e1e30"
                    ColumnLayout {
                        anchors.fill:parent;anchors.margins:12;spacing:4
                        RowLayout {Text {text:"事件记录";color:ink;font.pixelSize:12} Item {Layout.fillWidth:true} Text {text:"最多保留 80 条";color:muted;font.pixelSize:10}}
                        ListView {
                            Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:bus.serialMode?bus.frames:twin.eventLog;spacing:3
                            delegate:Text {width:ListView.view.width;text:modelData;color:index===0?"#bfd9e9":muted;font.pixelSize:11;elide:Text.ElideRight}
                            ScrollBar.vertical:ScrollBar {}
                        }
                    }
                }
            }
            Rectangle {Layout.preferredWidth:1;Layout.fillHeight:true;color:line}
            Rectangle {
                Layout.preferredWidth:288;Layout.fillHeight:true;color:"#102033"
                ScrollView {
                    anchors.fill:parent;clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:parent.width;spacing:0
                        Item {Layout.preferredHeight:16}
                        Text {Layout.leftMargin:18;text:"设备详情";color:muted;font.pixelSize:12}
                        Text {Layout.leftMargin:18;Layout.topMargin:8;text:selected.displayName||"";color:ink;font.pixelSize:21;font.bold:true}
                        Text {Layout.leftMargin:18;Layout.topMargin:4;text:selected.deviceId||"";color:muted;font.family:"Consolas";font.pixelSize:12}
                        Text {Layout.leftMargin:18;Layout.topMargin:12;text:"● "+(selected.statusText||"");color:statusColor(selected.statusKind);font.pixelSize:12;font.bold:true}
                        Rectangle {Layout.fillWidth:true;Layout.leftMargin:18;Layout.rightMargin:18;Layout.topMargin:16;Layout.preferredHeight:1;color:line}
                        Text {Layout.leftMargin:18;Layout.topMargin:15;text:selected.metric||"";color:muted;font.pixelSize:12}
                        RowLayout {
                            Layout.leftMargin:18;Layout.topMargin:4;spacing:7
                            Text {objectName:"selectedValue";text:selected.valueText||"—";color:statusColor(selected.statusKind);font.pixelSize:selected.numeric?30:21;font.bold:true}
                            Text {text:selected.valueText==="—"?"":selected.unit||"";color:muted;font.pixelSize:12;Layout.alignment:Qt.AlignBottom;Layout.bottomMargin:5}
                        }
                        Text {Layout.leftMargin:18;Layout.topMargin:4;text:selected.sourceKind||"";color:accent;font.pixelSize:11}
                        Text {Layout.leftMargin:18;Layout.rightMargin:18;Layout.fillWidth:true;Layout.topMargin:7;visible:String(selected.quality||"").indexOf("Bad")===0;text:"最后有效值："+(selected.lastValid||"—");color:"#ffacb5";font.pixelSize:12;wrapMode:Text.Wrap}
                        GridLayout {
                            Layout.fillWidth:true;Layout.leftMargin:18;Layout.rightMargin:18;Layout.topMargin:16;columns:2;columnSpacing:12;rowSpacing:9
                            Text {text:"端口 / 站号";color:muted;font.pixelSize:12}
                            Text {text:(selected.port||"")+" / "+(selected.station||"")+(bus.serialMode?"":"（示例）");color:ink;font.pixelSize:12;Layout.fillWidth:true}
                            Text {text:"数据质量";color:muted;font.pixelSize:12}
                            Text {text:selected.quality||"";color:selected.statusKind==="fault"?"#ff8796":ink;font.pixelSize:11;Layout.fillWidth:true;wrapMode:Text.Wrap}
                            Text {text:"采样年龄";color:muted;font.pixelSize:12}
                            Text {text:((selected.ageMs||0)/1000).toFixed(2)+" s";color:ink;font.pixelSize:12}
                        }
                        Text {Layout.leftMargin:18;Layout.rightMargin:18;Layout.fillWidth:true;Layout.topMargin:12;visible:!!selected.target;text:selected.target||"";color:accent;font.pixelSize:12;wrapMode:Text.Wrap}
                        Rectangle {Layout.fillWidth:true;Layout.leftMargin:18;Layout.rightMargin:18;Layout.topMargin:16;Layout.preferredHeight:1;color:line}
                        Text {Layout.leftMargin:18;Layout.topMargin:14;text:"关联协议点位";color:muted;font.pixelSize:12}
                        Text {Layout.leftMargin:18;Layout.rightMargin:18;Layout.fillWidth:true;Layout.topMargin:5;text:selected.point||"";color:ink;font.family:"Consolas";font.pixelSize:12;wrapMode:Text.WrapAnywhere}
                        Text {Layout.leftMargin:18;Layout.rightMargin:18;Layout.fillWidth:true;Layout.topMargin:7;text:selected.rawText||"";color:muted;font.pixelSize:11;wrapMode:Text.Wrap}
                        Text {Layout.leftMargin:18;Layout.rightMargin:18;Layout.fillWidth:true;Layout.topMargin:10;text:selected.note||"";color:muted;font.pixelSize:11;wrapMode:Text.Wrap;lineHeight:1.4}
                        Text {Layout.leftMargin:18;Layout.topMargin:16;text:bus.serialMode?"最近采样 · 虚拟设备反馈":"最近采样 · 模拟趋势";color:muted;font.pixelSize:12;visible:!!selected.numeric}
                        Canvas {
                            id:trend;Layout.fillWidth:true;Layout.leftMargin:18;Layout.rightMargin:18;Layout.topMargin:8;Layout.preferredHeight:66;visible:!!selected.numeric
                            property var points:twin.history
                            onPointsChanged:requestPaint()
                            onWidthChanged:requestPaint()
                            onPaint:{
                                var c=getContext("2d");c.clearRect(0,0,width,height);c.strokeStyle="#284054";c.lineWidth=1;c.beginPath();c.moveTo(0,height-6);c.lineTo(width,height-6);c.stroke()
                                var vals=points.filter(function(v){return v!==null&&v!==undefined})
                                if(vals.length<2)return
                                var lo=Math.min.apply(null,vals),hi=Math.max.apply(null,vals);if(hi-lo<.001){hi+=1;lo-=1}
                                c.strokeStyle="#6ee3c3";c.lineWidth=1.7;c.beginPath();var connected=false
                                for(var i=0;i<points.length;i++){if(points[i]===null||points[i]===undefined){connected=false;continue}var x=i/(Math.max(1,points.length-1))*width,y=height-8-(points[i]-lo)/(hi-lo)*(height-16);if(connected)c.lineTo(x,y);else c.moveTo(x,y);connected=true}c.stroke()
                            }
                        }
                        Text {Layout.leftMargin:18;Layout.rightMargin:18;Layout.fillWidth:true;Layout.topMargin:12;text:"配方容量、时长和 PR 点号来自配置；三维坐标及传感器反馈为演示模拟值。";color:"#6f8ba3";font.pixelSize:11;wrapMode:Text.Wrap;lineHeight:1.4}
                        Item {Layout.preferredHeight:18}
                    }
                }
            }
        }
        Rectangle {Layout.fillWidth:true;Layout.preferredHeight:1;color:line}
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:118;color:"#101f30"
            ColumnLayout {
                anchors.fill:parent;anchors.leftMargin:18;anchors.rightMargin:18;anchors.topMargin:10;anchors.bottomMargin:10;spacing:7
                RowLayout {
                    Text {text:"工艺进度";color:ink;font.bold:true;font.pixelSize:12}
                    Text {text:(twin.currentStep+1)+" / 12   "+twin.stepNote;color:muted;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideRight}
                    Text {text:twin.productionPoint;color:muted;font.pixelSize:11}
                }
                GridLayout {
                    Layout.fillWidth:true;columns:6;rowSpacing:5;columnSpacing:6
                    Repeater {
                        model:twin.steps
                        delegate:Rectangle {
                            Layout.fillWidth:true;Layout.preferredHeight:28;radius:4
                            color:index===twin.currentStep?"#1a4348":"#152a3e"
                            border.color:index===twin.currentStep?"#39726c":"#233b51"
                            RowLayout {anchors.fill:parent;anchors.leftMargin:9;anchors.rightMargin:9
                                Text {text:(index+1<10?"0":"")+(index+1);color:index===twin.currentStep?accent:muted;font.family:"Consolas";font.pixelSize:11}
                                Text {text:modelData.label;color:index===twin.currentStep?accent:ink;Layout.fillWidth:true;font.pixelSize:12}
                                Text {visible:index===twin.currentStep;text:Math.floor(twin.progress*100)+"%";color:accent;font.pixelSize:11}
                            }
                            MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;enabled:twin.fault===""&&!bus.serialMode;onClicked:twin.jumpToStep(index)}
                        }
                    }
                }
                Text {text:bus.serialMode?"串口模式：由外部主站发送 PR / 灌装 / 阀门指令；阶段标签由站 240 提供，设备反馈为虚拟计算":"点击阶段可单独预览（不计产量） · 完整运行按 "+twin.recipe.batches+" 批 × "+twin.recipe.units_per_batch+" 件生产，全部完成后执行 CIP · 短灌装动作放慢至 3 秒便于观察";color:muted;font.pixelSize:11}
            }
        }
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:27;color:"#0b1726"
            RowLayout {
                anchors.fill:parent;anchors.leftMargin:18;anchors.rightMargin:18
                Text {text:bus.serialMode?"数据源：单串口 Modbus RTU / 指令真实接收，设备反馈为虚拟计算":"数据源：内置模拟器 / 未打开串口";color:"#7992aa";font.pixelSize:11}
                Item {Layout.fillWidth:true}
                Text {text:"PR 空间映射为示意 · beverage_full / "+(twin.profileIndex?"example5":"example4")+" · v0.3";color:"#7992aa";font.pixelSize:11}
            }
        }
    }
}
