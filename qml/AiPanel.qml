import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14

Item {
    id: panel
    property color ink: "#dbe9f5"
    property color muted: "#879eb5"
    property color accent: "#6ee3c3"
    signal serialSettings()
    Connections {target:ai;onCandidateReady:{reviewPoints.checked=false;reviewRequirements.checked=false}}
    ColumnLayout {
        anchors.fill:parent;spacing:10
        RowLayout {
            Text {text:"自然语言 → 配置 → 验证 → 数字孪生";font.pixelSize:23;font.bold:true;color:ink;Layout.fillWidth:true}
            ActionButton {text:"查看演示记录";enabled:ai.directory.length>0;onClicked:ai.showRecords()}
            ActionButton {text:"串口设置 / 联调";onClicked:panel.serialSettings()}
        }
        Text {text:"论文对应：固定 L1/L2 → 生成 L3/L4 与配方增量 → 结构验证 → 仿真验证 → 人工语义审核 → 串口演示";color:muted;font.pixelSize:12}
        RowLayout {
            Layout.fillWidth:true;spacing:7
            Repeater {
                model:["01 自然语言","02 DeepSeek JSON","03 结构 / C++","04 完整工艺仿真","05 人工审核","06 串口 → 3D"]
                Rectangle {
                    Layout.fillWidth:true;Layout.preferredHeight:40;radius:5
                    color:index===0||index===1&&ai.rawJson.length>0||index===2&&ai.gate>=1||index===3&&ai.gate>=2||index===4&&ai.applied||index===5&&ai.gate>=4?"#174638":"#142a3c"
                    Text {anchors.centerIn:parent;text:modelData;color:ink;font.pixelSize:12}
                }
            }
        }
        Rectangle {
            color:"#102337";radius:6;Layout.fillWidth:true;Layout.preferredHeight:182
            ColumnLayout {
                anchors.fill:parent;anchors.margins:10;spacing:7
                RowLayout {
                    Text {text:"DeepSeek Key";color:muted}
                    TextField {id:keyInput;Layout.fillWidth:true;Layout.preferredHeight:32;echoMode:TextInput.Password;placeholderText:ai.hasEnvironmentKey?"已读取环境变量；留空即可":"仅本次使用，不保存";enabled:!ai.busy}
                    Text {text:"模型";color:muted}
                    TextField {id:modelInput;text:ai.defaultModel;Layout.preferredWidth:160;Layout.preferredHeight:32;enabled:!ai.busy}
                    Text {text:"最多轮数";color:muted}
                    SpinBox {id:roundLimit;from:1;to:6;value:3;Layout.preferredWidth:105;Layout.preferredHeight:32;enabled:!ai.busy}
                }
                ScrollView {
                    Layout.fillWidth:true;Layout.fillHeight:true;clip:true
                    TextArea {id:promptInput;selectByMouse:true;wrapMode:TextEdit.Wrap;color:ink;font.pixelSize:13;enabled:!ai.busy
                        text:"请在现有橙汁、苹果汁、葡萄汁的2L家庭装和330mL罐装基础上，为三种口味都增加250mL小瓶装。每种口味两批、每批六瓶。沿用十二阶段流程和三个灌装头，使用预置工程点位，保留原六配方，新增三个生产入口。"}
                }
                RowLayout {
                    ActionButton {objectName:"aiGenerateButton";text:ai.busy?"处理中…":"生成 JSON 并验证";primary:true;enabled:!ai.busy&&!master.busy;onClicked:{ai.generate(promptInput.text,keyInput.text,modelInput.text,roundLimit.value);keyInput.clear()}}
                    ActionButton {text:"取消";danger:true;enabled:ai.busy;onClicked:ai.cancel()}
                    Text {text:"现场案例限定：三口味新增250mL；坐标来自预先标定数据。失败会反馈给AI，最多6轮，可重试。";color:muted;font.pixelSize:11;wrapMode:Text.Wrap;Layout.fillWidth:true}
                }
            }
        }
        Text {text:(ai.round===0&&ai.rawJson.length>0?"历史记录":"第 "+ai.round+" 轮")+"  ·  "+ai.status;color:ai.ready||ai.applied?accent:ink;wrapMode:Text.Wrap;Layout.fillWidth:true;font.pixelSize:13}
        RowLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:10
            Rectangle {
                color:"#0f1e2f";radius:6;Layout.preferredWidth:365;Layout.fillHeight:true
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:10
                    Text {text:"验证与自动修正过程";color:ink;font.bold:true}
                    ListView {id:events;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:ai.logs;spacing:7
                        onCountChanged:positionViewAtEnd()
                        delegate:Text {width:events.width;text:modelData;color:muted;font.pixelSize:12;wrapMode:Text.Wrap}
                        ScrollBar.vertical:ScrollBar {}}
                }
            }
            Rectangle {
                color:"#0f1e2f";radius:6;Layout.fillWidth:true;Layout.fillHeight:true
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:10;spacing:6
                    TabBar {id:outputTab;Layout.fillWidth:true;TabButton {text:"DeepSeek 原始 JSON"} TabButton {text:"配置文件差异"} TabButton {text:"语义审核说明"}}
                    ScrollView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true
                        TextArea {readOnly:true;selectByMouse:true;wrapMode:TextEdit.Wrap;color:ink;font.family:outputTab.currentIndex===2?"Microsoft YaHei":"Consolas";font.pixelSize:12
                            text:outputTab.currentIndex===0?(ai.rawJson||"DeepSeek 的每轮完整输出会原样保留。首次通过时如实显示，无需人为制造错误。"):outputTab.currentIndex===1?(ai.diff||"通过自动验证后，显示实际变更差异。"):(ai.summary||"自动验证不替代人工语义审核。请核对需求、容器规格、上瓶点位、三个共享灌装头及成品点位。\n\n完整仿真：九个配方与11个既定故障；故障场景沿用原仓库，仅作用于原配方索引0。\n\n串口为已验证写轨迹回放；接收端实时反馈虚拟设备状态，未实现原引擎的在线条件解释。\n工艺等待50倍加速，完整仿真中的时长不变；运动与灌装保留接收端实际时长。")}
                    }
                }
            }
        }
        Rectangle {
            color:"#102d32";radius:6;Layout.fillWidth:true;Layout.preferredHeight:138
            ColumnLayout {
                anchors.fill:parent;anchors.margins:10;spacing:6
                RowLayout {
                    CheckBox {id:reviewRequirements;objectName:"aiReviewRequirements";text:"已核对：三口味、新规格、原六配方保持";enabled:ai.ready&&!ai.busy;font.pixelSize:12}
                    CheckBox {id:reviewPoints;objectName:"aiReviewPoints";text:"已核对：上瓶 / 灌装容器 / 成品点位";enabled:ai.ready&&!ai.busy;font.pixelSize:12}
                    Item {Layout.fillWidth:true}
                    ActionButton {objectName:"aiApplyButton";text:ai.applied?"已审核并应用":"人工审核通过并应用";primary:true;enabled:ai.ready&&!ai.applied&&!ai.busy&&!master.busy&&reviewRequirements.checked&&reviewPoints.checked;onClicked:ai.approve()}
                }
                RowLayout {
                    Text {text:"已应用配方";color:ink}
                    ComboBox {model:twin.flavorLabels;currentIndex:twin.flavorIndex;enabled:ai.applied&&!master.busy;onActivated:twin.selectRecipe(currentIndex,twin.formatIndex);Layout.preferredWidth:115;Layout.preferredHeight:32}
                    ComboBox {model:twin.formatLabels;currentIndex:twin.formatIndex;enabled:ai.applied&&!master.busy;onActivated:twin.selectRecipe(twin.flavorIndex,currentIndex);Layout.preferredWidth:145;Layout.preferredHeight:32}
                    ComboBox {id:playMode;model:["首瓶片段 · PPT","完整12瓶 · 论文"];enabled:!master.busy;Layout.preferredWidth:160;Layout.preferredHeight:32}
                    ActionButton {objectName:"aiReplayButton";text:"串口执行 → 3D";primary:true;enabled:ai.applied&&!ai.busy&&master.connected&&!master.busy;onClicked:ai.replay(playMode.currentIndex===0)}
                    ActionButton {text:"中止";danger:true;enabled:master.busy;onClicked:master.stopDemo()}
                }
                Text {text:master.status+"  ·  有效应答 "+master.acknowledgements+" / 错误 "+master.failures+"  ·  工艺等待50×加速，运动与灌装按实际时长；首瓶片段包含初始化、首瓶、CIP和回零。";color:muted;font.pixelSize:11;wrapMode:Text.Wrap;Layout.fillWidth:true}
            }
        }
    }
}
