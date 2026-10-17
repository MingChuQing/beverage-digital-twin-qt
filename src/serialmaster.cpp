#include "serialmaster.h"
#include "twincontroller.h"
#include <QSettings>
#include <QTime>
#include <QJsonArray>
#include <algorithm>

SerialMaster::SerialMaster(TwinController *twin,const QJsonObject &config,const QJsonObject &registry,bool automatic):m_twin(twin),m_config(config),m_registry(registry){
    auto settings=config.value("serial").toObject();m_portName=QSettings().value("serial/sender_port",settings.value("sender_port").toString()).toString();m_baud=QSettings().value("serial/sender_baud",settings.value("baud_rate").toInt()).toInt();
    m_ids=registry.keys();std::sort(m_ids.begin(),m_ids.end(),[&](const QString &a,const QString &b){return address(a)<address(b);});
    for(const auto &id:m_ids)m_labels<<QString("%1 / %2 / 站 %3").arg(twin->device(id).value("displayName").toString()).arg(id).arg(address(id));
    m_clock.start();m_timer.setInterval(10);connect(&m_timer,&QTimer::timeout,this,&SerialMaster::pump);
    connect(&m_port,&QSerialPort::readyRead,this,[this]{receiveBytes(m_port.readAll());});
    connect(&m_port,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError error){if(error==QSerialPort::ResourceError||error==QSerialPort::DeviceNotFoundError){QString message=m_port.errorString();closePort();failed(message);}});
    if(automatic)m_timer.start();
}
int SerialMaster::address(const QString &id) const{return m_registry.value(id).toObject().value("address").toInt();}
QString SerialMaster::status() const{if(!m_lastError.isEmpty())return m_lastError;if(!connected())return "发送串口未打开";return m_demo?"正在发送单件工艺指令":m_pending?"等待 Modbus 应答":"串口已打开 · 自动轮询设备";}
void SerialMaster::log(const QString &message){m_logDirty=true;m_frames.prepend(QTime::currentTime().toString("HH:mm:ss.zzz")+" "+message);while(m_frames.size()>100)m_frames.removeLast();}
bool SerialMaster::openPort(const QString &name,int baud){closePort();m_portName=name.trimmed();m_baud=baud;m_lastError.clear();if(!Rtu::open(m_port,m_portName,baud,m_config.value("serial").toObject(),m_lastError)){log("打开失败："+m_lastError);emit changed();return false;}QSettings settings;settings.setValue("serial/sender_port",m_portName);settings.setValue("serial/sender_baud",baud);log("打开发送串口 "+m_portName);emit changed();return true;}
void SerialMaster::closePort(){m_port.close();m_test=false;m_jobs.clear();m_pending=false;m_demo=false;m_buffer.clear();m_readyAt=0;emit changed();}
void SerialMaster::failed(const QString &message){++m_failures;m_lastError=message;m_jobs.clear();m_demo=false;m_pending=false;m_buffer.clear();m_readyAt=m_clock.elapsed()+1000;log(message+"；已停止发送队列");emit changed();}
void SerialMaster::enqueue(int unit,int fc,int reg,const QList<quint16> &values,int delay){
    if(!connected()){m_lastError="请先打开发送串口";emit changed();return;}if(m_jobs.size()>=256){failed("发送队列已满");return;}
    QByteArray frame(1,char(unit));frame.append(char(fc));frame+=Rtu::words({quint16(reg)});
    if(fc==16){frame+=Rtu::words({quint16(values.size())});frame.append(char(values.size()*2));frame+=Rtu::words(values);}else frame+=Rtu::words(values);
    m_jobs.enqueue({Rtu::seal(frame),delay});m_lastError.clear();emit changed();
}
bool SerialMaster::send(int unit,int fc,const QString &regText,const QString &valueText){
    bool ok=false;int reg=regText.toInt(&ok,0);if(!ok||reg<0||reg>65535||unit<0||unit>247||!QList<int>{3,4,5,6,16}.contains(fc)){m_lastError="站号 / 功能码 / 寄存器无效（十六进制请加 0x）";emit changed();return false;}
    QList<quint16> values;for(const auto &part:valueText.split(',',Qt::SkipEmptyParts)){bool valid=false;int value=part.trimmed().toInt(&valid,0);if(!valid||value<0||value>65535){m_lastError="数据必须是 0–65535 整数";emit changed();return false;}values<<quint16(value);}
    if(values.isEmpty()||(fc!=16&&values.size()!=1)||(fc==16&&values.size()>123)||((fc==3||fc==4)&&(unit==0||values[0]<1||values[0]>125||reg+values[0]>65536))||(fc==5&&values[0]!=0&&values[0]!=0xff00)||(fc==16&&reg+values.size()>65536)){m_lastError="数量 / 线圈值无效";emit changed();return false;}
    if(!connected()){m_lastError="请先打开串口";emit changed();return false;}enqueue(unit,fc,reg,values);return true;
}
void SerialMaster::registerWrite(const QString &id,int reg,int value,int delay){enqueue(address(id),6,reg,{quint16(value)},delay);}
void SerialMaster::coilWrite(const QString &id,bool on,int delay){auto fields=m_config.value("devices").toObject().value(id).toObject().value("write_coils").toObject();if(fields.isEmpty())return;enqueue(address(id),5,fields.keys().first().toInt(),{quint16(on?0xff00:0)},delay);}
void SerialMaster::lineWrite(int index,int value,int delay){auto line=m_config.value("line_control").toObject();QStringList names={"profile_register","flavor_register","format_register","stage_register","command_register","completed_register"};enqueue(line.value("address").toInt(),6,line.value(names[index]).toInt(),{quint16(value)},delay);}
void SerialMaster::triggerPr(const QString &id,int pr){if(!m_registry.contains(id)||pr<0||pr>15)return;registerWrite(id,0x6002,16+pr);}
void SerialMaster::toggle(const QString &id,bool on){coilWrite(id,on);}
void SerialMaster::applyRecipe(){auto line=m_config.value("line_control").toObject();enqueue(line.value("address").toInt(),16,line.value("profile_register").toInt(),{quint16(m_twin->profileIndex()),quint16(m_twin->flavorIndex()),quint16(m_twin->formatIndex())});}
void SerialMaster::startDemo(){
    if(!connected()||m_demo||!m_jobs.isEmpty())return;
    QString error;auto variables=Rtu::object(m_twin->configLocation()+"/system_variables.json",error);if(!error.isEmpty()){failed(error);return;}
    m_demo=true;applyRecipe();lineWrite(4,3);lineWrite(4,1);
    auto recipe=m_twin->recipe();auto points=variables.value("production_points").toArray();
    int movement=m_config.value("serial").toObject().value("motion_duration_ms").toInt()+150;
    auto move=[&](int index){auto point=points.at(index).toObject();registerWrite("X",0x6002,16+point.value("x").toInt());registerWrite("Y1",0x6002,16+point.value("y").toInt());registerWrite("Y2",0x6002,16+point.value("y").toInt());registerWrite("Z",0x6002,16+point.value("z").toInt(),movement);};
    auto station=[&](const QString &name){move(recipe.value(name+"_entry").toInt());move(recipe.value(name+"_work").toInt());};
    auto relay=[&](int reg,bool on,int delay=0){enqueue(address("Relay"),5,reg,{quint16(on?0xff00:0)},delay);};
    lineWrite(3,0);move(recipe.value("home_point").toInt());
    lineWrite(3,1);registerWrite("Infeed",0x6002,16+recipe.value("infeed_pr").toInt());station("empty");
    lineWrite(3,2);coilWrite("Gripper",true,250);enqueue(address("ContainerSensor"),4,0,{1});
    lineWrite(3,3);station("rinse");coilWrite("RinseValve",true,700);coilWrite("RinseValve",false);
    lineWrite(3,4);station("fill");
    lineWrite(3,5);registerWrite("FlowMeter",1,0);QString head=recipe.value("fill_device").toString();registerWrite(head,16,recipe.value("volume_ml").toInt());registerWrite(head,17,recipe.value("fill_time_ms").toInt());coilWrite(head,true,recipe.value("fill_time_ms").toInt()+250);enqueue(address(head),4,0,{1});coilWrite(head,false);
    lineWrite(3,6);enqueue(address("LevelSensor"),4,0x38,{1});enqueue(address("FlowMeter"),3,0,{2});
    lineWrite(3,7);station("closure");coilWrite("CapFeeder",true);registerWrite("Closure",0x6002,16+recipe.value("closure_pr").toInt(),movement);relay(11,true,600);relay(11,false);coilWrite("CapFeeder",false);
    lineWrite(3,8);station("code");relay(12,true,200);relay(12,false);relay(13,true,200);relay(13,false);
    lineWrite(3,9);station("finished");coilWrite("Gripper",false);registerWrite("Outfeed",0x6002,16+recipe.value("outfeed_pr").toInt(),movement);lineWrite(5,1);
    lineWrite(3,10);coilWrite("CIPValve",true,700);coilWrite("CIPValve",false);
    lineWrite(3,11);move(recipe.value("home_point").toInt());log("开始单件串口工艺 · "+m_twin->recipeCaption());emit changed();
}
void SerialMaster::stopDemo(){m_jobs.clear();m_current.delay=0;m_demo=false;m_readyAt=0;if(connected())lineWrite(4,0);emit changed();}
void SerialMaster::dispatch(const Task &task,bool heartbeat){
    m_current=task;m_pending=true;m_heartbeat=heartbeat;m_sentAt=m_clock.elapsed();
    log("TX "+QString::fromLatin1(task.frame.toHex(' ').toUpper()));
    if(m_port.isOpen()&&m_port.write(task.frame)!=task.frame.size()){failed("串口发送失败");return;}
    emit requestReady(task.frame);
    if(quint8(task.frame[0])==0){m_pending=false;m_readyAt=m_clock.elapsed()+qMax(5,task.delay);}
    emit changed();
}
void SerialMaster::pump(){
    if(m_logDirty&&m_clock.elapsed()-m_logNotifiedAt>=200){m_logDirty=false;m_logNotifiedAt=m_clock.elapsed();emit framesChanged();}
    if(!connected())return;
    const qint64 now=m_clock.elapsed();
    if(m_pending){if(now-m_sentAt>m_config.value("serial").toObject().value("response_timeout_ms").toInt())failed("应答超时：检查对端串口、接线及波特率");return;}
    if(!m_jobs.isEmpty()&&now>=m_readyAt){dispatch(m_jobs.dequeue());return;}
    if(m_demo&&m_jobs.isEmpty()&&now>=m_readyAt){m_demo=false;log("单件工艺发送完成");emit completedDemo();emit changed();}
    if(now-m_lastPoll>=90&&now>=m_sentAt+5){m_lastPoll=now;QString id=m_ids.value(m_pollIndex++%m_ids.size());QByteArray request(1,char(address(id)));request+=char(4);request+=Rtu::words({0,1});dispatch({Rtu::seal(request),0},true);}
}
void SerialMaster::receiveBytes(const QByteArray &bytes){
    m_buffer+=bytes;if(m_buffer.size()>2048){m_buffer.clear();log("接收缓存溢出");emit changed();return;}
    while(m_buffer.size()>=5){int length=0,found=-1;for(int offset=0;offset+5<=m_buffer.size();++offset){int fc=quint8(m_buffer[offset+1]);int n=(fc&0x80)?5:(fc==3||fc==4)?5+quint8(m_buffer[offset+2]):8;if(n>256||offset+n>m_buffer.size())continue;if(Rtu::valid(m_buffer.mid(offset,n))){found=offset;length=n;break;}}
        if(found<0)break;
        auto frame=m_buffer.mid(found,length);m_buffer.remove(0,found+length);
        if(!m_pending||quint8(frame[0])!=quint8(m_current.frame[0])||((quint8(frame[1])&0x7f)!=quint8(m_current.frame[1]))){log("忽略无匹配请求的应答");continue;}
        if(quint8(frame[1])&0x80){failed(QString("从站异常码 %1，报文 %2").arg(quint8(frame[2])).arg(QString::fromLatin1(frame.toHex(' '))));return;}
        int fc=quint8(frame[1]);bool valid=(fc==3||fc==4)?quint8(frame[2])==Rtu::word(m_current.frame,4)*2&&frame.size()==5+quint8(frame[2]):frame.left(6)==m_current.frame.left(6);
        if(!valid){failed("应答功能码 / 寄存器 / 数量不匹配");return;}
        ++m_acks;log("RX "+QString::fromLatin1(frame.toHex(' ').toUpper()));m_pending=false;m_lastError.clear();
        if(!m_heartbeat){m_readyAt=m_clock.elapsed()+qMax(5,m_current.delay);}
        emit changed();
    }
}
