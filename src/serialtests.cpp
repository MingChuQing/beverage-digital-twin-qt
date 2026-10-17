#include "serialplant.h"
#include "serialmaster.h"
#include "twincontroller.h"
#include <QJsonArray>
#include <QtEndian>
#include <QCoreApplication>
#include <cstring>

bool SerialPlant::selfTest(QString *error){
    auto check=[&](bool ok,const QString &message){if(!ok&&error)*error=message;return ok;};
    if(!check(Rtu::seal(QByteArray::fromHex("01030000000a"))==QByteArray::fromHex("01030000000ac5cd"),"标准 CRC16 测试向量错误"))return false;
    TwinController twin(nullptr,false);if(!check(twin.configError().isEmpty(),twin.configError()))return false;
    SerialPlant plant(&twin,twin.configurationRoot(),false);twin.attachPlant(&plant);plant.enableTestTransport();
    if(!check(plant.configError().isEmpty(),plant.configError()))return false;
    auto frame=[](int unit,int fc,int reg,int value){QByteArray b(1,char(unit));b+=char(fc);return Rtu::seal(b+Rtu::words({quint16(reg),quint16(value)}));};
    auto multi=[](int unit,int reg,const QList<quint16> &values){QByteArray b(1,char(unit));b+=char(16);b+=Rtu::words({quint16(reg),quint16(values.size())});b+=char(values.size()*2);return Rtu::seal(b+Rtu::words(values));};
    int control=plant.protocol().value("line_control").toObject().value("address").toInt();
    auto selection=plant.transact(multi(control,256,{1,2,2}));
    if(!check(Rtu::valid(selection)&&twin.profileIndex()==1&&twin.formatIndex()==2&&twin.activeFillDevice()=="GrapeFill","原子切换扩展葡萄小瓶配方失败"))return false;
    int head=plant.address("GrapeFill"),x=plant.address("X"),grip=plant.address("Gripper");
    if(!check(x==1&&plant.address("Outfeed")==7&&plant.address("OrangeFill")==21&&plant.address("Torque")==34,"单总线站号不符合统一配置"))return false;
    if(!check(quint8(plant.transact(multi(control,256,{0,0,2}))[1])==0x90&&twin.formatIndex()==2,"非法配方写入必须原子拒绝"))return false;
    plant.transact(frame(control,6,260,1));
    auto request=frame(x,6,0x6002,20);if(!check(plant.transact(request)==request,"FC06 回显不符合原 PR 接口"))return false;
    plant.advance(1);auto position=plant.transact(frame(x,3,0x602c,2));
    if(!check(Rtu::valid(position)&&position.mid(3,4)==QByteArray::fromHex("fffff830")&&qAbs(twin.axisX()+2)<.001,"PR4 运动 / 大端 int32 反馈错误"))return false;
    auto done=plant.transact(frame(x,4,0,1));if(!check(Rtu::word(done,3)==1,"轴完成标志错误"))return false;
    plant.transact(frame(head,6,16,250));plant.transact(frame(head,6,17,1000));
    auto invalid=plant.transact(multi(head,16,{500,0}));
    if(!check(quint8(invalid[1])==0x90&&plant.targetVolume()==250&&plant.targetTime()==1000,"多寄存器非法参数必须全部拒绝"))return false;
    auto invalidCoil=plant.transact(frame(grip,5,0,1));if(!check(quint8(invalidCoil[1])==0x85&&!plant.hasContainer(),"FC05 非标准线圈值未拒绝"))return false;
    plant.transact(frame(grip,5,0,0xff00));plant.transact(frame(control,6,259,5));plant.transact(frame(head,5,0,0xff00));plant.advance(.4);
    if(!check(plant.hasContainer()&&qAbs(twin.fillFraction()-.4)<.01&&qAbs(plant.snapshot("GrapeFill").value("value").toDouble()-100)<.1,"灌装指令未驱动液面 / 夹爪"))return false;
    plant.advance(.7);auto flow=plant.transact(frame(plant.address("FlowMeter"),3,0,2));quint32 bits=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(flow.constData()+3));float volume;std::memcpy(&volume,&bits,4);
    if(!check(Rtu::valid(flow)&&qAbs(volume-250)<.1&&Rtu::word(plant.transact(frame(head,3,18,1)),3)==250,"小端 float 流量 / 头部体积反馈错误"))return false;
    plant.transact(frame(head,5,0,0));
    int replies=0;QObject::connect(&plant,&SerialPlant::replyReady,[&](const QByteArray &reply){if(Rtu::valid(reply))++replies;});
    auto off=frame(grip,5,0,0),on=frame(grip,5,0,0xff00);plant.receiveBytes(off.left(3));if(!check(plant.hasContainer()&&replies==0,"半包不应更新设备"))return false;
    plant.receiveBytes(off.mid(3)+on);if(!check(plant.hasContainer()&&replies==2,"分包 / 粘包解析失败"))return false;
    auto corrupt=off;corrupt[7]=char(corrupt[7]^1);int before=plant.rxCount();plant.receiveBytes(corrupt);plant.expireBuffer();if(!check(plant.hasContainer()&&plant.rxCount()==before&&plant.crcErrors()>0,"CRC 错误报文改变了设备"))return false;
    auto badRegister=plant.transact(frame(x,6,0x9999,1));if(!check(quint8(badRegister[1])==0x86&&quint8(badRegister[2])==2,"非法寄存器未返回异常 02"))return false;
    auto shortException=plant.transact(Rtu::seal(QByteArray::fromHex("0107")));if(!check(Rtu::valid(shortException)&&quint8(shortException[1])==0x87,"短帧非法功能码未返回异常"))return false;
    auto unknownFunction=plant.transact(frame(x,17,0,0));if(!check(quint8(unknownFunction[1])==0x91&&quint8(unknownFunction[2])==1,"非法功能码未返回异常 01"))return false;
    if(!check(plant.transact(frame(239,6,0,1)).isEmpty(),"未知站号必须静默"))return false;
    if(!check(plant.transact(frame(0,5,0,0)).isEmpty()&&!plant.hasContainer(),"广播写入应生效且无应答"))return false;
    plant.transact(frame(x,6,0x6002,22));plant.advance(.25);plant.transact(frame(control,6,260,2));double frozen=twin.axisX();plant.advance(.4);if(!check(twin.axisX()==frozen,"主站暂停后运动未冻结"))return false;
    plant.transact(frame(control,6,260,1));plant.advance(.2);plant.advance(6);frozen=twin.axisX();plant.advance(1);if(!check(!plant.healthy()&&twin.axisX()==frozen&&plant.snapshot("X").value("quality").toString().startsWith("Bad"),"通信超时未冻结 / 无效标记错误"))return false;
    plant.receiveBytes(corrupt);plant.expireBuffer();if(!check(!plant.healthy(),"CRC 错误不得恢复通信质量"))return false;
    plant.transact(frame(x,4,0,1));plant.advance(.1);if(!check(plant.healthy()&&twin.axisX()!=frozen,"有效指令恢复后未继续运动"))return false;
    // Exercise the same master request/response matcher through a byte transport.
    TwinController senderTwin(nullptr,false);SerialMaster master(&senderTwin,plant.protocol(),plant.registry(),false);master.enableTestTransport();
    QObject::connect(&master,&SerialMaster::requestReady,&plant,&SerialPlant::receiveBytes);
    QObject::connect(&plant,&SerialPlant::replyReady,&master,&SerialMaster::receiveBytes);
    int acknowledgements=master.acknowledgements();master.triggerPr("X",4);master.pump();
    if(!check(master.acknowledgements()==acknowledgements+1&&master.failures()==0,"主站回显匹配 / 往返传输失败"))return false;
    if(error)error->clear();
    return true;
}
