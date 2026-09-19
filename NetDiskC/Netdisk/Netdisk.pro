QT       += core gui concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(./netapi/netapi.pri)
INCLUDEPATH += ./netapi \
               ./netapi/net \
               ./netapi/mediator

include(./md5/md5.pri)
INCLUDEPATH +=./md5/

include(./sqlapi/sqlapi.pri)
INCLUDEPATH +=./sqlapi/

# ---- Video Player Integration ----
include(./videoplayer/opengl/opengl.pri)
INCLUDEPATH += ./videoplayer \
               ./videoplayer/opengl \
               $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/include \
               $$PWD/../MediaPlayer/MediaPlayer/SDL2-2.0.10/include

LIBS += $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/avcodec.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/avdevice.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/avfilter.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/avformat.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/avutil.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/postproc.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/swresample.lib \
        $$PWD/../MediaPlayer/MediaPlayer/ffmpeg-4.2.2/lib/swscale.lib \
        $$PWD/../MediaPlayer/MediaPlayer/SDL2-2.0.10/lib/x86/SDL2.lib

SOURCES += \
    ckernel.cpp \
    logindialog.cpp \
    main.cpp \
    maindialog.cpp \
    mytablewidgetitem.cpp \
    videoplayer/playerdialog.cpp \
    videoplayer/videoplayer.cpp \
    videoplayer/PacketQueue.cpp

HEADERS += \
    ckernel.h \
    common.h \
    logindialog.h \
    maindialog.h \
    mytablewidgetitem.h \
    videoplayer/playerdialog.h \
    videoplayer/videoplayer.h \
    videoplayer/PacketQueue.h

FORMS += \
    logindialog.ui \
    maindialog.ui \
    videoplayer/playerdialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resource.qrc

#DISTFILES += \
#    ../../../NetDisk/ui_assets/face/btn_avatar_a28.png \
#    ../../../NetDisk/ui_assets/images/Cancel_32x32.png \
#    ../../../NetDisk/ui_assets/images/CheckBox_32x32.png \
#    ../../../NetDisk/ui_assets/images/addFile.png \
#    ../../../NetDisk/ui_assets/images/check.png \
#    ../../../NetDisk/ui_assets/images/delete.png \
#    ../../../NetDisk/ui_assets/images/file.png \
#    ../../../NetDisk/ui_assets/images/folder.png \
#    ../../../NetDisk/ui_assets/images/logo.ico \
#    ../../../NetDisk/ui_assets/images/pause.png \
#    ../../../NetDisk/ui_assets/images/play.png \
#    ../../../NetDisk/ui_assets/images/search.png \
#    ../../../NetDisk/ui_assets/images/share.png \
#    ../../../NetDisk/ui_assets/images/transmit.ico \
#    ../../../NetDisk/ui_assets/tb/baidu.png

