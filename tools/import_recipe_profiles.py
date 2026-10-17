"""Import the repository's six/nine recipe cases without modifying them."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

project = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else project.parent / "tmp/industrial_config_engine/examples/beverage_full"
configuration = project / "config"
manifest = {}
for origin, name in [("example4", "baseline"), ("example5", "extended")]:
    destination = configuration / "recipes" / name
    destination.mkdir(parents=True, exist_ok=True)
    files = [source / origin / "device_registry.json", source / origin / "system_variables.json"]
    files += list((source / origin / "layered").rglob("*.json"))
    for file in files:
        relative = file.relative_to(source / origin)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
        manifest[f"recipes/{name}/{relative.as_posix()}"] = hashlib.sha256(target.read_bytes()).hexdigest()

profile = json.loads((configuration / "demo_profile.json").read_text(encoding="utf-8"))
profile["source"] = "industrial_config_engine/examples/beverage_full/example4 → example5"
profile["notice"] = "外部 JSON 选择口味和规格、生产点、容量与灌装时长；三维 PR 映射为展示标定，不执行完整 L1/L2，不打开串口。"
keys = ["home", "empty", "empty", "rinse", "fill", "fill", "fill", "closure", "code", "finished", "home", "home"]
actives = [["X","Y1","Y2","Z"], ["Infeed","Gripper","ContainerSensor"], ["BottleSensor","ContainerSensor"], ["RinseValve","PressureSensor"], ["X","Y1","Y2","Z"], ["${fill_device}","FlowMeter","LevelSensor","PressureSensor"], ["LevelSensor"], ["Closure","CapFeeder","Relay","Torque"], ["Relay","BottleSensor"], ["Gripper","Outfeed","BottleSensor"], ["CIPValve","RinseValve","PressureSensor","Relay"], ["X","Y1","Y2","Z"]]
for i, step in enumerate(profile["steps"]):
    step.pop("point", None)
    step["route"] = keys[i]
    step["active"] = actives[i]
    if i == 7: step["label"] = "封口"
    if i == 10: step["note"] = "本配方全部批次完成后执行一次 CIP；时长压缩演示"
    if i == 5: step["note"] = "选定口味的共用灌装头，容量和设定时长读取当前配方"

existing = {d["id"]: d for d in profile["devices"]}
def device(identity, title, kind, metric, unit, point, note, x, z, source_kind="模拟反馈"):
    return dict(id=identity, name=title, category=kind, metric=metric, unit=unit, point=point, sourceKind=source_kind, note=note, x=x, z=z)
devices = [existing[k] for k in ["X","Y1","Y2","Z"]]
for axis in devices:
    axis["metric"] = "PR 映射示意位置"
    axis["unit"] = "展示单位"
    axis["note"] = "位置读数取自三维标定映射，尚非现场毫米；源配置反馈为 FC03 0x602C × 2 / int32 BE。"
for identity, title, x in [("Infeed","上瓶电机",-6), ("Closure","封口电机",3.5), ("Outfeed","出料电机",5.6)]:
    devices.append(device(identity,title,"运动","模拟 PR 动作","","FC06 0x6002 / FC04 0x0000", "动作点号取自当前配方；同一电机支持各包装规格。",x,0))
for identity, title, x in [("OrangeFill","橙汁灌装头",-2), ("AppleFill","苹果汁灌装头",0), ("GrapeFill","葡萄汁灌装头",2)]:
    devices.append(device(identity,title,"灌装","模拟已灌装量","mL","FC06 0x0010 容量 / 0x0011 时长；FC05 0x0000 启停；FC04 0x0000 完成；FC03 0x0012 体积", "固定口味灌装头，三种包装共用；包装改变容器的 Y/Z PR。",x,0))
devices += [device("Gripper","容器夹爪","执行","模拟夹持状态","","FC05 coil 0x0000","夹取后携带当前包装容器，出料阶段松夹。",-6,0,"模拟指令"),
            device("CapFeeder","供盖机构","执行","供盖指令","","FC05 coil 0x0000","供盖启停；封口形式与电机 PR 由当前配方决定。",3.5,-1,"模拟指令"),
            device("ContainerSensor","空容器检测","检测","模拟到位","","FC04 0x0000","上瓶与夹取检测；对应源配置的独立端点。",-6,.8)]
devices += [existing[k] for k in ["RinseValve","CIPValve","Relay","BottleSensor","LevelSensor","PressureSensor","FlowMeter","Torque"]]
for item in devices:
    if item["id"] == "FlowMeter":
        item["point"] = "FC06 0x0001 清零；FC03 0x0000 × 2 / float LE"
    if item["id"] == "Torque":
        item["name"] = "封口扭矩检测"
        item["note"] = "源配置在三种包装的共用封口阶段均检查扭矩；封口动作关联 Closure、CapFeeder 与 Relay。"
profile["devices"] = devices
(configuration / "demo_profile.json").write_text(json.dumps(profile,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

visual = {
 "notice":"PR 点号是设备程序索引；以下空间映射只为本演示标定，不是现场毫米坐标。",
 "flavors":[{"id":"orange","label":"橙汁","device":"OrangeFill","color":"#e5ab50"},{"id":"apple","label":"苹果汁","device":"AppleFill","color":"#8fc677"},{"id":"grape","label":"葡萄汁","device":"GrapeFill","color":"#b58bdb"}],
 "formats":[{"id":"family","label":"2 L 家庭装","height":.84,"radius":.24,"kind":"jug"},{"id":"can","label":"330 mL 罐装","height":.38,"radius":.18,"kind":"can"},{"id":"small","label":"250 mL 小瓶装","height":.50,"radius":.14,"kind":"bottle"}],
 "x_pr":{"0":-6.8,"1":-6.2,"2":-5.7,"3":-5.2,"4":-2,"5":0,"6":2,"7":3.2,"8":3.5,"9":3.8,"10":5,"11":5.55,"12":6.1,"13":-3.8,"14":4.4},
 "y_pr":{"0":0,"1":-.8,"2":0,"3":.8,"4":-.8,"5":0,"6":.8,"7":-.8,"8":0,"9":.8,"10":-.3,"11":0,"12":.3},
 "z_pr":{"0":0,"1":0,"2":.45,"3":.95,"4":.82}
}
(configuration / "visualization.json").write_text(json.dumps(visual,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
(configuration / "source_manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
entries = sorted(configuration.rglob("*.json"))
qml = sorted((project / "qml").glob("*.qml"))
(project / "resources.qrc").write_text('<RCC><qresource prefix="/">\n'+''.join('  <file>'+p.relative_to(project).as_posix()+'</file>\n' for p in qml+entries)+'</qresource></RCC>\n',encoding="utf-8")
print(f"Imported {len(manifest)} source JSON files; 21 devices, 6 → 9 recipes")
