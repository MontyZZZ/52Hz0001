QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    paint/drawarrow.cpp \
    paint/drawscene.cpp \
    paint/drawtext.cpp \
    record/screenrecord.cpp \
    widget.cpp

HEADERS += \
    paint/drawarrow.h \
    paint/drawscene.h \
    paint/drawtext.h \
    record/screenrecord.h \
    widget.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32: LIBS += -L$$PWD/lib/ -llibavcodec.dll
       LIBS += -L$$PWD/lib/ -llibavdevice.dll
       LIBS += -L$$PWD/lib/ -llibavfilter.dll
       LIBS += -L$$PWD/lib/ -llibavformat.dll
       LIBS += -L$$PWD/lib/ -llibavutil.dll
       LIBS += -L$$PWD/lib/ -llibpostproc.dll
       LIBS += -L$$PWD/lib/ -llibswresample.dll
       LIBS += -L$$PWD/lib/ -llibswscale.dll

INCLUDEPATH += $$PWD/include
DEPENDPATH += $$PWD/include

RESOURCES += \
    rs.qrc
