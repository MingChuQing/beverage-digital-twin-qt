#include "twincontroller.h"
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
    app.setOrganizationName("IndustrialConfigEngine");app.setApplicationName("BeverageTwinDemo");app.setApplicationVersion("0.2.0");
    const auto args=app.arguments();
    if(args.contains("--self-test")){QString error;bool ok=TwinController::selfTest(&error);qInfo().noquote()<< (ok?"PASS: 6/9 recipe configuration, shared filling heads, point mapping, batch simulation, pause, offline and faults":"FAIL: "+error);return ok?0:1;}
    QQuickStyle::setStyle("Fusion");
    TwinController twin;
    if(!twin.configError().isEmpty()){qCritical().noquote()<<twin.configError();return 2;}
    if(args.contains("--extended")&&!twin.loadProfile(1))return 2;
    auto optionNumber=[&args](const QString &name){int i=args.indexOf(name);if(i<0)return 0;bool valid=false;int value=args.value(i+1).toInt(&valid);return valid?value:-1;};
    if(!twin.selectRecipe(optionNumber("--flavor"),optionNumber("--format"))){qCritical()<<"Unsupported flavor / format in selected profile";return 2;}
    QQmlApplicationEngine engine;
    QStringList qmlWarnings;
    QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError> &errors){for(const auto &e:errors)qmlWarnings<<e.toString();});
    engine.rootContext()->setContextProperty("twin",&twin);
    engine.rootContext()->setContextProperty("demoSmallWindow",args.contains("--small-window"));
    qInfo()<<"Loading demo QML";
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if(engine.rootObjects().isEmpty())return 3;
    auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!window)return 4;
    qInfo()<<"Demo window ready"<<window->size();
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
