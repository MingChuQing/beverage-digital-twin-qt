import QtQuick 2.14
import QtQuick.Window 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14

ApplicationWindow {
    id: senderWindow
    visible:true;width:1240;height:960;minimumWidth:1120;minimumHeight:900
    title:"DeepSeek 配置生成与验证 · 饮料数字孪生演示"
    color:"#0b1726";font.family:"Microsoft YaHei";font.pixelSize:13
    palette.window:"#102033";palette.base:"#15283c";palette.button:"#15283c";palette.text:"#dbe9f5";palette.buttonText:"#dbe9f5";palette.windowText:"#dbe9f5";palette.highlight:"#27665c"
    property color ink:"#dbe9f5"
    property color muted:"#879eb5"
    property color accent:"#6ee3c3"
    property int selectedStation:master.address(master.deviceIds[deviceBox.currentIndex]||"X")
    TabBar {id:senderTabs;anchors.top:parent.top;anchors.left:parent.left;anchors.right:parent.right;anchors.margins:20;height:40
        TabButton {text:"AI 配置生成 · 论文演示"}
        TabButton {text:"Modbus 串口联调"}
    }
    StackLayout {anchors.fill:parent;anchors.margins:20;anchors.topMargin:72;currentIndex:senderTabs.currentIndex
    AiPanel {onSerialSettings:senderTabs.currentIndex=1}
    Item {
    ColumnLayout {
        anchors.fill:parent;anchors.margins:20;spacing:12
        RowLayout {
            Text {text:"Modbus RTU · 指令发送端";color:ink;font.pixelSize:23;font.bold:true;Layout.fillWidth:true}
            Text {text:"主站 → TTL 串口 → 数字孪生从站";color:muted}
        }
        Text {text:"默认：发送 COM3，接收 COM10。两端波特率及校验位保持一致；接收端切到“串口接收”并打开端口。";color:muted;font.pixelSize:12}
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:58;color:"#132a3b";radius:6
            RowLayout {
                anchors.fill:parent;anchors.margins:10;spacing:9
                Text {text:"发送串口";color:muted}
                ComboBox {id:portBox;editable:true;model:master.availablePorts;currentIndex:master.availablePorts.indexOf(master.portName);editText:master.portName;Component.onCompleted:editText=master.portName;enabled:!master.connected;Layout.preferredWidth:128;Layout.preferredHeight:34}
                ComboBox {id:baudBox;model:["9600","19200","38400","57600","115200"];currentIndex:model.indexOf(String(master.baudRate));enabled:!master.connected;Layout.preferredWidth:112;Layout.preferredHeight:34}
                ActionButton {text:"刷新";onClicked:master.refreshPorts()}
                ActionButton {objectName:"senderConnectButton";text:master.connected?"关闭串口":"打开串口";primary:!master.connected;onClicked:master.connected?master.closePort():master.openPort(portBox.editText,Number(baudBox.currentText))}
                Text {text:master.status;color:master.failures>0?"#ffacb5":accent;Layout.fillWidth:true;elide:Text.ElideRight;font.pixelSize:12}
            }
        }
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:104;color:"#102337";radius:6
            ColumnLayout {
                anchors.fill:parent;anchors.margins:12;spacing:8
                RowLayout {
                    Text {text:"配方联动";color:ink;font.bold:true}
                    ComboBox {model:["原版 6 配方","扩展 9 配方"];currentIndex:twin.profileIndex;enabled:!master.busy;onActivated:twin.loadProfile(currentIndex);Layout.preferredWidth:145;Layout.preferredHeight:32}
                    ComboBox {model:twin.flavorLabels;currentIndex:twin.flavorIndex;enabled:!master.busy;onActivated:twin.selectRecipe(currentIndex,twin.formatIndex);Layout.preferredWidth:125;Layout.preferredHeight:32}
                    ComboBox {model:twin.formatLabels;currentIndex:twin.formatIndex;enabled:!master.busy;onActivated:twin.selectRecipe(twin.flavorIndex,currentIndex);Layout.preferredWidth:168;Layout.preferredHeight:32}
                    Item {Layout.fillWidth:true}
                    ActionButton {text:"下发配方选择";enabled:master.connected;onClicked:master.applyRecipe()}
                    ActionButton {objectName:"serialDemoButton";text:"发送单件工艺";primary:true;enabled:master.connected&&!master.busy;onClicked:master.startDemo()}
                    ActionButton {text:"中止 / 暂停";danger:true;enabled:master.connected;onClicked:master.stopDemo()}
                }
                Text {text:twin.recipeCaption+" · "+twin.volumeMl+" mL / "+twin.recipe.fill_time_ms+" ms · 运动 PR、灌装头和阀门分别下发，接收端按指令动作。";color:accent;font.pixelSize:12}
                Text {text:"站 240：0x0100–0102 选择配置/口味/规格；0x0103 阶段；0x0104 运行控制；0x0105 成品计数。";color:muted;font.pixelSize:11}
            }
        }
        RowLayout {
            Layout.fillWidth:true;spacing:12
            Rectangle {
                Layout.fillWidth:true;Layout.preferredHeight:130;color:"#102337";radius:6
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:12;spacing:10
                    Text {text:"运动控制 · 原接口 FC06 / 0x6002";color:ink;font.bold:true}
                    RowLayout {
                        ComboBox {id:axisBox;model:["X","Y1","Y2","Z","Infeed","Closure","Outfeed"];Layout.preferredWidth:130;Layout.preferredHeight:34}
                        Text {text:"PR";color:muted}
                        SpinBox {id:prBox;from:0;to:15;value:4;Layout.preferredWidth:110;Layout.preferredHeight:34}
                        ActionButton {text:"触发动作";enabled:master.connected;onClicked:master.triggerPr(axisBox.currentText,prBox.value)}
                    }
                    Text {text:"写入值 = 0x10 + PR；FC03 0x602C 读位置，FC04 0x0000 读完成。";color:muted;font.pixelSize:11}
                }
            }
            Rectangle {
                Layout.fillWidth:true;Layout.preferredHeight:130;color:"#102337";radius:6
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:12;spacing:10
                    Text {text:"当前口味灌装头 · "+twin.activeFillDevice+" / 站 "+master.address(twin.activeFillDevice);color:twin.flavorColor;font.bold:true}
                    RowLayout {
                        TextField {id:volumeBox;text:String(twin.volumeMl);placeholderText:"容量 mL";Layout.preferredWidth:85;Layout.preferredHeight:34}
                        TextField {id:timeBox;text:String(twin.recipe.fill_time_ms);placeholderText:"时长 ms";Layout.preferredWidth:100;Layout.preferredHeight:34}
                        ActionButton {text:"设置并灌装";enabled:master.connected;onClicked:{if(master.send(master.address(twin.activeFillDevice),16,"0x0010",volumeBox.text+","+timeBox.text))master.toggle(twin.activeFillDevice,true)}}
                        ActionButton {text:"停止";enabled:master.connected;onClicked:master.toggle(twin.activeFillDevice,false)}
                    }
                    Text {text:"容量 0x0010 / 时长 0x0011；FC05 线圈 0 开关。";color:muted;font.pixelSize:11}
                }
            }
        }
        RowLayout {
            spacing:9
            Text {text:"执行器";color:ink;font.bold:true}
            ComboBox {id:actuatorBox;model:["Gripper","RinseValve","CIPValve","CapFeeder"];Layout.preferredWidth:165;Layout.preferredHeight:32}
            ActionButton {text:"开启";enabled:master.connected;onClicked:master.toggle(actuatorBox.currentText,true)}
            ActionButton {text:"关闭";enabled:master.connected;onClicked:master.toggle(actuatorBox.currentText,false)}
            Text {text:"接收有效应答 "+master.acknowledgements+" 次 · 错误 "+master.failures+" 次";color:muted;Layout.fillWidth:true;horizontalAlignment:Text.AlignRight}
        }
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:102;color:"#102337";radius:6
            ColumnLayout {
                anchors.fill:parent;anchors.margins:12;spacing:8
                RowLayout {
                    ComboBox {id:deviceBox;model:master.deviceLabels;Layout.preferredWidth:290;Layout.preferredHeight:32;onActivated:stationBox.value=selectedStation}
                    Text {text:"站号";color:muted}
                    SpinBox {id:stationBox;from:0;to:247;value:selectedStation;Layout.preferredWidth:100;Layout.preferredHeight:32}
                    ComboBox {id:functionBox;model:["03 读保持寄存器","04 读输入寄存器","05 写单线圈","06 写单寄存器","16 写多寄存器"];Layout.preferredWidth:172;Layout.preferredHeight:32}
                    TextField {id:registerBox;text:"0x602C";placeholderText:"寄存器（0x 前缀）";Layout.preferredWidth:110;Layout.preferredHeight:32}
                    TextField {id:valueBox;text:"2";placeholderText:"数量 / 数值";Layout.fillWidth:true;Layout.preferredHeight:32}
                    ActionButton {text:"发送";enabled:master.connected;onClicked:master.send(stationBox.value,[3,4,5,6,16][functionBox.currentIndex],registerBox.text,valueBox.text)}
                }
                Text {text:"读命令填写寄存器数量；FC05 写 0xFF00 / 0；FC06 填数值；FC16 用逗号分隔。站号 0 仅用于广播写入，不等待应答。";color:muted;font.pixelSize:11}
            }
        }
        Rectangle {
            Layout.fillWidth:true;Layout.fillHeight:true;color:"#0f1e2f";radius:6
            ColumnLayout {
                anchors.fill:parent;anchors.margins:12;spacing:8
                Text {text:"RTU 收发日志 · CRC 由程序自动生成并校验";color:ink;font.bold:true}
                ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:master.frames;spacing:3;delegate:Text {width:ListView.view.width;text:modelData;color:index===0?accent:muted;font.family:"Consolas";font.pixelSize:12;elide:Text.ElideRight} ScrollBar.vertical:ScrollBar {}}
            }
        }
        Text {text:"接线：一根线 TX → 另一根 RX，两根反向互接并连接 GND；VCC 不相接。两个程序各占用一个端口。";color:muted;font.pixelSize:11}
    }
    }
    }
}
