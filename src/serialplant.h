#pragma once
#include <QObject>
#include <QSerialPort>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>

class TwinController;
namespace Rtu {
quint16 crc(const QByteArray &);
QByteArray seal(const QByteArray &);
bool valid(const QByteArray &);
quint16 word(const QByteArray &,int);
QByteArray words(const QList<quint16> &);
QStringList ports();
bool open(QSerialPort &,const QString &,int,const QJsonObject &,QString &);
QJsonObject object(const QString &,QString &);
}

class SerialPlant : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool serialMode READ serialMode WRITE setSerialMode NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString portName READ portName NOTIFY changed)
    Q_PROPERTY(int baudRate READ baudRate NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY portsChanged)
    Q_PROPERTY(QStringList frames READ frames NOTIFY framesChanged)
    Q_PROPERTY(int rxCount READ rxCount NOTIFY changed)
    Q_PROPERTY(int txCount READ txCount NOTIFY changed)
    Q_PROPERTY(int crcErrors READ crcErrors NOTIFY changed)
    Q_PROPERTY(int exceptions READ exceptions NOTIFY changed)
    Q_PROPERTY(double targetVolume READ targetVolume NOTIFY changed)
    Q_PROPERTY(int targetTime READ targetTime NOTIFY changed)
public:
    SerialPlant(TwinController *,const QString &,bool automatic=true);
    bool serialMode() const{return m_serialMode;}
    bool connected() const{return m_port.isOpen()||m_test;}
    QString portName() const{return m_portName;}
    int baudRate() const{return m_baud;}
    QString status() const;
    QStringList availablePorts() const{return Rtu::ports();}
    QStringList frames() const{return m_frames;}
    int rxCount() const{return m_rxCount;}
    int txCount() const{return m_txCount;}
    int crcErrors() const{return m_crcErrors;}
    int exceptions() const{return m_exceptions;}
    QString configError() const{return m_error;}
    QJsonObject protocol() const{return m_config;}
    QJsonObject registry() const{return m_registry;}
    Q_INVOKABLE bool openPort(const QString &,int);
    Q_INVOKABLE void closePort();
    Q_INVOKABLE void refreshPorts(){emit portsChanged();}
    Q_INVOKABLE void setSerialMode(bool);
    Q_INVOKABLE void launchSender();
    double targetVolume() const;
    int targetTime() const;
    bool capped() const{return m_capped;}
    bool healthy() const;
    bool paused() const{return m_paused;}
    double axis(const QString &) const;
    double fillFraction() const;
    bool output(const QString &) const;
    bool hasContainer() const;
    double capAngle() const{return m_capAngle;}
    int stage() const{return m_control[3];}
    int completed() const{return m_control[5];}
    double stageProgress() const;
    QVariantMap snapshot(const QString &) const;
    int address(const QString &id) const{return m_registry.value(id).toObject().value("address").toInt();}
    void receiveBytes(const QByteArray &);
    void expireBuffer();
    void advance(double);
    QByteArray transact(const QByteArray &);
    void enableTestTransport(){m_test=true;setSerialMode(true);}
    static bool selfTest(QString *);
signals:
    void changed();
    void framesChanged();
    void portsChanged();
    void replyReady(const QByteArray &);
private:
    struct Device {
        double position=0,start=0,target=0,motionTime=0,dispensed=0,fillElapsed=0,lastSeen=-1;
        int pr=0,volume=250,fillTime=1250;
        bool moving=false,enabled=false;
        QHash<int,bool> coils;
    };
    void resetPlant();
    void log(const QString &);
    QByteArray registers(const QString &,int,int,bool,bool *) const;
    bool validateWrite(const QString &,int,int,bool) const;
    void write(const QString &,int,int,bool);
    void inferRecipe(const QString &,int,int);
    bool applyControl(const QList<QPair<int,int>> &);
    double source(const QString &,const QString &) const;
    QString idFor(int) const;
    TwinController *m_twin;
    QSerialPort m_port;
    QJsonObject m_config,m_registry,m_visual;
    QJsonArray m_catalog;
    QHash<QString,Device> m_devices;
    QString m_root,m_portName,m_error,m_lastError;
    QStringList m_frames;
    bool m_logDirty=false;
    double m_logNotifiedAt=-1;
    QByteArray m_buffer;
    QTimer m_timer,m_gap;
    QElapsedTimer m_wall;
    bool m_serialMode=false,m_test=false,m_paused=false;
    bool m_containerReady=false,m_finishedReady=false,m_capped=false,m_counted=false,m_explicitStage=false;
    int m_baud=115200,m_rxCount=0,m_txCount=0,m_crcErrors=0,m_exceptions=0;
    int m_control[6]={0,0,0,0,0,0};
    double m_clock=0,m_lastFrame=-1,m_flow=0,m_capAngle=0;
};
