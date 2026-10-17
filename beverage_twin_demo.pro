QT += core gui qml quick quickcontrols2 quick3d
CONFIG += c++14
# Qt 5's MinGW tools cannot round-trip non-ASCII absolute paths in moc predefs.
win32-g++:CONFIG += no_moc_predefs
TEMPLATE = app
TARGET = BeverageTwinDemo
SOURCES += src/main.cpp src/twincontroller.cpp
HEADERS += src/twincontroller.h
RESOURCES += resources.qrc
DISTFILES += $$files(qml/*.qml) $$files(config/*.json, true)
# Explicit relative dependencies also work when the source directory has Chinese characters.
resource_deps.target = qrc_resources.cpp
for(input, DISTFILES) {
    resource_deps.depends += $$relative_path($$absolute_path($$input), $$OUT_PWD)
}
QMAKE_EXTRA_TARGETS += resource_deps
DESTDIR = bin
win32:QMAKE_CXXFLAGS += -finput-charset=UTF-8
msvc { QMAKE_CXXFLAGS -= -finput-charset=UTF-8; QMAKE_CXXFLAGS += /utf-8 }
