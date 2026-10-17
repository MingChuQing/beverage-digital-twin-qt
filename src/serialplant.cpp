#include "serialplant.h"
#include "twincontroller.h"
#include <QSerialPortInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QSettings>
#include <QDateTime>
#include <QtEndian>
#include <QtMath>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <QSet>
#include <QProcess>
#include <QCoreApplication>

quint16 Rtu::crc(const QByteArray &data){quint16 value=0xffff;for(unsigned char byte:data){value^=byte;for(int i=0;i<8;++i)value=(value&1)?(value>>1)^0xa001:value>>1;}return value;}
QByteArray Rtu::seal(const QByteArray &data){auto result=data;auto c=crc(data);result.append(char(c&255));result.append(char(c>>8));return result;}
bool Rtu::valid(const QByteArray &data){return data.size()>=4&&crc(data.left(data.size()-2))==quint16(quint8(data.at(data.size()-2))|(quint8(data.back())<<8));}
quint16 Rtu::word(const QByteArray &data,int offset){return (quint8(data.at(offset))<<8)|quint8(data.at(offset+1));}
QByteArray Rtu::words(const QList<quint16> &values){QByteArray result;for(auto v:values){result.append(char(v>>8));result.append(char(v&255));}return result;}
QStringList Rtu::ports(){QStringList result;for(const auto &port:QSerialPortInfo::availablePorts())result<<port.portName();result.sort();return result;}
QJsonObject Rtu::object(const QString &path,QString &error){QFile file(path);if(!file.open(QIODevice::ReadOnly)){error="无法读取 "+path;return {};}QJsonParseError parse;auto doc=QJsonDocument::fromJson(file.readAll(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject()){error="无效 JSON："+path;return {};}return doc.object();}
bool Rtu::open(QSerialPort &port,const QString &name,int baud,const QJsonObject &settings,QString &error){
    port.close();port.setPortName(name);
    const QString parity=settings.value("parity").toString();
    if(name.trimmed().isEmpty()||baud<1200||baud>1000000||settings.value("data_bits").toInt()!=8||!QStringList{"None","Even","Odd"}.contains(parity)||!QList<int>{1,2}.contains(settings.value("stop_bits").toInt())){error="串口参数无效";return false;}
    if(!port.setBaudRate(baud)||!port.setDataBits(QSerialPort::Data8)||!port.setParity(parity=="Even"?QSerialPort::EvenParity:parity=="Odd"?QSerialPort::OddParity:QSerialPort::NoParity)||!port.setStopBits(settings.value("stop_bits").toInt()==2?QSerialPort::TwoStop:QSerialPort::OneStop)||!port.setFlowControl(QSerialPort::NoFlowControl)||!port.open(QIODevice::ReadWrite)){error=port.errorString();return false;}
    port.clear();return true;
}

SerialPlant::SerialPlant(TwinController *twin,const QString &root,bool automatic):m_twin(twin),m_root(root){
    m_config=Rtu::object(root+"/serial/protocol.json",m_error);
    m_registry=Rtu::object(root+"/"+m_config.value("registry").toString(),m_error).value("devices").toObject();
    m_visual=Rtu::object(root+"/visualization.json",m_error);
    m_catalog=Rtu::object(root+"/recipes/extended/system_variables.json",m_error).value("recipes").toArray();
    QSet<int> addresses;const int control=m_config.value("line_control").toObject().value("address").toInt();
    if(control<1||control>247){m_error="产线控制站号无效";}
    addresses.insert(control);
    auto line=m_config.value("line_control").toObject();int base=line.value("profile_register").toInt(-1);QStringList names={"profile_register","flavor_register","format_register","stage_register","command_register","completed_register"};
    for(int i=0;i<6;++i)if(base<0||base+5>65535||line.value(names[i]).toInt(-1)!=base+i)m_error="产线控制寄存器必须连续";
    auto definitions=m_config.value("devices").toObject();
    if(m_registry.size()!=21||definitions.size()!=21)m_error="单串口设备表必须包含 21 个端点";
    const QSet<QString> sourceNames={"done","position","zero","volume","fill_time_ms","dispensed","flow","pressure","torque","container","capped","level"};
    for(auto it=m_registry.begin();it!=m_registry.end();++it){
        const int address=it.value().toObject().value("address").toInt();
        if(!it.value().toObject().value("address").isDouble()||it.value().toObject().value("address").toDouble()!=address||address<1||address>247||addresses.contains(address)||!definitions.contains(it.key())||twin->device(it.key()).isEmpty())m_error="单串口站号无效、冲突或设备绑定缺失："+it.key();
        addresses.insert(address);m_devices.insert(it.key(),Device());
        auto d=definitions.value(it.key()).toObject();
        for(const QString &space:QStringList{"holding","input"}){
            QSet<int> used;auto fields=d.value(space).toObject();
            for(auto f=fields.begin();f!=fields.end();++f){bool ok=false;int a=f.key().toInt(&ok);auto field=f.value().toObject();auto type=field.value("type").toString();int count=type=="u16"?1:2;
                if(!ok||a<0||a+count>65536||!QStringList{"u16","i32be","f32le"}.contains(type)||!sourceNames.contains(field.value("source").toString()))m_error="寄存器映射无效";
                for(int i=0;i<count;++i){if(used.contains(a+i))m_error="寄存器映射重叠";used.insert(a+i);}
            }
        }
    }
    auto settings=m_config.value("serial").toObject();
    if(settings.value("stale_timeout_ms").toInt()<200||settings.value("motion_duration_ms").toInt()<100||settings.value("counts_per_scene_unit").toDouble()<=0||settings.value("partial_frame_timeout_ms").toInt()<2)m_error="串口时限 / 运动标定无效";
    m_portName=QSettings().value("serial/receiver_port",settings.value("receiver_port").toString()).toString();m_baud=QSettings().value("serial/receiver_baud",settings.value("baud_rate").toInt()).toInt();
    double scale=settings.value("counts_per_scene_unit").toDouble();
    if(!qIsFinite(scale)||scale<=0||settings.value("response_timeout_ms").toInt()<50||settings.value("response_timeout_ms").toInt()>10000||settings.value("partial_frame_timeout_ms").toInt()>1000||settings.value("motion_duration_ms").toInt()>60000)m_error="串口超时或位置缩放超出支持范围";
    for(const auto &axis:QStringList{"x","y","z"}){auto positions=m_visual.value(axis+"_pr").toObject();for(auto it=positions.begin();it!=positions.end();++it)if(!qIsFinite(it.value().toDouble()*scale)||qAbs(it.value().toDouble()*scale)>2147483647.)m_error="位置反馈超出 int32 范围";}
    resetPlant();m_wall.start();m_timer.setInterval(20);
    connect(&m_timer,&QTimer::timeout,this,[this]{advance(qMin(.25,m_wall.restart()/1000.));});
    connect(&m_port,&QSerialPort::readyRead,this,[this]{receiveBytes(m_port.readAll());});
    connect(&m_port,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError error){if(error==QSerialPort::ResourceError||error==QSerialPort::DeviceNotFoundError){m_lastError=m_port.errorString();m_port.close();log("串口错误："+m_lastError);emit changed();}});
    m_gap.setSingleShot(true);connect(&m_gap,&QTimer::timeout,this,&SerialPlant::expireBuffer);
    if(automatic)m_timer.start();
}
void SerialPlant::resetPlant(){
    for(auto it=m_devices.begin();it!=m_devices.end();++it){it.value()=Device();auto d=m_config.value("devices").toObject().value(it.key()).toObject();QString axis=d.value("axis").toString();if(!axis.isEmpty())it.value().position=m_visual.value(axis+"_pr").toObject().value("0").toDouble();it.value().target=it.value().position;it.value().volume=m_twin->recipe().value("volume_ml").toInt();it.value().fillTime=m_twin->recipe().value("fill_time_ms").toInt();}
    m_containerReady=m_finishedReady=m_capped=m_counted=m_explicitStage=false;
    m_flow=0;m_capAngle=0;m_paused=false;m_lastFrame=-1;m_control[0]=m_twin->profileIndex();m_control[1]=m_twin->flavorIndex();m_control[2]=m_twin->formatIndex();for(int i=3;i<6;++i)m_control[i]=0;
}
void SerialPlant::setSerialMode(bool value){if(value==m_serialMode)return;if(value){m_twin->pause();m_serialMode=true;resetPlant();}else{closePort();m_serialMode=false;m_test=false;m_twin->reset();}emit changed();}
bool SerialPlant::openPort(const QString &name,int baud){
    setSerialMode(true);m_lastError.clear();if(!m_error.isEmpty()){m_lastError=m_error;emit changed();return false;}
    m_portName=name.trimmed();m_baud=baud;m_buffer.clear();m_gap.stop();m_port.close();m_test=false;resetPlant();
    if(!Rtu::open(m_port,m_portName,baud,m_config.value("serial").toObject(),m_lastError)){log("打开失败："+m_lastError);emit changed();return false;}
    QSettings settings;settings.setValue("serial/receiver_port",m_portName);settings.setValue("serial/receiver_baud",baud);log("打开接收串口 "+m_portName);emit changed();return true;
}
void SerialPlant::closePort(){m_port.close();m_test=false;m_buffer.clear();m_gap.stop();m_lastFrame=-1;emit changed();}
void SerialPlant::launchSender(){QProcess::startDetached(QCoreApplication::applicationFilePath(),QStringList{"--sender"});}
double SerialPlant::targetVolume() const{return m_devices.value(m_twin->activeFillDevice()).volume;}
int SerialPlant::targetTime() const{return m_devices.value(m_twin->activeFillDevice()).fillTime;}
bool SerialPlant::healthy() const{return connected()&&m_lastFrame>=0&&(m_clock-m_lastFrame)*1000<m_config.value("serial").toObject().value("stale_timeout_ms").toInt();}
QString SerialPlant::status() const{if(!m_error.isEmpty())return m_error;if(!m_lastError.isEmpty())return m_lastError;if(!connected())return "串口未打开";if(m_lastFrame<0)return "等待有效 Modbus RTU 指令";if(!healthy())return "通信超时 · 动作冻结";return m_paused?"串口在线 · 指令暂停":"串口在线 · 指令驱动";}
void SerialPlant::log(const QString &message){m_logDirty=true;m_frames.prepend(QTime::currentTime().toString("HH:mm:ss.zzz")+" "+message);while(m_frames.size()>80)m_frames.removeLast();}
QString SerialPlant::idFor(int address) const{for(auto it=m_registry.begin();it!=m_registry.end();++it)if(it.value().toObject().value("address").toInt()==address)return it.key();return {};}
double SerialPlant::axis(const QString &id) const{return m_devices.value(id).position;}
bool SerialPlant::output(const QString &id) const{return healthy()&&!m_paused&&m_devices.value(id).enabled;}
bool SerialPlant::hasContainer() const{return m_devices.value("Gripper").enabled;}
double SerialPlant::fillFraction() const{auto d=m_devices.value(m_twin->activeFillDevice());return qBound(0.,d.dispensed/qMax(1,d.volume),1.);}
double SerialPlant::stageProgress() const{if(stage()==5)return fillFraction();double p=1;bool moving=false;for(const auto &d:m_devices)if(d.moving){moving=true;p=qMin(p,d.motionTime/(m_config.value("serial").toObject().value("motion_duration_ms").toDouble()/1000.));}return moving?p:0;}
double SerialPlant::source(const QString &id,const QString &sourceName) const{
    const auto d=m_devices.value(id);
    if(sourceName=="position")return d.position*m_config.value("serial").toObject().value("counts_per_scene_unit").toDouble();
    if(sourceName=="done")return !d.moving&&!(m_config.value("devices").toObject().value(id).toObject().value("kind")=="fill"&&d.enabled&&d.dispensed<d.volume);
    if(sourceName=="volume")return d.volume;
    if(sourceName=="fill_time_ms")return d.fillTime;
    if(sourceName=="dispensed")return d.dispensed;
    if(sourceName=="flow")return m_flow;
    if(sourceName=="container")return hasContainer()||m_containerReady||m_finishedReady;
    if(sourceName=="capped")return m_capped;
    if(sourceName=="level")return fillFraction()>=.98;
    if(sourceName=="pressure"){if(output("CIPValve"))return .35;if(output("RinseValve"))return .22;for(auto it=m_devices.begin();it!=m_devices.end();++it)if(it.key().endsWith("Fill")&&output(it.key()))return .22;return 0;}
    if(sourceName=="torque")return healthy()&&!m_paused&&(m_devices.value("Relay").coils.value(11)||m_devices.value("Closure").moving)?2.2:0;
    return 0;
}
QByteArray SerialPlant::registers(const QString &id,int start,int count,bool input,bool *ok) const{
    QHash<int,quint16> image;
    if(id=="@line"){if(input){*ok=false;return {};}auto control=m_config.value("line_control").toObject();for(int i=0;i<6;++i)image.insert(control.value(QStringList{"profile_register","flavor_register","format_register","stage_register","command_register","completed_register"}[i]).toInt(),quint16(m_control[i]));}
    else{
        auto fields=m_config.value("devices").toObject().value(id).toObject().value(input?"input":"holding").toObject();
        for(auto it=fields.begin();it!=fields.end();++it){int a=it.key().toInt();auto field=it.value().toObject();QString type=field.value("type").toString();double value=source(id,field.value("source").toString());
            if(type=="u16")image.insert(a,quint16(qBound(0.,value,65535.)));
            else if(type=="i32be"){quint32 bits=quint32(qint32(qRound64(value)));image.insert(a,quint16(bits>>16));image.insert(a+1,quint16(bits));}
            else {float f=float(value);quint32 bits;std::memcpy(&bits,&f,4);QByteArray bytes(4,0);qToLittleEndian(bits,reinterpret_cast<uchar*>(bytes.data()));image.insert(a,Rtu::word(bytes,0));image.insert(a+1,Rtu::word(bytes,2));}
        }
    }
    QByteArray data;*ok=true;for(int i=0;i<count;++i){if(!image.contains(start+i)){*ok=false;return {};}data+=Rtu::words({image.value(start+i)});}return data;
}
bool SerialPlant::validateWrite(const QString &id,int reg,int value,bool coil) const{
    auto d=m_config.value("devices").toObject().value(id).toObject();auto fields=d.value(coil?"write_coils":"write_registers").toObject();
    if(!fields.contains(QString::number(reg)))return false;
    auto meaning=fields.value(QString::number(reg)).toString();
    if(coil)return value==0||value==0xff00;
    if(meaning=="pr_trigger"){
        if(value<16||value>31)return false;
        auto axis=d.value("axis").toString();return axis.isEmpty()||m_visual.value(axis+"_pr").toObject().contains(QString::number(value-16));
    }
    return (meaning!="volume"&&meaning!="fill_time_ms")||value>0;
}
void SerialPlant::write(const QString &id,int reg,int value,bool coil){
    auto &d=m_devices[id];auto definition=m_config.value("devices").toObject().value(id).toObject();QString meaning=definition.value(coil?"write_coils":"write_registers").toObject().value(QString::number(reg)).toString();
    if(coil){bool previous=d.coils.value(reg);d.coils[reg]=value==0xff00;bool was=d.enabled;d.enabled=false;for(bool on:d.coils)d.enabled|=on;if(meaning=="enable"&&definition.value("kind")=="fill"&&d.enabled&&!was){d.dispensed=0;d.fillElapsed=0;if(!m_explicitStage)m_control[3]=5;}
        if(id=="Gripper"){if(d.enabled&&!was){m_containerReady=false;m_capped=false;if(!m_explicitStage)m_control[3]=2;}if(!d.enabled&&was){m_finishedReady=true;if(!m_explicitStage)m_control[3]=9;}}
        if(id=="Relay"&&reg==11&&previous&&value==0)m_capped=true;
        if(d.enabled&&!m_explicitStage){if(id=="RinseValve")m_control[3]=3;if(id=="CIPValve")m_control[3]=10;}
    }
    else if(meaning=="pr_trigger"){d.pr=value-16;d.start=d.position;auto axis=definition.value("axis").toString();d.target=axis.isEmpty()?d.pr:m_visual.value(axis+"_pr").toObject().value(QString::number(d.pr)).toDouble();d.motionTime=0;d.moving=true;
        if(id=="Infeed"){m_containerReady=false;m_finishedReady=false;m_counted=false;inferRecipe("",0,d.pr);if(!m_explicitStage)m_control[3]=1;}
        if(id=="Closure"&&!m_explicitStage)m_control[3]=7;
        if(id=="Outfeed"&&!m_explicitStage)m_control[3]=9;
    }
    else if(meaning=="volume"){d.volume=value;inferRecipe(id,value,-1);}
    else if(meaning=="fill_time_ms")d.fillTime=value;
    else if(meaning=="clear_flow")m_flow=0;
}
void SerialPlant::inferRecipe(const QString &head,int volume,int infeed){
    for(const auto &entry:m_catalog){auto recipe=entry.toObject();
        if(!head.isEmpty()&&(recipe.value("fill_device").toString()!=head||recipe.value("volume_ml").toInt()!=volume))continue;
        if(head.isEmpty()&&(recipe.value("infeed_pr").toInt()!=infeed||recipe.value("flavor").toString()!=m_twin->recipe().value("flavor").toString()))continue;
        int flavor=QStringList{"orange","apple","grape"}.indexOf(recipe.value("flavor").toString());int format=QStringList{"family","can","small"}.indexOf(recipe.value("format").toString());
        int profile=format==2?1:m_control[0];if(flavor<0||format<0)return;
        if(profile!=m_control[0]||flavor!=m_control[1]||format!=m_control[2]){if(m_twin->applySerialRecipe(profile,flavor,format)){m_control[0]=profile;m_control[1]=flavor;m_control[2]=format;}}
        return;
    }
}
bool SerialPlant::applyControl(const QList<QPair<int,int>> &writes){
    int next[6];std::copy(m_control,m_control+6,next);auto c=m_config.value("line_control").toObject();const QStringList fields={"profile_register","flavor_register","format_register","stage_register","command_register","completed_register"};
    for(const auto &w:writes){int index=-1;for(int i=0;i<6;++i)if(c.value(fields[i]).toInt()==w.first)index=i;if(index<0)return false;next[index]=w.second;}
    if(next[0]>1||next[1]>2||next[2]>(next[0]?2:1)||next[3]>11||next[4]>3)return false;
    if(next[0]!=m_control[0]||next[1]!=m_control[1]||next[2]!=m_control[2]){if(!m_twin->applySerialRecipe(next[0],next[1],next[2]))return false;auto &d=m_devices[m_twin->activeFillDevice()];d.volume=m_twin->recipe().value("volume_ml").toInt();d.fillTime=m_twin->recipe().value("fill_time_ms").toInt();}
    for(const auto &w:writes)if(w.first==c.value("stage_register").toInt())m_explicitStage=true;
    bool commandWritten=false;for(const auto &w:writes)commandWritten|=w.first==c.value("command_register").toInt();
    if(commandWritten){if(next[4]==3){resetPlant();next[3]=0;next[4]=0;next[5]=0;}else m_paused=next[4]!=1;}
    std::copy(next,next+6,m_control);return true;
}
QByteArray SerialPlant::transact(const QByteArray &frame){
    if(!Rtu::valid(frame)||frame.size()<4)return {};
    int unit=quint8(frame[0]),fc=quint8(frame[1]);
    bool broadcast=unit==0;QString id=unit==m_config.value("line_control").toObject().value("address").toInt()?"@line":idFor(unit);
    if(id.isEmpty()&&!broadcast)return {};
    auto error=[&](int code){++m_exceptions;log(QString("异常：站 %1 FC%2 代码 %3").arg(unit).arg(fc).arg(code));emit changed();return broadcast?QByteArray():Rtu::seal(QByteArray(1,char(unit))+char(fc|0x80)+char(code));};
    if(!QList<int>{3,4,5,6,16}.contains(fc))return error(1);
    if(frame.size()<8)return error(3);
    int reg=Rtu::word(frame,2),arg=Rtu::word(frame,4);
    if((fc!=16&&frame.size()!=8)||(fc==16&&(frame.size()<11||quint8(frame[6])!=arg*2||frame.size()!=9+arg*2)))return error(3);
    QByteArray response;
    if(fc==3||fc==4){
        if(broadcast)return {};
        if(arg<1||arg>125||reg+arg>65536)return error(3);
        bool ok=false;auto data=registers(id,reg,arg,fc==4,&ok);if(!ok)return error(2);response=QByteArray(1,char(unit))+char(fc)+char(data.size())+data;
    }else{
        if(fc==16&&(arg<1||arg>123||reg+arg>65536))return error(3);
        QList<QPair<int,int>> values;if(fc==16){for(int i=0;i<arg;++i)values<<qMakePair(reg+i,int(Rtu::word(frame,7+i*2)));}else values<<qMakePair(reg,arg);
        if(id=="@line"){
            if(fc==5)return error(2);
            int base=m_config.value("line_control").toObject().value("profile_register").toInt();
            for(const auto &value:values)if(value.first<base||value.first>base+5)return error(2);
            if(!applyControl(values))return error(3);
        }else{
            QStringList targets=broadcast?m_devices.keys():QStringList{id};bool applied=false;
            for(const auto &target:targets){auto mapping=m_config.value("devices").toObject().value(target).toObject().value(fc==5?"write_coils":"write_registers").toObject();bool supported=true,valid=true;for(const auto &w:values){supported&=mapping.contains(QString::number(w.first));valid&=validateWrite(target,w.first,w.second,fc==5);}
                if(!supported){if(!broadcast)return error(2);continue;}if(!valid){if(!broadcast)return error(3);continue;}
                for(const auto &w:values){write(target,w.first,w.second,fc==5);}
                m_devices[target].lastSeen=m_clock;applied=true;
            }
            if(!applied&&broadcast)return {};
        }
        response=frame.left(6);
    }
    ++m_rxCount;m_lastFrame=m_clock;if(m_devices.contains(id))m_devices[id].lastSeen=m_clock;
    log("RX "+QString::fromLatin1(frame.toHex(' ').toUpper()));
    emit changed();return broadcast?QByteArray():Rtu::seal(response);
}
void SerialPlant::receiveBytes(const QByteArray &bytes){
    if(bytes.isEmpty())return;
    m_buffer+=bytes;
    if(m_buffer.size()>4096){m_buffer.clear();++m_crcErrors;log("接收缓存溢出，丢弃");emit changed();return;}
    while(m_buffer.size()>=4){int found=-1,size=0;
        for(int offset=0;offset+4<=m_buffer.size();++offset){int fc=quint8(m_buffer[offset+1]);int length=(fc==7||fc==17)?4:8;if(fc==16||fc==15){if(offset+7>m_buffer.size())continue;length=9+quint8(m_buffer[offset+6]);if(length>256)continue;}if(offset+length>m_buffer.size())continue;
            int address=quint8(m_buffer[offset]);if(address>247)continue;if(Rtu::valid(m_buffer.mid(offset,length))){found=offset;size=length;break;}
        }
        if(found<0)break;
        if(found){++m_crcErrors;log("丢弃无效前缀 / CRC 错误");m_buffer.remove(0,found);}
        auto frame=m_buffer.left(size);m_buffer.remove(0,size);auto response=transact(frame);
        if(!response.isEmpty()){++m_txCount;log("TX "+QString::fromLatin1(response.toHex(' ').toUpper()));if(m_port.isOpen()&&m_port.write(response)!=response.size()){m_lastError="串口写入失败";}emit replyReady(response);}emit changed();
    }
    if(!m_buffer.isEmpty())m_gap.start(m_config.value("serial").toObject().value("partial_frame_timeout_ms").toInt());else m_gap.stop();
}
void SerialPlant::expireBuffer(){if(m_buffer.isEmpty())return;m_buffer.clear();++m_crcErrors;log("丢弃不完整报文 / CRC 错误；设备状态未更新");emit changed();}
void SerialPlant::advance(double seconds){
    if(seconds<=0||!qIsFinite(seconds))return;
    m_clock+=seconds;
    if(m_logDirty&&m_clock-m_logNotifiedAt>=.2){m_logDirty=false;m_logNotifiedAt=m_clock;emit framesChanged();}
    if(m_serialMode&&healthy()&&!m_paused){
        double motion=m_config.value("serial").toObject().value("motion_duration_ms").toDouble()/1000.;
        for(auto it=m_devices.begin();it!=m_devices.end();++it){auto &d=it.value();if(d.moving){d.motionTime=qMin(motion,d.motionTime+seconds);double p=d.motionTime/motion;p=p*p*(3-2*p);d.position=d.start+(d.target-d.start)*p;if(d.motionTime>=motion){d.position=d.target;d.moving=false;
                if(it.key()=="Infeed")m_containerReady=true;
                if(it.key()=="Closure"&&hasContainer())m_capped=true;
                if(it.key()=="Outfeed"&&m_finishedReady&&!m_counted){if(m_capped&&fillFraction()>=.98){++m_control[5];m_counted=true;}}
            }}
            if(it.key().endsWith("Fill")&&d.enabled&&d.dispensed<d.volume){double previous=d.dispensed;d.fillElapsed+=seconds;d.dispensed=qMin(double(d.volume),d.fillElapsed*1000/qMax(1,d.fillTime)*d.volume);m_flow+=d.dispensed-previous;}
        }
        if(m_devices.value("Closure").moving||m_devices.value("Relay").coils.value(11))m_capAngle=std::fmod(m_capAngle+seconds*240,360.);
    }
    if(m_serialMode)emit changed();
}
QVariantMap SerialPlant::snapshot(const QString &id) const{
    const auto d=m_devices.value(id);const auto def=m_config.value("devices").toObject().value(id).toObject();QString kind=def.value("kind").toString();bool numeric=true;double value=0;QString text,unit,detail;
    bool active=false;
    if(kind=="motion"){value=d.position*100;unit=def.value("axis").toString().isEmpty()?"PR":"展示单位";text=def.value("axis").toString().isEmpty()?QString("PR%1").arg(d.pr):QString::number(value,'f',1);numeric=!def.value("axis").toString().isEmpty();active=d.moving;detail=QString("收到目标 PR%1；FC06 0x6002 = 0x10 + PR").arg(d.pr);}
    else if(kind=="fill"){value=d.dispensed;unit="mL";text=QString::number(value,'f',1);active=d.enabled&&d.dispensed<d.volume;detail=QString("指令目标 %1 mL / %2 ms").arg(d.volume).arg(d.fillTime);}
    else if(id=="FlowMeter"){value=m_flow;unit="mL";text=QString::number(value,'f',1);active=output(m_twin->activeFillDevice());}
    else if(id=="PressureSensor"||id=="Torque"){value=source(id,id=="Torque"?"torque":"pressure");unit=id=="Torque"?"N·m":"MPa";text=QString::number(value,'f',3);}
    else {numeric=false;value=id=="ContainerSensor"||id=="BottleSensor"?source(id,"container"):id=="LevelSensor"?source(id,"level"):d.enabled;active=value!=0;text=value?"ON / 已到位":"OFF / 未到位";}
    const bool valid=healthy()&&d.lastSeen>=0&&(m_clock-d.lastSeen)*1000<m_config.value("serial").toObject().value("stale_timeout_ms").toInt();
    return {{"value",valid?QVariant(value):QVariant()},{"valueText",valid?text:QStringLiteral("—")},{"lastValid",d.lastSeen>=0?text+(unit.isEmpty()?"":" "+unit):"—"},{"unit",unit},{"numeric",numeric},{"quality",valid?"Good · RTU 指令 / 虚拟反馈":d.lastSeen<0?"Unknown · 未收到":"Bad · 通信超时"},{"ageMs",d.lastSeen<0?0:qRound((m_clock-d.lastSeen)*1000)},{"statusKind",!valid?"fault":active&&!m_paused?"running":"idle"},{"statusText",!valid?(d.lastSeen<0?"等待该站指令":"通信超时"):m_paused?"指令暂停":active?"动作中":"已到位 / 待机"},{"sourceKind","串口接收指令 → 虚拟设备反馈"},{"rawText","Modbus RTU / CRC16 已校验；反馈由虚拟设备计算"},{"target",detail},{"port",m_portName},{"station",address(id)}};
}
