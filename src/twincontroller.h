#pragma once
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QVariantList>
#include "serialplant.h"

class DeviceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {DeviceId=Qt::UserRole+1,DisplayName,Category,StatusText,StatusKind,ValueText,Unit,Quality};
    explicit DeviceModel(QObject *parent=nullptr):QAbstractListModel(parent){}
    int rowCount(const QModelIndex &p=QModelIndex()) const override{return p.isValid()?0:m_rows.size();}
    QVariant data(const QModelIndex &,int) const override;
    QHash<int,QByteArray> roleNames() const override;
    void initialize(const QVariantList &rows);
    void updateRow(int,const QVariantMap &);
    QVariantMap device(const QString &) const;
    QVariantMap row(int i) const{return m_rows.value(i).toMap();}
private:
    QVariantList m_rows;
};

class TwinController : public QObject {
    Q_OBJECT
    Q_PROPERTY(DeviceModel* deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY configChanged)
    Q_PROPERTY(QVariantMap selectedDevice READ selectedDevice NOTIFY stateChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY stateChanged)
    Q_PROPERTY(int currentStep READ currentStep NOTIFY stateChanged)
    Q_PROPERTY(QString currentStepName READ currentStepName NOTIFY stateChanged)
    Q_PROPERTY(QString stepNote READ stepNote NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY stateChanged)
    Q_PROPERTY(int completed READ completed NOTIFY stateChanged)
    Q_PROPERTY(QString fault READ fault NOTIFY stateChanged)
    Q_PROPERTY(QString modeText READ modeText NOTIFY stateChanged)
    Q_PROPERTY(QStringList eventLog READ eventLog NOTIFY logChanged)
    Q_PROPERTY(int revision READ revision NOTIFY stateChanged)
    Q_PROPERTY(double axisX READ axisX NOTIFY stateChanged)
    Q_PROPERTY(double axisY READ axisY NOTIFY stateChanged)
    Q_PROPERTY(double axisZ READ axisZ NOTIFY stateChanged)
    Q_PROPERTY(double bottleOffset READ bottleOffset NOTIFY stateChanged)
    Q_PROPERTY(double fillFraction READ fillFraction NOTIFY stateChanged)
    Q_PROPERTY(double capAngle READ capAngle NOTIFY stateChanged)
    Q_PROPERTY(bool fillingOn READ fillingOn NOTIFY stateChanged)
    Q_PROPERTY(bool rinseOn READ rinseOn NOTIFY stateChanged)
    Q_PROPERTY(bool cipOn READ cipOn NOTIFY stateChanged)
    Q_PROPERTY(bool conveyorOn READ conveyorOn NOTIFY stateChanged)
    Q_PROPERTY(bool hasContainer READ hasContainer NOTIFY stateChanged)
    Q_PROPERTY(bool containerCapped READ containerCapped NOTIFY stateChanged)
    Q_PROPERTY(QString productionPoint READ productionPoint NOTIFY stateChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY stateChanged)
    Q_PROPERTY(QString configError READ configError CONSTANT)
    Q_PROPERTY(int profileIndex READ profileIndex NOTIFY configChanged)
    Q_PROPERTY(int flavorIndex READ flavorIndex NOTIFY recipeChanged)
    Q_PROPERTY(int formatIndex READ formatIndex NOTIFY recipeChanged)
    Q_PROPERTY(int recipeCount READ recipeCount NOTIFY configChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY configChanged)
    Q_PROPERTY(QStringList flavorLabels READ flavorLabels NOTIFY configChanged)
    Q_PROPERTY(QStringList formatLabels READ formatLabels NOTIFY configChanged)
    Q_PROPERTY(QVariantMap recipe READ recipe NOTIFY recipeChanged)
    Q_PROPERTY(QString recipeCaption READ recipeCaption NOTIFY recipeChanged)
    Q_PROPERTY(QString activeFillDevice READ activeFillDevice NOTIFY recipeChanged)
    Q_PROPERTY(double volumeMl READ volumeMl NOTIFY stateChanged)
    Q_PROPERTY(QString flavorColor READ flavorColor NOTIFY recipeChanged)
    Q_PROPERTY(QVariantMap containerShape READ containerShape NOTIFY recipeChanged)
    Q_PROPERTY(QVariantList flavorStations READ flavorStations NOTIFY configChanged)
    Q_PROPERTY(QVariantList formatStations READ formatStations NOTIFY configChanged)
    Q_PROPERTY(QVariantList routeSegments READ routeSegments NOTIFY recipeChanged)
    Q_PROPERTY(QVariantList pointCards READ pointCards NOTIFY recipeChanged)
    Q_PROPERTY(QString configLocation READ configLocation NOTIFY configChanged)
    Q_PROPERTY(QString loadMessage READ loadMessage NOTIFY configChanged)
    Q_PROPERTY(QString batchCaption READ batchCaption NOTIFY stateChanged)
public:
    explicit TwinController(QObject *parent=nullptr,bool automaticTimer=true,const QString &configurationRoot=QString());
    DeviceModel *deviceModel(){return &m_model;}
    QVariantList steps() const{return m_steps;}
    QVariantMap selectedDevice() const{return m_model.device(m_selected);}
    QString selectedId() const{return m_selected;}
    bool running() const{return serialDriven()?m_plant->healthy()&&!m_plant->paused():m_running;}
    bool loop() const{return m_loop;}
    int currentStep() const{return serialDriven()?m_plant->stage():m_step;}
    QString currentStepName() const;
    QString stepNote() const{return serialDriven()?QStringLiteral("串口指令驱动 · 反馈为虚拟设备计算"):m_steps.value(m_step).toMap().value("note").toString();}
    double progress() const;
    double speed() const{return m_speed;}
    int completed() const{return serialDriven()?m_plant->completed():m_completed;}
    QString fault() const{return m_fault;}
    QString modeText() const;
    QStringList eventLog() const{return m_logs;}
    int revision() const{return m_revision;}
    double axisX() const{return serialDriven()?m_plant->axis("X"):m_x;}
    double axisY() const{return serialDriven()?(m_plant->axis("Y1")+m_plant->axis("Y2"))/2:m_y;}
    double axisZ() const{return serialDriven()?m_plant->axis("Z"):m_z;}
    double bottleOffset() const{return m_belt;}
    double fillFraction() const{return serialDriven()?m_plant->fillFraction():m_fill;}
    double capAngle() const{return serialDriven()?m_plant->capAngle():m_capAngle;}
    bool fillingOn() const{return serialDriven()?m_plant->output(activeFillDevice())&&fillFraction()<1:m_step==5&&m_running&&m_fault.isEmpty();}
    bool rinseOn() const{return serialDriven()?m_plant->output("RinseValve"):(m_step==3||m_step==10)&&m_running&&m_fault.isEmpty();}
    bool cipOn() const{return serialDriven()?m_plant->output("CIPValve"):m_step==10&&m_running&&m_fault.isEmpty();}
    bool conveyorOn() const{return serialDriven()?m_plant->snapshot("Infeed").value("statusKind")=="running"||m_plant->snapshot("Outfeed").value("statusKind")=="running":(m_step==1||m_step==9)&&m_running&&m_fault.isEmpty();}
    bool hasContainer() const{return serialDriven()?m_plant->hasContainer():(m_step>=2&&m_step<=9)||(m_step==1&&progress()>.5);}
    bool containerCapped() const{return serialDriven()?m_plant->capped():m_step>=8;}
    QString productionPoint() const;
    QVariantList history() const{return m_history.value(m_selected);}
    QString configError() const{return m_configError;}
    int profileIndex() const{return m_profile;}
    int flavorIndex() const{return m_flavor;}
    int formatIndex() const{return m_format;}
    int recipeCount() const{return m_recipes.size();}
    int deviceCount() const{return m_model.rowCount();}
    QStringList flavorLabels() const;
    QStringList formatLabels() const;
    QVariantMap recipe() const{return m_recipe;}
    QString recipeCaption() const;
    QString activeFillDevice() const{return m_recipe.value("fill_device").toString();}
    double volumeMl() const{return serialDriven()?m_plant->targetVolume():m_recipe.value("volume_ml").toDouble();}
    QString flavorColor() const;
    QVariantMap containerShape() const;
    QVariantList flavorStations() const;
    QVariantList formatStations() const;
    QVariantList routeSegments() const;
    QVariantList pointCards() const;
    QString configLocation() const{return m_profile&&!m_candidateDirectory.isEmpty()?m_candidateDirectory:m_configRoot+"/recipes/"+(m_profile?"extended":"baseline");}
    bool loadCandidateDirectory(const QString &);
    QString activationPath() const{return m_activationPath;}
    void setActivationPath(const QString &path,bool allowTest=false){m_activationPath=path;m_allowTestActivation=allowTest;}
    bool publishCandidateActivation(const QString &report,bool testing,QString &error);
    QString configurationRoot() const{return m_configRoot;}
    void attachPlant(SerialPlant *plant){
        m_plant=plant;
        connect(plant,&SerialPlant::changed,this,[this]{
            if(!serialDriven()){publish();return;}
            if(m_serialPublishPending)return;
            m_serialPublishPending=true;
            QTimer::singleShot(50,this,[this]{m_serialPublishPending=false;publish();});
        });
    }
    bool serialDriven() const{return m_plant&&m_plant->serialMode();}
    bool applySerialRecipe(int,int,int);
    QString loadMessage() const{return m_loadMessage;}
    QString batchCaption() const;
    Q_INVOKABLE QVariantMap device(const QString &id) const{return m_model.device(id);}
    Q_INVOKABLE bool loadProfile(int);
    Q_INVOKABLE bool selectRecipe(int flavor,int specification);
    Q_INVOKABLE bool reloadConfig(){return loadProfile(m_profile);}
    Q_INVOKABLE void selectDevice(const QString &);
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void nextStep();
    Q_INVOKABLE void jumpToStep(int);
    Q_INVOKABLE void injectFault(const QString &);
    Q_INVOKABLE void clearFault();
    void setSpeed(double);
    void setLoop(bool);
    void advance(double seconds);
    static bool selfTest(QString *error);
signals:
    void stateChanged();
    void logChanged();
    void configChanged();
    void recipeChanged();
private:
    QString m_candidateDirectory;
    QString m_activationPath;
    bool m_allowTestActivation=false;
    bool loadSharedCandidate();
    double duration() const;
    int totalUnits() const;
    int currentPointIndex(double fraction=-1) const;
    QVariantMap scenePoint(int) const;
    bool applyRecipe(int,int);
    void publish();
    void addLog(const QString &);
    void enterStep(int);
    void finishStep();
    void simulate(double);
    DeviceModel m_model;
    QVariantList m_steps;
    QJsonArray m_points,m_recipes,m_lookup;
    QJsonObject m_visual;
    SerialPlant *m_plant=nullptr;
    bool m_serialPublishPending=false;
    QVariantMap m_recipe;
    QTimer m_timer;
    QElapsedTimer m_wallClock;
    QString m_configRoot,m_loadMessage,m_configError;
    QString m_selected="OrangeFill",m_fault,m_faultDevice,m_faultKey;
    QStringList m_logs;
    QHash<QString,QVariantList> m_history;
    int m_profile=0,m_flavor=0,m_format=0,m_step=0,m_completed=0,m_revision=0;
    bool m_running=false,m_loop=true,m_ended=false,m_filled=false,m_capped=false,m_discharged=false,m_preview=false;
    double m_elapsed=0,m_speed=1,m_x=0,m_y=0,m_z=0,m_belt=0,m_fill=0,m_capAngle=0,m_clock=0,m_pressure=0,m_torque=0,m_historyClock=0;
};
