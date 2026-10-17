#include "twincontroller.h"
#include "serialmaster.h"
#include "aicontroller.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QDebug>
#include <QLoggingCategory>
#include <QSurfaceFormat>
#include <QFont>
#include <memory>

int main(int argc,char **argv){
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format;format.setDepthBufferSize(24);format.setStencilBufferSize(8);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    app.setFont(QFont("Microsoft YaHei",10));
    QLoggingCategory::setFilterRules("QSSG.trace_info=false");
    app.setOrganizationName("IndustrialConfigEngine");app.setApplicationName("BeverageTwinDemo");app.setApplicationVersion("0.4.0");
    const auto args=app.arguments();
    auto optionText=[&args](const QString &name,const QString &fallback){int i=args.indexOf(name);return i>=0?args.value(i+1):fallback;};
    if(args.contains("--self-test")){QString error;bool ok=TwinController::selfTest(&error);qInfo().noquote()<< (ok?"PASS: 6/9 recipe configuration, shared filling heads, point mapping, batch simulation, pause, offline and faults":"FAIL: "+error);return ok?0:1;}
    if(args.contains("--list-ports")){qInfo().noquote()<<QJsonDocument(QJsonArray::fromStringList(Rtu::ports())).toJson(QJsonDocument::Compact);return 0;}
    if(args.contains("--rtu-self-test")){QString error;bool ok=SerialPlant::selfTest(&error);qInfo().noquote()<<(ok?"PASS: RTU CRC, framing, commands, replies, addresses, atomic writes, quality, motion, filling, master loopback":"FAIL: "+error);return ok?0:1;}
    QQuickStyle::setStyle("Fusion");
    TwinController twin(nullptr,!args.contains("--sender"));
    twin.setActivationPath(optionText("--ai-active-file",twin.activationPath()),args.contains("--ai-playback-test")||args.contains("--ai-allow-test-profile"));
    if(!twin.configError().isEmpty()){qCritical().noquote()<<twin.configError();return 2;}
    if(args.contains("--extended")&&!twin.loadProfile(1))return 2;
    auto optionNumber=[&args](const QString &name){int i=args.indexOf(name);if(i<0)return 0;bool valid=false;int value=args.value(i+1).toInt(&valid);return valid?value:-1;};
    if(!twin.selectRecipe(optionNumber("--flavor"),optionNumber("--format"))){qCritical()<<"Unsupported flavor / format in selected profile";return 2;}
    SerialPlant bus(&twin,twin.configurationRoot());
    if(!bus.configError().isEmpty()){qCritical().noquote()<<bus.configError();return 13;}
    twin.attachPlant(&bus);
    SerialMaster master(&twin,bus.protocol(),bus.registry());
    AiController ai(&twin,&master);
    const bool sender=args.contains("--sender");
    if(sender&&args.contains("--ai-button-stability-test")){
        master.enableTestTransport();
        QObject::connect(&master,&SerialMaster::requestReady,&app,[&](const QByteArray &request){
            QByteArray reply=request.left(2)+char(2)+Rtu::words({1});
            QTimer::singleShot(35,&app,[&,reply]{master.receiveBytes(Rtu::seal(reply));});
        });
    }
    bus.setSerialMode(!sender&&!args.contains("--offline")&&!args.contains("--smoke-test"));
    std::shared_ptr<SerialMaster> hardwareMaster;
    if(args.contains("--serial-smoke-test")||args.contains("--serial-gui-test")){
        QString receiver=optionText("--receiver-port","COM10"),transmitter=optionText("--sender-port","COM3");
        if(receiver==transmitter){qCritical()<<"Receiver and sender ports must differ";return 14;}
        if(!bus.openPort(receiver,115200)||!master.openPort(transmitter,115200)){qCritical().noquote()<<bus.status()<<master.status();return 14;}
        // Separate controller objects prevent sender UI recipe edits from mutating the receiver.
        auto senderTwin=std::make_shared<TwinController>(nullptr,false);senderTwin->loadProfile(1);senderTwin->selectRecipe(2,2);
        auto senderMaster=std::make_shared<SerialMaster>(senderTwin.get(),bus.protocol(),bus.registry());
        master.closePort();if(!senderMaster->openPort(transmitter,115200)){qCritical()<<senderMaster->status();return 14;}
        QTimer::singleShot(300,&app,[senderMaster]{senderMaster->startDemo();});
        QObject::connect(senderMaster.get(),&SerialMaster::completedDemo,&app,[&,senderMaster,senderTwin]{
            bool ok=senderMaster->failures()==0&&senderMaster->acknowledgements()>60&&bus.rxCount()>60&&twin.profileIndex()==1&&twin.flavorIndex()==2&&twin.formatIndex()==2&&twin.completed()==1&&twin.currentStep()==11&&qAbs(twin.fillFraction()-1)<.001&&qAbs(twin.axisX()+6.8)<.02;
            qInfo().noquote()<<(ok?"PASS: physical COM10/COM3 RTU link, CRC replies, grape 250mL full single-container cycle":"FAIL: physical serial state mismatch")<<"RX"<<bus.rxCount()<<"ACK"<<senderMaster->acknowledgements()<<"CRC"<<bus.crcErrors()<<"stage"<<twin.currentStep()<<"fill"<<twin.fillFraction()<<"count"<<twin.completed();
            app.exit(ok?0:15);
        });
        QTimer::singleShot(65000,&app,[&,senderMaster,senderTwin]{qCritical().noquote()<<"FAIL: physical serial test timed out"<<senderMaster->status()<<bus.status()<<senderMaster->frames().mid(0,6).join('\n');app.exit(16);});
        hardwareMaster=senderMaster;
        if(!args.contains("--serial-gui-test"))return app.exec();
    }
    if(args.contains("--port")){int baud=optionNumber("--baud");if(baud==0)baud=115200;if(sender){master.openPort(optionText("--port",master.portName()),baud);}else bus.openPort(optionText("--port",bus.portName()),baud);}
    QQmlApplicationEngine engine;
    QStringList qmlWarnings;
    QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError> &errors){for(const auto &e:errors)qmlWarnings<<e.toString();});
    engine.rootContext()->setContextProperty("twin",&twin);
    engine.rootContext()->setContextProperty("bus",&bus);
    engine.rootContext()->setContextProperty("master",&master);
    engine.rootContext()->setContextProperty("ai",&ai);
    engine.rootContext()->setContextProperty("demoSmallWindow",args.contains("--small-window"));
    qInfo()<<"Loading demo QML";
    engine.load(QUrl(sender?"qrc:/qml/Sender.qml":"qrc:/qml/Main.qml"));
    if(engine.rootObjects().isEmpty())return 3;
    auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!window)return 4;
    qInfo()<<"Demo window ready"<<window->size();
    if(sender&&args.contains("--ai-button-stability-test")){
        auto stable=std::make_shared<bool>(true);auto samples=std::make_shared<int>(0);
        auto button=window->findChild<QObject*>("aiGenerateButton");
        auto sampleTimer=new QTimer(&app);sampleTimer->setInterval(5);
        QObject::connect(sampleTimer,&QTimer::timeout,&app,[&,stable,samples,button]{
            ++*samples;*stable&=button&&button->property("enabled").toBool()&&!master.busy()&&master.status()==QStringLiteral("串口已打开 · 自动轮询设备");
        });sampleTimer->start();
        QTimer::singleShot(3000,&app,[&,stable,samples]{
            bool ok=*stable&&*samples>=100&&master.acknowledgements()>=20&&master.failures()==0&&qmlWarnings.isEmpty();
            qInfo().noquote()<<(ok?"PASS: AI generate button and status stay stable across delayed background poll replies":"FAIL: background poll flickers AI controls")<<"samples"<<*samples<<"ACK"<<master.acknowledgements();app.exit(ok?0:38);
        });
    }
    if(sender&&args.contains("--ai-report")){
        if(args.contains("--ai-playback-test"))ai.enableAutomatedTest();
        ai.previewReport(optionText("--ai-report",QString()));
    }
    if(sender&&args.contains("--ai-playback-test")){
        auto testPhase=std::make_shared<int>(0);
        auto lastTraceStatus=std::make_shared<QString>();
        auto timer=new QTimer(&app);timer->setInterval(150);
        QObject::connect(timer,&QTimer::timeout,&app,[&,testPhase,lastTraceStatus]{
            QString traceStatus=QString::number(*testPhase)+" / "+ai.status()+" / "+master.status();
            if(*lastTraceStatus!=traceStatus){*lastTraceStatus=traceStatus;qInfo().noquote()<<"AI TEST:"<<traceStatus;}
            if(master.failures()>0){qCritical()<<"FAIL: AI playback serial"<<master.status();app.exit(32);return;}
            if(*testPhase==0&&!ai.busy()&&!ai.ready()){qCritical()<<"FAIL: AI report preview"<<ai.status();app.exit(36);return;}
            if(*testPhase==2&&!ai.busy()&&ai.gate()<4){qCritical()<<"FAIL: AI trace adapter"<<ai.status();app.exit(37);return;}
            if(*testPhase==0&&ai.ready()&&!ai.busy()&&!master.busy()){
                auto requirements=window->findChild<QObject*>("aiReviewRequirements");auto points=window->findChild<QObject*>("aiReviewPoints");auto button=window->findChild<QObject*>("aiApplyButton");
                if(!requirements||!points||!button){qCritical()<<"FAIL: AI review controls missing";app.exit(33);return;}
                requirements->setProperty("checked",true);points->setProperty("checked",true);
                if(!button->property("enabled").toBool()){qCritical()<<"FAIL: AI review gate disabled";app.exit(33);return;}
                *testPhase=1;QMetaObject::invokeMethod(button,"clicked");
            }
            if(*testPhase==1&&ai.applied()&&!ai.busy()&&!master.busy()){*testPhase=2;ai.replay(true);}
        });timer->start();
        QObject::connect(&master,&SerialMaster::completedDemo,&app,[&]{
            QTimer::singleShot(100,&app,[&]{
                QString capture=optionText("--capture",QString());auto shot=window->grabWindow();QSet<QRgb> colors;
                if(!shot.isNull())for(int y=0;y<shot.height();y+=19)for(int x=0;x<shot.width();x+=19)colors.insert(shot.pixel(x,y));
                bool ok=ai.applied()&&ai.gate()==4&&master.failures()==0&&master.acknowledgements()>60&&qmlWarnings.isEmpty()&&colors.size()>8&&(capture.isEmpty()||shot.save(capture));
                qInfo().noquote()<<(ok?"PASS: original JSON activated, Qt review controls, validated trace over real serial (TEST_ONLY review)":"FAIL: AI native UI playback")<<"ACK"<<master.acknowledgements()<<qmlWarnings.join('\n');app.exit(ok?0:34);
            });
        });
        QTimer::singleShot(110000,&app,[&]{qCritical()<<"FAIL: AI native playback timeout"<<ai.status()<<master.status();app.exit(35);});
    }
    if(args.contains("--sender-demo-test")){
        QObject::connect(&master,&SerialMaster::changed,&app,[&]{if(master.failures()>0){qCritical().noquote()<<"FAIL: sender request"<<master.status()<<master.frames().mid(0,8).join('\n');app.exit(25);}});
        QTimer::singleShot(500,&app,[&]{master.startDemo();});
        QObject::connect(&master,&SerialMaster::completedDemo,&app,[&]{
            QString path=optionText("--capture",QString());QImage shot=window->grabWindow();QSet<QRgb> colors;
            if(!shot.isNull())for(int y=0;y<shot.height();y+=17)for(int x=0;x<shot.width();x+=17)colors.insert(shot.pixel(x,y));
            bool saved=path.isEmpty()||(!shot.isNull()&&colors.size()>8&&shot.save(path));
            bool ok=master.failures()==0&&master.acknowledgements()>60&&qmlWarnings.isEmpty()&&saved;
            qInfo().noquote()<<(ok?"PASS: independent sender UI, real RTU requests and matching replies":"FAIL: sender UI test")<<"ACK"<<master.acknowledgements()<<"errors"<<master.failures()<<qmlWarnings.join('\n');app.exit(ok?0:21);
        });
        QTimer::singleShot(90000,&app,[&]{qCritical()<<"FAIL: sender timed out"<<master.status();app.exit(22);});
    }
    if(args.contains("--receiver-serial-test")){
        auto captured=std::make_shared<bool>(false);
        QObject::connect(&bus,&SerialPlant::changed,&app,[&,captured]{if(!*captured&&bus.stage()==5&&bus.fillFraction()>.35){*captured=true;twin.selectDevice("FlowMeter");QTimer::singleShot(25,&app,[&]{QString path=optionText("--capture",QString());if(!path.isEmpty()&&!window->grabWindow().save(path)){qCritical()<<"FAIL: receiver screenshot";app.exit(23);}});}});
        auto finishCheck=new QTimer(&app);finishCheck->setInterval(200);
        QObject::connect(finishCheck,&QTimer::timeout,&app,[&,captured]{
            if(twin.completed()!=1||twin.currentStep()!=11||bus.healthy()||bus.rxCount()<60)return;
            bool metadata=true;
            QString active=optionText("--ai-active-file",QString());
            if(!active.isEmpty()){
                QString error;auto activation=Rtu::object(active,error);auto candidate=Rtu::object(activation.value("candidate").toString()+"/system_variables.json",error);
                auto rows=candidate.value("recipe_lookup").toArray();
                if(rows.size()!=3||rows.at(2).toArray().size()!=3)metadata=false;
                else metadata=error.isEmpty()&&candidate.value("recipes").toArray().at(rows.at(2).toArray().at(2).toInt()).toObject().toVariantMap()==twin.recipe();
                qInfo()<<"AI receiver candidate metadata"<<metadata<<"fill point"<<twin.recipe().value("fill_work")<<"finished point"<<twin.recipe().value("finished_work");
            }
            bool ok=*captured&&bus.crcErrors()==0&&qAbs(twin.fillFraction()-1)<.001&&qAbs(twin.axisX()+6.8)<.02&&qmlWarnings.isEmpty()&&metadata;
            qInfo().noquote()<<(ok?"PASS: independent receiver UI, complete real serial cycle, physical link timeout freeze":"FAIL: receiver serial UI test")<<"RX"<<bus.rxCount()<<"CRC"<<bus.crcErrors()<<"count"<<twin.completed()<<"stage"<<twin.currentStep()<<"healthy"<<bus.healthy()<<qmlWarnings.join('\n');app.exit(ok?0:24);
        });finishCheck->start();
        QTimer::singleShot(100000,&app,[&]{qCritical()<<"FAIL: receiver did not finish"<<bus.status()<<twin.currentStep()<<twin.completed();app.exit(24);});
    }
    if(args.contains("--ui-smoke-test")){
        QTimer::singleShot(1800,&app,[&]{
            QString capture=optionText("--capture",QString());
            if(!qmlWarnings.isEmpty()||(!capture.isEmpty()&&!window->grabWindow().save(capture))){qCritical().noquote()<<"FAIL: UI render"<<qmlWarnings.join('\n');app.exit(17);return;}
            qInfo()<<"PASS: serial UI rendered without QML warnings";app.exit(0);
        });
    }
    if(hardwareMaster){
        auto captured=std::make_shared<bool>(false);
        QObject::connect(&bus,&SerialPlant::changed,&app,[&,captured]{
            if(!*captured&&bus.stage()==5&&bus.fillFraction()>.35){
                *captured=true;twin.selectDevice("FlowMeter");
                QTimer::singleShot(25,&app,[&]{QString path=optionText("--capture",QString());if(!path.isEmpty()&&!window->grabWindow().save(path)){qCritical()<<"FAIL: serial scene capture";app.exit(18);}});
            }
        });
        QObject::connect(hardwareMaster.get(),&SerialMaster::completedDemo,&app,[&,captured]{if(!*captured||!qmlWarnings.isEmpty()){qCritical().noquote()<<"FAIL: serial scene UI"<<qmlWarnings.join('\n');app.exit(19);}});
    }
    if(args.contains("--smoke-test")){
        auto interactionOk=std::make_shared<bool>(true);
        QTimer::singleShot(400,&app,[&,interactionOk]{
            const int profile=twin.profileIndex(),flavor=twin.flavorIndex(),specification=twin.formatIndex();
            auto button=window->findChild<QObject*>("profileButton");
            if(!button){qCritical()<<"Missing profile button";app.exit(12);return;}
            QMetaObject::invokeMethod(button,"clicked");
            bool changed=twin.profileIndex()==1-profile&&twin.recipeCount()==(profile?6:9)&&twin.flavorStations().size()==3;
            QMetaObject::invokeMethod(button,"clicked");
            QVariant chosen;
            QMetaObject::invokeMethod(window,"smokeSelectRecipe",Q_RETURN_ARG(QVariant,chosen),Q_ARG(QVariant,QVariant(flavor)),Q_ARG(QVariant,QVariant(specification)));
            *interactionOk=changed&&chosen.toBool()&&twin.profileIndex()==profile&&twin.deviceCount()==21;
            if(!*interactionOk){qCritical()<<"Profile / recipe controls failed";app.exit(12);return;}
            qInfo()<<"SMOKE: configuration switch and recipe selectors passed"<<twin.recipeCaption();
        });
        QTimer::singleShot(1200,&app,[&]{qInfo()<<"SMOKE: start filling";twin.jumpToStep(5);twin.start();});
        QTimer::singleShot(2800,&app,[&,interactionOk]{
            QVariant picked;
            QMetaObject::invokeMethod(window,"smokePickDevice",Q_RETURN_ARG(QVariant,picked));
            *interactionOk=*interactionOk&&!picked.toString().isEmpty()&&twin.selectedId()==picked.toString();
            qInfo()<<"SMOKE: mesh ray pick"<<picked.toString()<<*interactionOk;
            twin.pause();twin.selectDevice("FlowMeter");
            auto button=window->findChild<QObject*>("startButton");
            if(!button){qCritical()<<"Missing start button";app.exit(5);return;}
            QMetaObject::invokeMethod(button,"clicked");
        });
        QTimer::singleShot(4000,&app,[&,interactionOk]{
            qInfo()<<"SMOKE: capture"<<window->isExposed()<<window->size();
            if(args.contains("--small-window")&&(window->width()!=1120||window->height()!=840)){qCritical()<<"Small window dimensions changed unexpectedly";app.exit(11);return;}
            int index=args.indexOf("--capture");
            if(index>=0&&index+1<args.size()){
                QImage shot=window->grabWindow();if(shot.isNull()||!shot.save(args[index+1])){qCritical()<<"Screenshot failed";app.exit(6);return;}
            }
            if(!*interactionOk){qCritical()<<"Mesh picking did not select a device";app.exit(8);return;}
            twin.injectFault("offline");
            if(twin.running()||twin.device("FlowMeter").value("quality").toString().startsWith("Bad")==false){qCritical()<<"Offline UI state failed";app.exit(9);return;}
        });
        QTimer::singleShot(4600,&app,[&,interactionOk]{
            int index=args.indexOf("--capture");
            if(index>=0&&index+1<args.size()){
                QString path=args[index+1];path.replace(".png","-fault.png");window->grabWindow().save(path);
            }
            twin.clearFault();
            if(twin.running()){qCritical()<<"Recovery must remain paused";app.exit(10);return;}
            if(!qmlWarnings.isEmpty()){qCritical().noquote()<<qmlWarnings.join('\n');app.exit(7);return;}
            qInfo()<<"PASS: configuration switching, recipe selectors, rendered scene, mesh picking, start, telemetry, offline and recovery";app.exit(0);
        });
    }
    if(args.contains("--autoplay"))QTimer::singleShot(700,&twin,&TwinController::start);
    return app.exec();
}
