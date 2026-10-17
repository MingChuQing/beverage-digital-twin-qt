#include "aicontroller.h"
#include "twincontroller.h"
#include "serialmaster.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSettings>
#include <QDesktopServices>
#include <QTime>
#include <QUrl>

AiController::AiController(TwinController *twin,SerialMaster *master,QObject *parent):QObject(parent),m_twin(twin),m_master(master){
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&AiController::consume);
    connect(&m_process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        consume();m_process.readAllStandardError();
        if(code!=0&&m_status!="已取消；候选未应用") {m_status="本次流程停止："+m_status;log(m_status);}
        emit changed();
    });
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){m_status="无法启动验证运行时，请重新运行部署脚本";log(m_status);emit changed();}
    });
}
AiController::~AiController(){if(busy()){m_process.kill();m_process.waitForFinished(3000);}}
QString AiController::defaultModel() const{return QSettings().value("ai/model","deepseek-flash").toString();}
void AiController::log(const QString &text){m_logs.append(QTime::currentTime().toString("HH:mm:ss")+"  "+text);while(m_logs.size()>150)m_logs.removeFirst();}
void AiController::start(QJsonObject request){
    if(busy())return;
    const QString app=QCoreApplication::applicationDirPath();
    const QString python=app+"/runtime/python/python.exe";
    if(!QFile::exists(python)||!QFile::exists(app+"/ai/pipeline.py")){m_status="缺少 AI 运行时，请先运行 build_demo.ps1 -Deploy";log(m_status);emit changed();return;}
    m_buffer.clear();m_process.setProgram(python);m_process.setArguments({"-u",app+"/ai/pipeline.py"});
    m_process.setWorkingDirectory(app);m_process.start();
    // QProcess buffers stdin until started. Secrets never enter command arguments or QSettings.
    m_process.write(QJsonDocument(request).toJson(QJsonDocument::Compact)+"\n");m_process.closeWriteChannel();emit changed();
}
void AiController::generate(const QString &prompt,const QString &key,const QString &model,int rounds){
    if(busy()||m_master->busy())return;
    QString secret=key.trimmed();if(secret.isEmpty())secret=qEnvironmentVariable("DEEPSEEK_API_KEY");
    m_ready=false;m_applied=false;m_round=0;m_gate=0;m_raw.clear();m_diff.clear();m_summary.clear();m_report.clear();m_logs.clear();
    m_status="正在准备 DeepSeek 请求";
    QSettings().setValue("ai/model",model.trimmed());
    start({{"action","generate"},{"prompt",prompt},{"key",secret},{"model",model.trimmed()},{"max_rounds",rounds},
           {"sessions",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ai-sessions"}});
}
void AiController::cancel(){if(busy()){m_status="已取消；候选未应用";m_ready=false;m_process.kill();log(m_status);emit changed();}}
void AiController::approve(){if(m_ready&&!busy()&&!m_master->busy())start({{"action","approve"},{"report",m_report},{"test_only",m_automatedTest}});}
void AiController::replay(bool excerpt){
    if(!m_applied||busy()||m_master->busy())return;
    if(!m_master->connected()){m_status="请在串口联调页打开 COM3 发送串口";emit changed();return;}
    start({{"action","replay"},{"report",m_report},{"flavor",m_twin->flavorIndex()},{"spec",m_twin->formatIndex()},{"excerpt",excerpt},{"test_only",m_automatedTest}});
}
void AiController::previewReport(const QString &report){if(!busy())start({{"action","preview"},{"report",report}});}
void AiController::showRecords(){if(!m_directory.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(m_directory));}
void AiController::consume(){
    m_buffer+=m_process.readAllStandardOutput();
    while(m_buffer.contains('\n')){const int end=m_buffer.indexOf('\n');auto bytes=m_buffer.left(end);m_buffer.remove(0,end+1);auto value=QJsonDocument::fromJson(bytes);if(value.isObject())handle(value.object());}
}
void AiController::handle(const QJsonObject &value){
    const QString kind=value.value("kind").toString(),message=value.value("message").toString();
    m_status=message;log(message);
    if(kind=="session")m_directory=value.value("directory").toString();
    if(kind=="round"){m_round=value.value("round").toInt();m_gate=0;}
    if(kind=="gate")m_gate=qMax(m_gate,value.value("gate").toInt());
    if(kind=="raw")m_raw=value.value("raw").toString();
    if(kind=="ready"){
        m_report=value.value("report").toString();m_directory=QFileInfo(m_report).absolutePath();m_diff=value.value("diff").toString();m_summary=value.value("summary").toString();m_ready=true;m_applied=false;m_gate=2;emit candidateReady();
    }
    if(kind=="applied"){
        if(m_twin->loadCandidateDirectory(value.value("candidate").toString())){
            QString activationError;
            if(m_twin->publishCandidateActivation(value.value("report").toString(),m_automatedTest,activationError)){
                m_twin->selectRecipe(2,2);m_applied=true;m_gate=3;emit candidateApplied();
            }else {m_applied=false;m_status="配置激活失败："+activationError;log(m_status);}
        }else {m_applied=false;m_status="候选展示配置加载失败："+m_twin->loadMessage();log(m_status);}
    }
    if(kind=="replay"){
        if(m_master->startValidatedTrace(value.value("jobs").toArray())){m_gate=4;emit playbackStarted();}
        else {m_status="串口未就绪或轨迹不受支持";log(m_status);}
    }
    if(kind=="error")m_ready=false;
    emit changed();
}
