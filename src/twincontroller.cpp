#include "twincontroller.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTime>
#include <QTemporaryDir>
#include <QtMath>
#include <cmath>
#include <functional>

QVariant DeviceModel::data(const QModelIndex &i,int role) const {
    if(!i.isValid()||i.row()<0||i.row()>=m_rows.size())return {};
    return m_rows[i.row()].toMap().value(QString::fromLatin1(roleNames().value(role)));
}
QHash<int,QByteArray> DeviceModel::roleNames() const {
    return {{DeviceId,"deviceId"},{DisplayName,"displayName"},{Category,"category"},{StatusText,"statusText"},
            {StatusKind,"statusKind"},{ValueText,"valueText"},{Unit,"unit"},{Quality,"quality"}};
}
void DeviceModel::initialize(const QVariantList &rows){beginResetModel();m_rows=rows;endResetModel();}
void DeviceModel::updateRow(int i,const QVariantMap &r){if(i<0||i>=m_rows.size()||m_rows[i].toMap()==r)return;m_rows[i]=r;emit dataChanged(index(i),index(i));}
QVariantMap DeviceModel::device(const QString &id) const{for(const auto &r:m_rows)if(r.toMap().value("deviceId")==id)return r.toMap();return {};}

static QJsonObject readObject(const QString &path,QString &error){
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)){error="无法读取配置："+path;return {};}
    QJsonParseError parse;
    auto document=QJsonDocument::fromJson(f.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()){error="无效 JSON："+path;return {};}
    return document.object();
}
static bool isInteger(const QJsonValue &value,int minimum,int maximum){
    return value.isDouble()&&value.toDouble()==value.toInt()&&value.toInt()>=minimum&&value.toInt()<=maximum;
}
TwinController::TwinController(QObject *parent,bool automaticTimer,const QString &root):QObject(parent),m_model(this){
    if(root.isEmpty())m_activationPath=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ai-active.json";
    if(!root.isEmpty())m_configRoot=root;
    else {
        const QString app=QCoreApplication::applicationDirPath();
        for(const auto &candidate:QStringList{app+"/config",app+"/../../config",":/config"}){
            if(QFile::exists(candidate+"/demo_profile.json")){m_configRoot=candidate;break;}
        }
    }
    if(!loadProfile(0))m_configError=m_loadMessage;
    m_wallClock.start();m_timer.setInterval(50);
    connect(&m_timer,&QTimer::timeout,this,[this]{advance(qMin(m_wallClock.restart()/1000.0,.25));});
    if(automaticTimer)m_timer.start();
}

bool TwinController::loadCandidateDirectory(const QString &folder){
    const QString previous=m_candidateDirectory;m_candidateDirectory=folder;
    if(loadProfile(1))return true;
    m_candidateDirectory=previous;return false;
}
static QString fileDigest(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex());}
bool TwinController::publishCandidateActivation(const QString &reportPath,bool testing,QString &error){
    auto report=readObject(reportPath,error);if(!error.isEmpty())return false;
    QString reviewPath=QFileInfo(reportPath).absolutePath()+"/human_review.json";
    auto review=readObject(reviewPath,error);
    if(!error.isEmpty()||review.value("status").toString()!=(testing?"TEST_ONLY":"APPROVED")||review.value("report_sha256").toString()!=fileDigest(reportPath)){error="审核记录与候选不匹配";return false;}
    QJsonObject activation{{"candidate",m_candidateDirectory},{"candidate_hashes",report.value("candidate_hashes")},
        {"report",reportPath},{"report_sha256",fileDigest(reportPath)},{"review",reviewPath},{"review_sha256",fileDigest(reviewPath)},{"test_only",testing}};
    if(m_activationPath.isEmpty()||!QDir().mkpath(QFileInfo(m_activationPath).absolutePath())){error="无法创建配置激活目录";return false;}
    QSaveFile file(m_activationPath);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
    auto bytes=QJsonDocument(activation).toJson();
    if(file.write(bytes)!=bytes.size()||!file.commit()){error="无法原子保存配置激活记录";return false;}
    return true;
}
bool TwinController::loadSharedCandidate(){
    if(m_activationPath.isEmpty()||!QFile::exists(m_activationPath))return true;
    auto fail=[&](const QString &why){m_loadMessage="拒绝加载共享候选："+why;addLog(m_loadMessage);emit configChanged();return false;};
    QString error;auto activation=readObject(m_activationPath,error);if(!error.isEmpty())return fail(error);
    if(activation.value("test_only").toBool()&&!m_allowTestActivation)return fail("测试审核不能用于正常运行");
    const QString folder=activation.value("candidate").toString(),reportPath=activation.value("report").toString(),reviewPath=activation.value("review").toString();
    if(folder.isEmpty()||fileDigest(reportPath)!=activation.value("report_sha256").toString()||fileDigest(reviewPath)!=activation.value("review_sha256").toString())return fail("报告 / 审核哈希不一致");
    auto report=readObject(reportPath,error),review=readObject(reviewPath,error);
    if(!error.isEmpty()||report.value("status").toString()!="AUTO_PASS"||report.value("candidate_hashes")!=activation.value("candidate_hashes")||
       review.value("status").toString()!=(activation.value("test_only").toBool()?"TEST_ONLY":"APPROVED")||review.value("candidate_hashes")!=report.value("candidate_hashes")||review.value("report_sha256").toString()!=fileDigest(reportPath))return fail("验证或审核状态不一致");
    auto hashes=activation.value("candidate_hashes").toObject();if(hashes.isEmpty())return fail("缺少候选哈希清单");
    for(auto it=hashes.begin();it!=hashes.end();++it){
        QString relative=QDir::cleanPath(it.key());
        if(QDir::isAbsolutePath(relative)||relative==".."||relative.startsWith("../")||fileDigest(folder+"/"+relative)!=it.value().toString())return fail("候选文件被修改："+it.key());
    }
    if(m_candidateDirectory!=folder||m_profile!=1)return loadCandidateDirectory(folder);
    return true;
}
bool TwinController::loadProfile(int index){
    auto fail=[&](const QString &why){m_loadMessage="加载失败，保留当前配置："+why;addLog(m_loadMessage);emit configChanged();return false;};
    if(index!=0&&index!=1)return fail("配置版本编号无效");
    QString error;
    const QString folder=index&&!m_candidateDirectory.isEmpty()?m_candidateDirectory:m_configRoot+"/recipes/"+(index?"extended":"baseline");
    auto presentation=readObject(m_configRoot+"/demo_profile.json",error);
    auto visual=readObject(m_configRoot+"/visualization.json",error);
    auto registry=readObject(folder+"/device_registry.json",error).value("devices").toObject();
    auto variables=readObject(folder+"/system_variables.json",error);
    auto runner=readObject(folder+"/layered/L3_group/run_recipe.group.json",error);
    auto batch=readObject(folder+"/layered/L3_group/produce_batch.group.json",error);
    if(!error.isEmpty())return fail(error);
    auto points=variables.value("production_points").toArray();
    auto recipes=variables.value("recipes").toArray();
    auto lookup=variables.value("recipe_lookup").toArray();
    const int formats=index?3:2;
    if(recipes.size()!=3*formats||lookup.size()!=3||registry.size()!=21||points.isEmpty())return fail("配方表、查找表或设备数量不匹配");
    const auto args=runner.value("args").toArray();
    if(args.size()!=2||args.at(0).toObject().value("min").toInt(-1)!=0||args.at(0).toObject().value("max").toInt(-1)!=2||args.at(1).toObject().value("min").toInt(-1)!=0||args.at(1).toObject().value("max").toInt(-1)!=formats-1)return fail("L3 口味 / 规格边界与所加载配置不一致");
    int batches=0;
    for(const auto &value:runner.value("body").toArray()){
        const auto group=value.toObject();
        if(group.value("mode").toString()=="loop"){
            const auto loop=group.value("loop").toObject();
            if(batches||loop.value("type").toString()!="count"||!isInteger(loop.value("count"),1,65535))return fail("L3 批次数循环不受此演示支持");
            batches=loop.value("count").toInt();
        }
    }
    const auto batchLoop=batch.value("loop").toObject();
    if(!batches||batch.value("mode").toString()!="loop"||batchLoop.value("type").toString()!="count"||!isInteger(batchLoop.value("count"),1,65535))return fail("L3 每批件数循环无效");
    const int unitsPerBatch=batchLoop.value("count").toInt();
    if(qint64(batches)*unitsPerBatch>1000000)return fail("配方数量超过演示支持范围");
    if(visual.value("flavors").toArray().size()!=3||visual.value("formats").toArray().size()<formats)return fail("缺少展示用口味/包装标定");
    for(const auto &row:lookup)if(row.toArray().size()!=formats)return fail("配方查找表列数错误");
    for(const auto &pointValue:points){
        auto point=pointValue.toObject();
        for(const QString &axis:QStringList{"x","y","z"}){
            const auto pr=point.value(axis);
            const auto coordinate=visual.value(axis+"_pr").toObject().value(QString::number(pr.toInt()));
            if(!isInteger(pr,0,15)||!coordinate.isDouble()||!qIsFinite(coordinate.toDouble()))return fail("点位 PR 无效或缺少数值空间标定");
        }
    }
    QSet<QString> identities;
    QSet<QString> fillDevices;
    QSet<int> headPositions;
    for(int f=0;f<3;++f){
        const auto flavor=visual.value("flavors").toArray().at(f).toObject();
        if(flavor.value("label").toString().isEmpty()||fillDevices.contains(flavor.value("device").toString()))return fail("三个口味必须对应三个独立灌装头");
        fillDevices.insert(flavor.value("device").toString());
        int commonX=-1;
        for(int s=0;s<formats;++s){
            const auto recipeIndex=lookup.at(f).toArray().at(s);
            if(!recipeIndex.isDouble()||recipeIndex.toDouble()!=recipeIndex.toInt()||recipeIndex.toInt()<0||recipeIndex.toInt()>=recipes.size())return fail("配方索引越界");
            const auto recipe=recipes.at(recipeIndex.toInt()).toObject();
            const QString id=recipe.value("id").toString();
            if(!QRegularExpression("^[a-z]+_(family|can|small)$").match(id).hasMatch()||identities.contains(id))return fail("配方 ID 无效或重复");
            identities.insert(id);
            if(recipe.value("flavor")!=flavor.value("id")||recipe.value("format")!=visual.value("formats").toArray().at(s).toObject().value("id")||recipe.value("fill_device")!=flavor.value("device")||!registry.contains(recipe.value("fill_device").toString()))return fail("口味、规格或共用灌装头映射错误");
            for(const QString &key:QStringList{"volume_ml","fill_time_ms"})if(!isInteger(recipe.value(key),1,65535))return fail("容量 / 时长必须是有效 u16 正整数");
            if(!isInteger(recipe.value("batches"),1,65535)||!isInteger(recipe.value("units_per_batch"),1,65535)||recipe.value("batches").toInt()!=batches||recipe.value("units_per_batch").toInt()!=unitsPerBatch)return fail("配方批量与 L3 实际循环数量不一致");
            if(!isInteger(recipe.value("home_point"),0,points.size()-1))return fail("回零点索引无效");
            for(const QString &key:QStringList{"infeed_pr","closure_pr","outfeed_pr"})if(!isInteger(recipe.value(key),0,15))return fail("包装电机 PR 必须在 0 至 15 内");
            const QString closure=recipe.value("closure_mode").toString();
            if(!QStringList{"screw_large","seam_can","screw_small"}.contains(closure))return fail("封口类型不受此演示支持");
            const auto shape=visual.value("formats").toArray().at(s).toObject();
            if(shape.value("label").toString().isEmpty()||!shape.value("height").isDouble()||shape.value("height").toDouble()<=0||!shape.value("radius").isDouble()||shape.value("radius").toDouble()<=0||!QStringList{"jug","can","bottle"}.contains(shape.value("kind").toString()))return fail("包装展示尺寸或外形无效");
            for(const QString &stage:QStringList{"empty","rinse","fill","closure","code","finished"})for(const QString &suffix:QStringList{"entry","work"}){
                const auto point=recipe.value(stage+"_"+suffix);
                if(!point.isDouble()||point.toDouble()!=point.toInt()||point.toInt()<0||point.toInt()>=points.size())return fail("配方生产点索引无效");
            }
            int x=points.at(recipe.value("fill_work").toInt()).toObject().value("x").toInt();
            if(commonX>=0&&commonX!=x)return fail("同一口味的固定灌装头 X PR 不一致");
            commonX=x;
            auto entry=readObject(folder+"/layered/L4_flow/"+id+".json",error);
            const auto entryBody=entry.value("body").toArray();
            if(entryBody.size()!=1)return fail("L4 入口必须调用一个共用配方流程");
            const auto call=entryBody.at(0).toObject();
            const auto params=call.value("params").toArray();
            if(!error.isEmpty()||params.size()!=2||!isInteger(params.at(0),f,f)||!isInteger(params.at(1),s,s)||call.value("template").toString()!="L3_group/run_recipe.group.json")return fail("L4 两参数入口与配方查找表不一致");
        }
        if(headPositions.contains(commonX))return fail("三个固定灌装头必须有独立 X PR");
        headPositions.insert(commonX);
    }
    if(index==1){
        auto base=readObject(m_configRoot+"/recipes/baseline/system_variables.json",error);
        auto baseRegistry=readObject(m_configRoot+"/recipes/baseline/device_registry.json",error).value("devices").toObject();
        auto oldRecipes=base.value("recipes").toArray(),oldPoints=base.value("production_points").toArray();
        if(!error.isEmpty()||baseRegistry!=registry||oldRecipes.size()!=6||oldPoints.size()>points.size())return fail("扩展配置的基线或设备表不一致");
        for(int i=0;i<oldRecipes.size();++i)if(oldRecipes.at(i)!=recipes.at(i))return fail("扩展修改了原有配方");
        for(int i=0;i<oldPoints.size();++i)if(oldPoints.at(i)!=points.at(i))return fail("扩展修改了原有点位");
    }
    // Read the stage order from the original shared L3 hierarchy, expanding each
    // loop body once for display. This is a viewer, not an L1/L2 interpreter.
    QStringList stageTemplates;
    QList<int> stageRepetitions;
    QSet<QString> stack;
    std::function<bool(const QJsonObject&,int)> collect;
    collect=[&](const QJsonObject &object,int repetitions){
        if(object.value("mode").toString()=="loop"){
            const auto loop=object.value("loop").toObject();
            if(loop.value("type").toString()!="count"||!isInteger(loop.value("count"),1,65535)||qint64(repetitions)*loop.value("count").toInt()>1000000)return false;
            repetitions*=loop.value("count").toInt();
        }
        for(const auto &value:object.value("body").toArray()){
            const auto child=value.toObject();const QString ref=child.value("template").toString();
            if(ref.startsWith("L3_group/step_")){
                if(!QRegularExpression("^L3_group/step_[0-9]{2}_[a-z_]+\\.group\\.json$").match(ref).hasMatch())return false;
                auto stage=readObject(folder+"/layered/"+ref,error);
                if(!error.isEmpty()||!stage.value("body").isArray())return false;
                stageTemplates.append(ref);
                stageRepetitions.append(repetitions);
            }
            else if(ref.startsWith("L3_group/")){
                if(!QRegularExpression("^L3_group/[a-z_]+\\.group\\.json$").match(ref).hasMatch())return false;
                if(stack.contains(ref))return false;
                stack.insert(ref);auto group=readObject(folder+"/layered/"+ref,error);
                if(!error.isEmpty()||!collect(group,repetitions))return false;
                stack.remove(ref);
            }else if(child.contains("body")&&!collect(child,repetitions))return false;
        }
        return true;
    };
    if(!collect(runner,1)||stageTemplates.size()!=12)return fail("原 L3 流程无法提取十二阶段");
    auto steps=presentation.value("steps").toArray().toVariantList();
    if(steps.size()!=12)return fail("展示阶段配置错误");
    const QStringList routes={"home","empty","empty","rinse","fill","fill","fill","closure","code","finished","home","home"};
    for(int i=0;i<steps.size();++i){
        if(!stageTemplates[i].startsWith(QString("L3_group/step_%1_").arg(i+1,2,10,QChar('0'))))return fail("L3 阶段顺序与演示流程不一致");
        if(stageRepetitions[i]!=(i>=1&&i<=9?batches*unitsPerBatch:1))return fail("L3 重复阶段结构不符：初始化 / CIP / 回零应仅执行一次");
        auto step=steps[i].toMap();
        const double duration=step.value("duration").toDouble();
        if(step.value("route").toString()!=routes[i]||!qIsFinite(duration)||duration<=0)return fail("展示阶段路径或时长无效");
        for(const auto &id:step.value("active").toList())if(id.toString()!="${fill_device}"&&!registry.contains(id.toString()))return fail("展示阶段包含未知设备");
        step["template"]=stageTemplates[i];steps[i]=step;
    }
    QVariantList rows;
    QSet<QString> displayIds;
    QSet<QString> endpoints;
    for(const auto &value:presentation.value("devices").toArray()){
        auto row=value.toObject().toVariantMap();const auto id=row.value("id").toString();
        if(!registry.contains(id)||displayIds.contains(id))return fail("设备显示绑定缺失或重复："+id);
        displayIds.insert(id);
        const auto endpoint=registry.value(id).toObject();
        const QString address=endpoint.value("port").toString()+":"+QString::number(endpoint.value("address").toInt());
        if(endpoint.value("port").toString().isEmpty()||!isInteger(endpoint.value("address"),1,247)||endpoints.contains(address))return fail("设备端口 / 站号无效或重复");
        endpoints.insert(address);
        const auto route=registry.value(id).toObject();row["deviceId"]=id;row["displayName"]=row.value("name");row["port"]=route.value("port").toString();row["station"]=route.value("address").toInt();rows.append(row);
    }
    if(rows.size()!=registry.size())return fail("设备显示数量错误");
    // Commit only after validation, so malformed reloads leave the active plan intact.
    m_running=false;m_profile=index;m_visual=visual;m_points=points;m_recipes=recipes;m_lookup=lookup;m_steps=steps;
    m_model.initialize(rows);m_format=qMin(m_format,formats-1);applyRecipe(m_flavor,m_format);reset();
    m_loadMessage=QString("%1 · %2 配方 / %3 点位").arg(index?"扩展配置已加载":"原始配置已加载").arg(recipes.size()).arg(points.size());
    addLog(m_loadMessage);emit configChanged();emit recipeChanged();return true;
}

QStringList TwinController::flavorLabels() const{QStringList labels;for(const auto &f:m_visual.value("flavors").toArray())labels<<f.toObject().value("label").toString();return labels;}
QStringList TwinController::formatLabels() const{QStringList labels;auto formats=m_visual.value("formats").toArray();for(int i=0;i<(m_profile?3:2);++i)labels<<formats.at(i).toObject().value("label").toString();return labels;}
bool TwinController::applyRecipe(int f,int s){
    if(f<0||f>=m_lookup.size()||s<0||s>=m_lookup.at(f).toArray().size())return false;
    m_flavor=f;m_format=s;m_recipe=m_recipes.at(m_lookup.at(f).toArray().at(s).toInt()).toObject().toVariantMap();return true;
}
bool TwinController::selectRecipe(int f,int s){
    if(!applyRecipe(f,s)){addLog("当前配置不支持该口味 / 规格");return false;}
    m_selected=activeFillDevice();reset();emit recipeChanged();
    addLog(QString("L4 入口 %1：run_recipe(%2, %3)，容量 %4 mL，设定 %5 ms").arg(m_recipe.value("id").toString()).arg(f).arg(s).arg(volumeMl()).arg(m_recipe.value("fill_time_ms").toInt()));return true;
}
QString TwinController::recipeCaption() const{return flavorLabels().value(m_flavor)+" · "+formatLabels().value(m_format);}
QString TwinController::flavorColor() const{return m_visual.value("flavors").toArray().at(m_flavor).toObject().value("color").toString();}
QVariantMap TwinController::containerShape() const{return m_visual.value("formats").toArray().at(m_format).toObject().toVariantMap();}
QVariantMap TwinController::scenePoint(int index) const{
    const auto point=m_points.at(index).toObject();auto result=point.toVariantMap();result["index"]=index;
    result["sx"]=m_visual.value("x_pr").toObject().value(QString::number(point.value("x").toInt())).toDouble();
    result["sz"]=m_visual.value("y_pr").toObject().value(QString::number(point.value("y").toInt())).toDouble();
    result["lift"]=m_visual.value("z_pr").toObject().value(QString::number(point.value("z").toInt())).toDouble();
    result["prText"]=QString("X PR%1 · Y PR%2 · Z PR%3").arg(point.value("x").toInt()).arg(point.value("y").toInt()).arg(point.value("z").toInt());return result;
}
QVariantList TwinController::pointCards() const{
    QVariantList cards;
    for(const auto &stage:QStringList{"empty","fill","finished"}){auto p=scenePoint(m_recipe.value(stage+"_work").toInt());p["label"]=stage=="empty"?"上瓶 / 夹取":stage=="fill"?"共用灌装口":"成品放置";cards<<p;}
    return cards;
}
QVariantList TwinController::flavorStations() const{
    QVariantList rows;const auto flavors=m_visual.value("flavors").toArray();
    for(int f=0;f<3;++f){auto r=flavors.at(f).toObject().toVariantMap();auto recipe=m_recipes.at(m_lookup.at(f).toArray().at(0).toInt()).toObject();auto p=scenePoint(recipe.value("fill_work").toInt());r["sx"]=p.value("sx");rows<<r;}return rows;
}
QVariantList TwinController::formatStations() const{
    QVariantList rows;
    for(int s=0;s<(m_profile?3:2);++s){auto r=m_visual.value("formats").toArray().at(s).toObject().toVariantMap();auto recipe=m_recipes.at(m_lookup.at(0).toArray().at(s).toInt()).toObject();r["formatIndex"]=s;r["empty"]=scenePoint(recipe.value("empty_work").toInt());r["finished"]=scenePoint(recipe.value("finished_work").toInt());rows<<r;}return rows;
}
QVariantList TwinController::routeSegments() const{
    QVariantList result;QVariantMap previous;
    for(const auto &stage:QStringList{"empty","rinse","fill","closure","code","finished"}){
        auto p=scenePoint(m_recipe.value(stage+"_work").toInt());
        if(!previous.isEmpty()){double dx=p.value("sx").toDouble()-previous.value("sx").toDouble(),dz=p.value("sz").toDouble()-previous.value("sz").toDouble();result<<QVariantMap{{"sx",(p.value("sx").toDouble()+previous.value("sx").toDouble())/2},{"sz",(p.value("sz").toDouble()+previous.value("sz").toDouble())/2},{"length",std::hypot(dx,dz)},{"angle",-qRadiansToDegrees(std::atan2(dz,dx))}};}
        previous=p;
    }
    return result;
}
int TwinController::currentPointIndex(double fraction) const{
    auto stage=m_steps.value(m_step).toMap().value("route").toString();
    if(stage=="home")return m_recipe.value("home_point").toInt();
    bool traversing=m_step==1||m_step==3||m_step==4||m_step==7||m_step==8||m_step==9;
    return m_recipe.value(stage+((traversing&&(fraction<0?progress():fraction)<.4)?"_entry":"_work")).toInt();
}
QString TwinController::productionPoint() const{if(serialDriven())return QString("外部阶段 %1 · X/Y1/Y2/Z 独立 PR 指令").arg(currentStep()+1);auto p=scenePoint(currentPointIndex());return QString("点 %1  ·  %2").arg(p.value("index").toInt()).arg(p.value("prText").toString());}
double TwinController::duration() const{return m_step==5?qMax(3.,m_recipe.value("fill_time_ms").toDouble()/1000.):qMax(.1,m_steps.value(m_step).toMap().value("duration").toDouble());}
double TwinController::progress() const{if(serialDriven())return m_plant->stageProgress();return qBound(0.,m_elapsed/duration(),1.);}
int TwinController::totalUnits() const{return m_recipe.value("batches").toInt()*m_recipe.value("units_per_batch").toInt();}
QString TwinController::batchCaption() const{
    if(serialDriven())return "单串口 / 外部主站控制";
    if(m_preview)return "阶段预览 · 不计入配方产量";
    int per=qMax(1,m_recipe.value("units_per_batch").toInt()),batches=m_recipe.value("batches").toInt();
    if(m_completed>=totalUnits())return QString("已完成 %1 批 × %2 件").arg(batches).arg(per);
    return QString("第 %1/%2 批 · 第 %3/%4 件").arg(m_completed/per+1).arg(batches).arg(m_completed%per+1).arg(per);
}
QString TwinController::currentStepName() const{QString name=m_steps.value(currentStep()).toMap().value("label").toString();if(currentStep()==7)name+=m_recipe.value("closure_mode").toString()=="seam_can"?" / 卷封":" / 旋盖";return name;}
QString TwinController::modeText() const{if(serialDriven())return m_plant->status();if(!m_fault.isEmpty())return "故障暂停";if(m_ended)return "本配方完成";return m_running?"模拟运行中":"模拟已暂停";}
void TwinController::addLog(const QString &s){m_logs.prepend(QTime::currentTime().toString("HH:mm:ss")+"  "+s);while(m_logs.size()>80)m_logs.removeLast();emit logChanged();}
void TwinController::selectDevice(const QString &id){if(m_model.device(id).isEmpty()||m_selected==id)return;m_selected=id;emit stateChanged();}
void TwinController::start(){if(serialDriven())return;if(!m_fault.isEmpty()||!m_configError.isEmpty())return;if(m_ended){reset();}if(m_running)return;m_running=true;addLog("开始模拟 · "+recipeCaption()+" · "+currentStepName());publish();}
void TwinController::pause(){if(serialDriven())return;if(!m_running)return;m_running=false;addLog("暂停，保持容器与机械位置");publish();}
void TwinController::reset(){if(serialDriven())return;
    m_running=false;m_ended=false;m_preview=false;m_fault.clear();m_faultDevice.clear();m_faultKey.clear();m_step=0;m_completed=0;
    m_elapsed=m_clock=m_historyClock=m_belt=m_fill=m_capAngle=m_pressure=m_torque=0;
    auto home=scenePoint(m_recipe.value("home_point").toInt());m_x=home.value("sx").toDouble();m_y=home.value("sz").toDouble();m_z=home.value("lift").toDouble();
    m_filled=m_capped=m_discharged=false;m_history.clear();m_logs.clear();addLog("模拟就绪 · "+recipeCaption()+" · 计划 "+QString::number(totalUnits())+" 件");publish();
}
void TwinController::setSpeed(double s){s=qBound(.25,s,4.);if(qFuzzyCompare(m_speed,s))return;m_speed=s;emit stateChanged();}
void TwinController::setLoop(bool b){if(m_loop!=b){m_loop=b;emit stateChanged();}}
void TwinController::enterStep(int i){m_step=i;m_elapsed=0;if(i==0||i==1){m_fill=0;m_filled=m_capped=m_discharged=false;}addLog(QString("阶段 %1/12 · %2").arg(i+1).arg(currentStepName()));}
void TwinController::finishStep(){
    if(m_step==5){m_fill=1;m_filled=true;}
    if(m_step==7&&m_filled)m_capped=true;
    if(m_step==9){
        if(!m_preview&&m_filled&&m_capped&&!m_discharged){++m_completed;m_discharged=true;addLog(QString("成品 %1/%2：%3").arg(m_completed).arg(totalUnits()).arg(recipeCaption()));}
        if(!m_preview&&m_completed<totalUnits()){enterStep(1);return;}
    }
    if(m_step==11){
        if(m_loop){m_completed=0;m_preview=false;enterStep(0);}else{m_running=false;m_ended=true;m_elapsed=duration();addLog("本配方结束：CIP 与回零完成");}
    }else enterStep(m_step+1);
}
void TwinController::nextStep(){if(serialDriven())return;if(!m_fault.isEmpty()||!m_configError.isEmpty()||m_ended)return;simulate(qMax(0.,duration()-m_elapsed));m_elapsed=duration();finishStep();publish();}
void TwinController::jumpToStep(int i){if(serialDriven())return;if(i<0||i>=12||!m_fault.isEmpty())return;m_running=false;m_ended=false;m_preview=true;m_filled=m_capped=m_discharged=false;m_fill=0;enterStep(i);publish();}
void TwinController::injectFault(const QString &key){if(serialDriven())return;
    if(!m_fault.isEmpty())return;
    if(key=="offline"){m_fault="流量计离线：当前值无效，最后有效值冻结";m_faultDevice="FlowMeter";}
    else if(key=="axis"){m_fault="Y2 驱动器报警，双 Y 轴暂停";m_faultDevice="Y2";}
    else if(key=="bottle"){m_fault="空容器到位等待超时";m_faultDevice="ContainerSensor";}
    else return;
    m_faultKey=key;m_running=false;m_selected=m_faultDevice;addLog("故障注入 · "+m_fault);publish();
}
void TwinController::clearFault(){if(serialDriven())return;if(m_fault.isEmpty())return;addLog("故障清除，请手动继续");m_fault.clear();m_faultDevice.clear();m_faultKey.clear();publish();}
void TwinController::simulate(double dt){
    auto target=scenePoint(currentPointIndex(qBound(0.,(m_elapsed+dt)/duration(),1.)));
    double f=1-std::exp(-dt*2.5);m_x+=(target.value("sx").toDouble()-m_x)*f;m_y+=(target.value("sz").toDouble()-m_y)*f;m_z+=(target.value("lift").toDouble()-m_z)*f;
    if(m_step==1||m_step==9)m_belt=std::fmod(m_belt+dt*.8,1.2);
    if(m_step==5)m_fill=qBound(0.,(m_elapsed+dt)/duration(),1.);
    m_pressure=m_step==10?.35:m_step==3||m_step==5?.22+.01*std::sin(m_clock*2):0;
    m_torque=m_step==7?2.2+.06*std::sin(m_clock*5):0;
    if(m_step==7)m_capAngle=std::fmod(m_capAngle+dt*240,360.);
}
void TwinController::advance(double dt){
    if(dt<=0||!qIsFinite(dt))return;
    if(serialDriven()){m_historyClock+=dt;return;}
    m_clock+=dt;
    if(!serialDriven()&&m_running&&m_fault.isEmpty()){
        double left=dt*m_speed;
        while(left>1e-8&&m_running){double slice=qMin(left,qMax(0.,duration()-m_elapsed));simulate(slice);m_elapsed+=slice;left-=slice;if(m_elapsed+1e-8>=duration())finishStep();else break;}
    }
    m_historyClock+=dt;publish();
}
bool TwinController::applySerialRecipe(int profile,int flavor,int format){
    if(profile<0||profile>1||flavor<0||flavor>2||format<0||format>(profile?2:1))return false;
    if(profile==1&&!loadSharedCandidate())return false;
    if(profile!=m_profile&&!loadProfile(profile))return false;
    return selectRecipe(flavor,format);
}
void TwinController::publish(){
    if(serialDriven()){
        const bool record=m_historyClock>=.2;if(record)m_historyClock=0;
        for(int i=0;i<m_model.rowCount();++i){auto row=m_model.row(i);QString id=row.value("deviceId").toString();auto state=m_plant->snapshot(id);
            if(!state.value("value").isValid())state["lastValid"]=row.value("lastValid","—");
            for(auto it=state.begin();it!=state.end();++it)row[it.key()]=it.value();
            if(record){auto &history=m_history[id];history.append(state.value("value"));while(history.size()>64)history.removeFirst();}
            m_model.updateRow(i,row);
        }
        ++m_revision;emit stateChanged();return;
    }
    auto active=m_steps.value(m_step).toMap().value("active").toList();
    for(auto &value:active)if(value.toString()=="${fill_device}")value=activeFillDevice();
    const bool record=m_historyClock>=.2;if(record)m_historyClock=0;
    for(int i=0;i<m_model.rowCount();++i){
        auto r=m_model.row(i);QString id=r.value("deviceId").toString();const bool isFault=id==m_faultDevice;
        bool on=active.contains(id)&&m_running&&m_fault.isEmpty();QString kind=on?"running":"idle",status=on?"运行中":"待机";
        double value=0;bool numeric=true;QString text;r.remove("target");
        if(id=="X"||id=="Y1"||id=="Y2"||id=="Z"){
            value=(id=="X"?m_x:id=="Z"?m_z:m_y)*100.;text=QString::number(value,'f',1);auto p=m_points.at(currentPointIndex()).toObject();QString axis=id.startsWith("Y")?"y":id.toLower();r["target"]=QString("当前目标 PR%1（%2）").arg(p.value(axis).toInt()).arg(id);
        }else if(id=="Infeed"||id=="Closure"||id=="Outfeed"){
            numeric=false;QString key=id=="Infeed"?"infeed_pr":id=="Closure"?"closure_pr":"outfeed_pr";text=QString("PR%1 · %2").arg(m_recipe.value(key).toInt()).arg(on?"动作中":"待机");r["target"]=id=="Closure"?m_recipe.value("closure_mode").toString():"规格参数来自当前配方";
        }else if(id=="FlowMeter"||id.endsWith("Fill")){
            value=(id=="FlowMeter"||id==activeFillDevice())?m_fill*volumeMl():0;text=QString::number(value,'f',1);r["target"]=id=="FlowMeter"||id==activeFillDevice()?QString("目标 %1 mL / 设定 %2 ms").arg(volumeMl()).arg(m_recipe.value("fill_time_ms").toInt()):"未被当前口味选用 · 灌装头仍为共用设备";
        }else if(id=="PressureSensor"){value=m_pressure;text=QString::number(value,'f',3);}
        else if(id=="Torque"){value=m_torque;text=QString::number(value,'f',2);}
        else if(id=="LevelSensor"){numeric=false;value=m_fill>=.98?1:0;text=value?"已到位":"未到位";}
        else if(id=="BottleSensor"||id=="ContainerSensor"){numeric=false;value=hasContainer()&&m_faultKey!="bottle"?1:0;text=value?"容器已到位":"未检测到容器";}
        else if(id=="Gripper"){numeric=false;value=hasContainer()&&!(m_step==9&&progress()>.75)?1:0;text=value?"夹持中":"已松开";}
        else if(id=="CapFeeder"){numeric=false;value=on?1:0;text=on?"供盖开启":"供盖关闭";}
        else if(id=="Relay"){numeric=false;text=m_step==7&&on?"封口输出 ON":m_step==8&&on?"喷码 / 贴标 ON":m_step==10&&on?"清洗输出 ON":"输出 OFF";}
        else {numeric=false;bool opened=(id=="RinseValve"&&rinseOn())||(id=="CIPValve"&&cipOn());value=opened?1:0;text=opened?"开启指令":"关闭指令";status=text;kind=opened?"running":"idle";}
        bool stale=isFault&&m_faultKey=="offline";
        if(isFault){kind="fault";status=stale?"离线 / 超时":"设备异常";}
        if(stale){text="—";r["ageMs"]=qRound((m_clock-r.value("sampleTime",m_clock).toDouble())*1000);r["quality"]="Bad · 无效";}
        else {r["ageMs"]=0;r["sampleTime"]=m_clock;r["quality"]="Simulated · 模拟";r["lastValid"]=text+(r.value("unit").toString().isEmpty()?"":" "+r.value("unit").toString());}
        r["statusKind"]=kind;r["statusText"]=status;r["valueText"]=text;r["value"]=stale?QVariant():QVariant(value);r["numeric"]=numeric;r["rawText"]=stale?"无有效响应":"模拟量 / 未发送串口报文";
        if(record){auto &h=m_history[id];h.append(stale?QVariant():QVariant(value));while(h.size()>64)h.removeFirst();}
        m_model.updateRow(i,r);
    }
    ++m_revision;emit stateChanged();
}

bool TwinController::selfTest(QString *error){
    auto check=[&](bool ok,const QString &message){if(!ok&&error)*error=message;return ok;};
    TwinController t(nullptr,false);
    if(!check(t.configError().isEmpty(),t.configError())||!check(t.recipeCount()==6&&t.deviceCount()==21,"原版应为 21 设备 / 6 配方")||!check(!t.selectRecipe(0,2),"原版必须拒绝第三规格"))return false;
    QVariantList oldPoints;
    for(int f=0;f<3;++f)for(int s=0;s<2;++s){if(!check(t.selectRecipe(f,s),"原配方选择失败"))return false;oldPoints<<QVariant(t.pointCards());}
    if(!check(t.loadProfile(1)&&t.recipeCount()==9&&t.deviceCount()==21,"扩展应复用 21 设备、追加到 9 配方"))return false;
    int n=0;
    for(int f=0;f<3;++f){
        QString head;
        for(int s=0;s<3;++s){
            if(!check(t.selectRecipe(f,s),"扩展配方选择失败"))return false;
            if(s==0)head=t.activeFillDevice();
            if(!check(head==t.activeFillDevice(),"新增包装必须复用同一口味灌装头"))return false;
            if(s<2&&!check(t.pointCards()==oldPoints[n++].toList(),"原有点位发生变化"))return false;
            if(s==2&&!check(t.pointCards()[0].toMap().value("index").toInt()==34&&t.pointCards()[2].toMap().value("index").toInt()==44,"小瓶上瓶 / 成品点错误"))return false;
            t.setLoop(false);
            int visits[12]={};
            for(int count=0;count<200&&!t.m_ended;++count){++visits[t.currentStep()];t.nextStep();}
            if(!check(t.completed()==12&&t.m_ended&&t.currentStep()==11&&visits[0]==1&&visits[9]==12&&visits[10]==1&&visits[11]==1,"每配方应生产两批六件、结束后仅清洗回零一次"))return false;
            for(int stage=1;stage<10;++stage)if(!check(visits[stage]==12,"每件容器必须完成全部九个加工阶段"))return false;
        }
    }
    t.selectRecipe(1,2);t.jumpToStep(5);t.start();t.advance(.5);double fill=t.fillFraction();auto last=t.device("FlowMeter").value("lastValid");
    t.injectFault("offline");t.advance(4);
    if(!check(!t.running()&&t.fillFraction()==fill&&t.device("FlowMeter").value("lastValid")==last&&t.device("FlowMeter").value("quality").toString().startsWith("Bad"),"离线值冻结错误"))return false;
    t.clearFault();if(!check(!t.running(),"清除故障不得自动运行"))return false;
    t.pause();double x=t.axisX();t.advance(1);if(!check(t.axisX()==x,"暂停机械位置变化"))return false;
    t.jumpToStep(9);t.nextStep();if(!check(t.completed()==0,"阶段预览不能增加成品数量"))return false;
    t.injectFault("axis");if(!check(t.selectedId()=="Y2"&&t.device("Y2").value("value").isValid(),"轴报警不得伪装成通信中断"))return false;
    t.clearFault();t.injectFault("bottle");if(!check(t.selectedId()=="ContainerSensor","空容器故障映射错误"))return false;
    t.clearFault();
    if(!check(!t.loadProfile(7)&&t.profileIndex()==1&&t.recipeCount()==9,"无效配置必须保留当前配方集"))return false;
    if(!check(t.loadProfile(0)&&t.formatLabels().size()==2&&t.recipeCount()==6,"应支持切回原始两规格"))return false;
    t.setLoop(false);t.start();t.advance(10000);
    if(!check(t.completed()==12&&t.m_ended&&!t.running(),"长时间步进必须在配方结束时停止"))return false;

    // Exercise real file reload failures, not only an out-of-range profile index.
    // The running model, selected recipe and pose must survive a broken edit.
    QTemporaryDir temporary;
    if(!check(temporary.isValid(),"无法创建配置回归测试临时目录"))return false;
    QDirIterator files(t.m_configRoot,QStringList{"*.json"},QDir::Files,QDirIterator::Subdirectories);
    while(files.hasNext()){
        const QString source=files.next();
        const QString relative=QDir(t.m_configRoot).relativeFilePath(source);
        const QString destination=temporary.path()+"/"+relative;
        QDir().mkpath(QFileInfo(destination).path());
        if(!check(QFile::copy(source,destination),"配置回归测试复制失败："+relative))return false;
    }
    TwinController reload(nullptr,false,temporary.path());
    if(!check(reload.configError().isEmpty(),reload.configError()))return false;
    reload.selectRecipe(2,1);reload.start();reload.advance(7);
    const auto savedRecipe=reload.recipe();const int savedStep=reload.currentStep();const double savedX=reload.axisX(),savedProgress=reload.progress();
    auto rejectedEdit=[&](const QString &relative,const std::function<void(QJsonObject&)> &edit){
        const QString path=temporary.path()+"/"+relative;
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;const QByteArray original=file.readAll();file.close();
        auto object=QJsonDocument::fromJson(original).object();edit(object);
        if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
        file.write(QJsonDocument(object).toJson());file.close();
        const bool rejected=!reload.reloadConfig()&&reload.running()&&reload.recipe()==savedRecipe&&reload.currentStep()==savedStep&&reload.axisX()==savedX&&reload.progress()==savedProgress&&reload.deviceCount()==21;
        if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
        file.write(original);file.close();return rejected;
    };
    if(!check(rejectedEdit("recipes/baseline/system_variables.json",[](QJsonObject &object){auto recipes=object["recipes"].toArray();auto first=recipes[0].toObject();first["home_point"]=999;recipes[0]=first;object["recipes"]=recipes;}),"无效回零点重载必须保留运行状态"))return false;
    if(!check(rejectedEdit("recipes/baseline/layered/L3_group/produce_batch.group.json",[](QJsonObject &object){auto loop=object["loop"].toObject();loop["count"]=5;object["loop"]=loop;}),"L3 批次数与配方不一致必须拒绝"))return false;
    if(!check(rejectedEdit("recipes/baseline/layered/L3_group/run_recipe.group.json",[](QJsonObject &object){object["args"]=QJsonArray();}),"缺失 L3 两参数定义必须拒绝"))return false;
    if(!check(rejectedEdit("recipes/baseline/layered/L3_group/run_recipe.group.json",[](QJsonObject &object){
        auto body=object["body"].toArray();
        for(int i=0;i<body.size();++i){auto group=body[i].toObject();if(group.value("mode").toString()=="loop"){
            auto loopBody=group["body"].toArray();loopBody.append(body[i+1]);group["body"]=loopBody;body[i]=group;body.removeAt(i+1);break;
        }}
        object["body"]=body;
    }),"CIP 放入批量循环时必须拒绝"))return false;
    if(!check(rejectedEdit("demo_profile.json",[](QJsonObject &object){auto devices=object["devices"].toArray();devices[1]=devices[0];object["devices"]=devices;}),"重复设备显示绑定必须拒绝"))return false;
    if(!check(rejectedEdit("visualization.json",[](QJsonObject &object){auto axis=object["x_pr"].toObject();axis["4"]="invalid";object["x_pr"]=axis;}),"非数值 PR 空间标定必须拒绝"))return false;
    if(error)error->clear();
    return true;
}
