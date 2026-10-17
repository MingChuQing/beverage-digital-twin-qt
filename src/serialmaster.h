#pragma once
#include "serialplant.h"
#include <QQueue>

class SerialMaster : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString portName READ portName NOTIFY changed)
    Q_PROPERTY(int baudRate READ baudRate NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY portsChanged)
    Q_PROPERTY(QStringList deviceIds READ deviceIds CONSTANT)
    Q_PROPERTY(QStringList deviceLabels READ deviceLabels CONSTANT)
    Q_PROPERTY(QStringList frames READ frames NOTIFY framesChanged)
    Q_PROPERTY(int acknowledgements READ acknowledgements NOTIFY changed)
    Q_PROPERTY(int failures READ failures NOTIFY changed)
public:
    SerialMaster(TwinController *,const QJsonObject &,const QJsonObject &,bool automatic=true);
    bool connected() const{return m_port.isOpen()||m_test;}
    // Background health polling must not disable operator controls between replies.
    bool busy() const{return (m_pending&&!m_heartbeat)||!m_jobs.isEmpty()||m_demo;}
    QString portName() const{return m_portName;}
    int baudRate() const{return m_baud;}
    QString status() const;
    QStringList availablePorts() const{return Rtu::ports();}
    QStringList deviceIds() const{return m_ids;}
    QStringList deviceLabels() const{return m_labels;}
    QStringList frames() const{return m_frames;}
    int acknowledgements() const{return m_acks;}
    int failures() const{return m_failures;}
    Q_INVOKABLE bool openPort(const QString &,int);
    Q_INVOKABLE void closePort();
    Q_INVOKABLE void refreshPorts(){emit portsChanged();}
    Q_INVOKABLE bool send(int,int,const QString &,const QString &);
    Q_INVOKABLE void triggerPr(const QString &,int);
    Q_INVOKABLE void toggle(const QString &,bool);
    Q_INVOKABLE void applyRecipe();
    Q_INVOKABLE void startDemo();
    bool startValidatedTrace(const QJsonArray &);
    Q_INVOKABLE void stopDemo();
    Q_INVOKABLE int address(const QString &) const;
    void receiveBytes(const QByteArray &);
    void pump();
    void enableTestTransport(){m_test=true;}
signals:
    void changed();
    void framesChanged();
    void portsChanged();
    void requestReady(const QByteArray &);
    void completedDemo();
private:
    struct Task{QByteArray frame;int delay=0;};
    void log(const QString &);
    void enqueue(int,int,int,const QList<quint16> &,int delay=0);
    void registerWrite(const QString &,int,int,int delay=0);
    void coilWrite(const QString &,bool,int delay=0);
    void lineWrite(int,int,int delay=0);
    void dispatch(const Task &,bool heartbeat=false);
    void failed(const QString &);
    TwinController *m_twin;
    QJsonObject m_config,m_registry;
    QSerialPort m_port;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QQueue<Task> m_jobs;
    Task m_current;
    QByteArray m_buffer;
    QString m_portName,m_lastError;
    QString m_executionLabel="单件工艺发送";
    QStringList m_labels,m_ids,m_frames;
    bool m_logDirty=false;
    qint64 m_logNotifiedAt=-1000;
    int m_baud=115200,m_acks=0,m_failures=0,m_pollIndex=0;
    bool m_test=false,m_pending=false,m_heartbeat=false,m_demo=false;
    qint64 m_sentAt=0,m_readyAt=0,m_lastPoll=0;
};
