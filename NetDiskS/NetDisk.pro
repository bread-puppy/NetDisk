TEMPLATE = app
CONFIG += console c++11
QMAKE_CXXFLAGS += -std=c++11
QMAKE_CXXFLAGS_WARN_ON += -Wno-reorder
QMAKE_CXXFLAGS += -Wno-unused-parameter
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += ./include
LIBS+=-lpthread -lmysqlclient

SOURCES += \
    src/Mysql.cpp \
    src/TCPKernel.cpp \
    src/Thread_pool.cpp \
    src/block_epoll_net.cpp \
    src/clogic.cpp \
    src/err_str.cpp \
    src/main.cpp \
    src/md5.cpp

HEADERS += \
    include/Mysql.h \
    include/TCPKernel.h \
    include/Thread_pool.h \
    include/block_epoll_net.h \
    include/clogic.h \
    include/err_str.h \
    include/md5.h \
    include/packdef.h

