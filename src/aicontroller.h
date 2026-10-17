#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
class TwinController;
class SerialMaster;

class AiController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool applied READ applied NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString rawJson READ rawJson NOTIFY changed)
    Q_PROPERTY(QString diff READ diff NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString directory READ directory NOTIFY changed)
    Q_PROPERTY(QStringList logs READ logs NOTIFY changed)
    Q_PROPERTY(int round READ round NOTIFY changed)
    Q_PROPERTY(int gate READ gate NOTIFY changed)
    Q_PROPERTY(QString defaultModel READ defaultModel CONSTANT)
    Q_PROPERTY(bool hasEnvironmentKey READ hasEnvironmentKey CONSTANT)
public:
    AiController(TwinController *,SerialMaster *,QObject *parent=nullptr);
    ~AiController() override;
    bool busy() const{return m_process.state()!=QProcess::NotRunning;}
    bool ready() const{return m_ready;}
    bool applied() const{return m_applied;}
    QString status() const{return m_status;}
    QString rawJson() const{return m_raw;}
    QString diff() const{return m_diff;}
    QString summary() const{return m_summary;}
    QString directory() const{return m_directory;}
    QStringList logs() const{return m_logs;}
    int round() const{return m_round;}
    int gate() const{return m_gate;}
    QString defaultModel() const;
    bool hasEnvironmentKey() const{return !qEnvironmentVariableIsEmpty("DEEPSEEK_API_KEY");}
    Q_INVOKABLE void generate(const QString &prompt,const QString &key,const QString &model,int rounds);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void approve();
    Q_INVOKABLE void replay(bool excerpt);
    Q_INVOKABLE void showRecords();
    void previewReport(const QString &);
    void enableAutomatedTest(){m_automatedTest=true;}
signals:
    void changed();
    void candidateReady();
    void candidateApplied();
    void playbackStarted();
private:
    void start(QJsonObject);
    void consume();
    void handle(const QJsonObject &);
    void log(const QString &);
    TwinController *m_twin;
    SerialMaster *m_master;
    QProcess m_process;
    QByteArray m_buffer;
    QString m_status="输入自然语言，开始 DeepSeek 配置生成",m_raw,m_diff,m_summary,m_directory,m_report;
    QStringList m_logs;
    bool m_ready=false,m_applied=false;
    bool m_automatedTest=false;
    int m_round=0,m_gate=0;
};
