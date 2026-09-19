#include "ckernel.h"
#include<QDebug>
#include<QCoreApplication>
#include<QFileInfo>
#include<QSettings> //配置文件使用的类
#include"TcpClientMediator.h"
#include"TcpServerMediator.h"
#include<QMessageBox>
#include <map>
#include"packdef.h"
#include"md5.h"
#include<QFileInfo>
#include<QDateTime>

#include <QThread>
#include <QTimer>
#include <QMetaObject>  // invokeMethod需要
#include <QMessageBox>
#include<QTextCodec>
#include <QtConcurrent>  // 用于异步MD5计算
#include <QFutureWatcher>

#include<QDir>
#include<QCoreApplication>

#define NetMap(a) m_netPackMap[a-_DEF_PACK_BASE]

//客户端先计算密码 MD5，服务端收到后再按固定盐计算
static bool checkPaused(FileInfo& info)
 {
     return info.isPause != 0;
 }


void Ckernel::setNetPackMap()
{
    //先清空数组
    memset(m_netPackMap,0,sizeof(PFUN)*_DEF_PACK_COUNT);
    //协议映射表 key协议头偏移量  value函数指针，通过协议头找到对应处理函数
    //m_netPackMap[_DEF_PACK_LOGIN_RS-_DEF_PACK_BASE]=&Ckernel::slot_dealLoginRs;
    NetMap(_DEF_PACK_REGISTER_RS)=&Ckernel::slot_dealRegisterRs;
    NetMap(_DEF_PACK_LOGIN_RS)=&Ckernel::slot_dealLoginRs;
    NetMap(_DEF_PACK_UPLOAD_FILE_RS)=&Ckernel::slot_dealUploadFileRs;
    NetMap(_DEF_PACK_FILE_CONTENT_RS)=&Ckernel::slot_dealFileContentRs;
    NetMap(_DEF_PACK_GET_FILE_INFO_RS)=&Ckernel::slot_dealGetFileInfoRs;
    NetMap(_DEF_PACK_FILE_HEADER_RQ)=&Ckernel::slot_dealFileHeaderRq;
    NetMap(_DEF_PACK_FILE_CONTENT_RQ)=&Ckernel::slot_dealFileContentRq;
    NetMap(_DEF_PACK_ADD_FOLDER_RS)=&Ckernel::slot_dealAddFolderRs;
    NetMap(_DEF_PACK_QUICK_UOLOAD_RS)=&Ckernel::slot_dealQuickUploadRs;
    NetMap(_DEF_PACK_SHARE_FILE_RS)=&Ckernel::slot_dealShareFileRs;
    NetMap(_DEF_PACK_MY_SHARE_RS)=&Ckernel::slot_dealMyShareRs;
    NetMap(_DEF_PACK_GET_SHARE_RS)=&Ckernel::slot_dealGetShareRs;
    NetMap(_DEF_PACK_FOLDER_HEADER_RQ)=&Ckernel::slot_dealFolderHeadRq;
    NetMap(_DEF_PACK_DELETE_FILE_RS)=&Ckernel::slot_dealDeleteFileRs;
    NetMap(_DEF_PACK_CONTINUE_UPLOAD_RS)=&Ckernel::slot_dealContinueUploadRs;
    NetMap(_DEF_PACK_CONTINUE_DOWNLOAD_RS)=&Ckernel::slot_dealContinueDownloadRs;  //下载续传回复
    NetMap(_DEF_PACK_HEARTBEAT_RQ) = &Ckernel::slot_dealHeartbeatRq;
    NetMap(_DEF_PACK_HEARTBEAT_RS) = &Ckernel::slot_dealHeartbeatRs;
    NetMap(_DEF_PACK_CHUNK_UPLOAD_RS) = &Ckernel::slot_dealChunkUploadRs;
    NetMap(_DEF_PACK_CHUNK_DOWNLOAD_RS) = &Ckernel::slot_dealChunkDownloadRs;
    NetMap(_DEF_PACK_SEARCH_FILE_RS) = &Ckernel::slot_dealSearchFileRs;
    NetMap(_DEF_PACK_FAVORITE_FILE_RS) = &Ckernel::slot_dealFavoriteFileRs;
    NetMap(_DEF_PACK_GET_FAVORITES_RS) = &Ckernel::slot_dealGetFavoritesRs;
    NetMap(_DEF_PACK_RECYCLE_FILE_RS) = &Ckernel::slot_dealRecycleFileRs;
    NetMap(_DEF_PACK_GET_RECYCLE_RS) = &Ckernel::slot_dealGetRecycleRs;
    NetMap(_DEF_PACK_RESTORE_FILE_RS) = &Ckernel::slot_dealRestoreFileRs;
    NetMap(_DEF_PACK_BROWSE_SHARE_RS) = &Ckernel::slot_dealBrowseShareRs;
    NetMap(_DEF_PACK_DELETE_FOREVER_RS) = &Ckernel::slot_dealDeleteForeverRs;
}

void Ckernel::setSystemPath()  //系统路径组成：./NetDisk +dir +name
{
    QString  path=QCoreApplication::applicationDirPath()+"/NetDisk";
    QDir dir;
    //没有文件夹 创建
    if(!dir.exists(path))
        dir.mkdir(path);  //只能创建一层
    //默认路径
    m_sysPath=path;
}

void Utf8ToGB2312( char* gbbuf , int nlen , const QString& utf8)
{
    //转码的对象
    QTextCodec * gb2312code = QTextCodec::codecForName( "gb2312");
    //QByteArray char 类型数组的封装类 里面有很多关于转码 和 写IO的操作
    QByteArray ba = gb2312code->fromUnicode( utf8 );// Unicode -> 转码对象的字符集

    strcpy_s ( gbbuf , nlen , ba.data() );
}

static std::string getFileMD5(QString path){  //获取文件MD5
    FILE* pFile=nullptr;  //打开文件，读取文件内容，读到MD5类，生成MD5
    //fopen如果有中文，支持ANSI编码，使用ascii码
    //path里是utf8(qt默认的)编码
    char buf[1000]="";
    Utf8ToGB2312(buf,1000,path);
    pFile=fopen(buf,"rb");  //二进制只读
    if(!pFile){
        qDebug()<<"file md5 open fail";
        return string();
    }
    int len=0;
    MD5 md;
    char bigBuf[65536];
    do{
        len=fread(bigBuf,1,sizeof(bigBuf),pFile);
        md.update(bigBuf,len);
    }while(len>0);
    fclose(pFile);
    qDebug()<<"file md5:"<<md.toString().c_str();
    return md.toString();
}
void Ckernel::SendData(char *buf, int len)
{
    m_tcpClient->SendData(0,buf,len);
}

Ckernel::Ckernel(QObject *parent) : QObject(parent),m_id(0),m_curDir("/"),m_quit(false),m_tcpClientPool(nullptr),m_deletingFromRecycle(false),m_activeUploads(0),m_pendingShareCode(0)
{
    //设置协议映射
    setNetPackMap();
    //加载配置文件
    loadIniFile();

    //设置默认路径
    setSystemPath();

    m_tcpClient.reset(new TcpClientMediator);
    connect(m_tcpClient.data(),SIGNAL(SIG_ReadyData(uint,char*,int))
            ,this,SLOT(slot_dealClientData(uint,char*,int)));

    if (m_tcpClient->OpenNet(m_ip.toStdString().c_str(),m_port.toInt())) {  //客户端连接真实地址
        m_heartbeatTimer = new QTimer(this);
        connect(m_heartbeatTimer, &QTimer::timeout, this, &Ckernel::slot_sendHeartbeat);
        m_heartbeatTimer->start(30000);
    }
    //m_tcpClient->OpenNet("127.0.0.1",8004);  //客户端连接真实地址

    m_loginDialog.reset(new LoginDialog);
    connect(m_loginDialog.data(),SIGNAL(SIG_registerCommit(QString,QString,QString)),this,SLOT(slot_registerCommit(QString,QString,QString)));
    connect(m_loginDialog.data(),SIGNAL(SIG_loginCommit(QString,QString)),this,SLOT(slot_loginCommit(QString,QString)));

    m_loginDialog->show();

    m_mainDialog.reset(new MainDialog); //new出对象

    connect(m_mainDialog.data(),SIGNAL(SIG_close()),this,SLOT(slot_destroy()));  //谁发出信号，发送的信号，谁去接收信号，接收信号之后要执行的槽函数
    connect(m_mainDialog.data(),SIGNAL(SIG_uploadFile(QString,QString)),this,SLOT(slot_uploadFile(QString,QString)));
    connect(this,SIGNAL(SIG_updateUploadFileProgress(int,int)),m_mainDialog.data(),SLOT(slot_updateUploadFileProgress(int,int)));
    //m_mainDialog->show();  //显示对象
    connect(m_mainDialog.data(),SIGNAL(SIG_downloadFile(int,QString)),this,SLOT(slot_downloadFile(int,QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_downloadFolder(int,QString)),this,SLOT(slot_downloadFolder(int,QString)));
    connect(this,SIGNAL(SIG_updateDownloadFileProgress(int,int)),m_mainDialog.data(),SLOT(slot_updateDownloadFileProgress(int,int)));
    connect(m_mainDialog.data(),SIGNAL(SIG_addFolder(QString,QString)),this,SLOT(slot_addFolder(QString,QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_changeDir(QString)),this,SLOT(slot_changeDir(QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_uploadFolder(QString,QString)),this,SLOT(slot_uploadFolder(QString,QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_shareFile(QVector<int>,QString,QString)),this,SLOT(slot_shareFile(QVector<int>,QString,QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_getshareByLink(int,QString,QString)),this,SLOT(slot_getshareByLink(int,QString,QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_deleteFile(QVector<int>,QString)),this,SLOT(slot_deleteFile(QVector<int>,QString)));
    connect(m_mainDialog.data(),SIGNAL(SIG_setUploadPause(int,int)),this,SLOT(slot_setUploadPause(int,int)));
    connect(m_mainDialog.data(),SIGNAL(SIG_setDownloadPause(int,int)),this,SLOT(slot_setDownloadPause(int,int)));
    connect(this,SIGNAL(SIG_videoReady(QString)),m_mainDialog.data(),SLOT(slot_onVideoReady(QString)));
    connect(m_mainDialog.data(), SIGNAL(SIG_searchFile(QString)),
              this, SLOT(slot_searchFile(QString)));
    connect(m_mainDialog.data(), SIGNAL(SIG_favoriteFile(QVector<int>,QString,bool)),
            this, SLOT(slot_favoriteFile(QVector<int>,QString,bool)));
    connect(m_mainDialog.data(), SIGNAL(SIG_recycleFile(QVector<int>,QString)),
            this, SLOT(slot_recycleFile(QVector<int>,QString)));
    connect(m_mainDialog.data(), SIGNAL(SIG_getFavorites()),
            this, SLOT(slot_getFavorites()));
    connect(m_mainDialog.data(), SIGNAL(SIG_getRecycle()),
            this, SLOT(slot_getRecycle()));
    connect(m_mainDialog.data(), SIGNAL(SIG_restoreFile(QVector<int>)),
            this, SLOT(slot_restoreFile(QVector<int>)));
    connect(m_mainDialog.data(), SIGNAL(SIG_deleteForever(QVector<int>)),
            this, SLOT(slot_deleteForever(QVector<int>)));
    connect(m_mainDialog.data(), SIGNAL(SIG_browseShare(int,QString,QString)),
            this, SLOT(slot_browseShare(int,QString,QString)));
    connect(m_mainDialog.data(), SIGNAL(SIG_downloadShareFile(int,int,QString)),
            this, SLOT(slot_downloadShareFile(int,int,QString)));
    connect(m_mainDialog.data(), SIGNAL(SIG_saveObtainedShare(int,QString,QString)),
            this, SLOT(slot_saveObtainedShare(int,QString,QString)));
    connect(m_mainDialog.data(), SIGNAL(SIG_loadObtainedShares()),
            this, SLOT(slot_loadObtainedShares()));
    connect(m_mainDialog.data(), SIGNAL(SIG_clearUploadTasks()),
            this, SLOT(slot_clearUploadTasks()));
}

void Ckernel::loadIniFile()
{
    //默认值
    m_ip="192.168.111.128";
    m_port="8004";

    //获取exe目录 C://build-debug
    QString path=QCoreApplication::applicationDirPath()+"/config.ini";
    //根据目录，看目录是否存在，存在加载，不存在创建并且写入默认值
    QFileInfo info(path);
    if(info.exists()){
        //存在，读取
        QSettings setting(path,QSettings::IniFormat);
        //打开组
        setting.beginGroup("net");
        QVariant strIP=setting.value("ip","");
        QVariant strPort=setting.value("port","");
        if(!strIP.toString().isEmpty()) m_ip=strIP.toString();
        if(!strPort.toString().isEmpty()) m_port=strPort.toString();
        //关闭组
        setting.endGroup();
    }else{ //不存在
        QSettings setting(path,QSettings::IniFormat); //没有会创建
        //打开组
        setting.beginGroup("net");
        //设置key value
        setting.setValue("ip",m_ip);
        setting.setValue("port",m_port);
        //关闭组
        setting.endGroup();
    }
    qDebug()<<"ip:"<<m_ip<<"port:"<<m_port;
}

void Ckernel::slot_destroy(){
    m_quit=true;
    qDebug()<<__func__;
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    m_tcpClient->CloseNet();
    m_tcpClient.reset();
    if(m_tcpClientPool){
        m_tcpClientPool->closeAll();
        m_tcpClientPool.reset();
    }
    m_mainDialog.reset();
    m_loginDialog.reset();
}

void Ckernel::slot_sendHeartbeat()
{
    if (!m_tcpClient || !m_tcpClient->IsConnected())
        return;

    //每个请求都要有回复，没回的先记下来
    //发下一个前看看有没有卡住
    if (m_heartbeatPendingSeq != 0)
        ++m_heartbeatMissed;

    STRU_HEARTBEAT_RQ rq;
    rq.seq = ++m_heartbeatSeq;
    if (m_tcpClient->SendData(0, (char*)&rq, sizeof(rq))) {
        m_heartbeatPendingSeq = rq.seq;
    } else {
        m_heartbeatPendingSeq = 0;
        qWarning() << "heartbeat send failed, seq" << rq.seq;
    }

    if (m_heartbeatMissed >= 3)
        qWarning() << "heartbeat response timeout, missed" << m_heartbeatMissed
                   << "consecutive responses";
}

void Ckernel::slot_registerCommit(QString tel, QString password, QString name)
{
    if (!m_tcpClient->IsConnected()) {
        QMessageBox::about(m_loginDialog.data(), "提示", "服务器未连接，请检查网络");
        return;
    }
    STRU_REGISTER_RQ rq;
    strcpy(rq.tel,tel.toStdString().c_str());
    std::string strPasswordMD5 = MD5(password.toStdString()).toString();
    strcpy(rq.password,strPasswordMD5.c_str());
    std::string strName=name.toStdString();
    strcpy(rq.name,strName.c_str());
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_loginCommit(QString tel, QString password)
{
    if (!m_tcpClient->IsConnected()) {
        QMessageBox::about(m_loginDialog.data(), "提示", "服务器未连接，请检查网络");
        return;
    }
    STRU_LOGIN_RQ rq;
    strcpy(rq.tel,tel.toStdString().c_str());
    rq.type = _DEF_PACK_LOGIN_RQ;
    std::string strPasswordMD5 = MD5(password.toStdString()).toString();
    strcpy(rq.password,strPasswordMD5.c_str());
    SendData((char*)&rq,sizeof (rq));
}

void Ckernel::slot_uploadFile(QString path, QString dir)  //上传文件槽函数
{
    QFileInfo qFileInfo(path);

    if(qFileInfo.size() < 1024*1024) {
        FileInfo info;
        info.absolutePath=path;
        info.dir=dir;
        info.md5=QString::fromStdString(getFileMD5(path));
        info.name=qFileInfo.fileName();
        info.size=qFileInfo.size();
        info.time=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        info.type="file";

        //先试mmap，不行再用FILE*
        char buf[1000]="";
        Utf8ToGB2312(buf,1000,path);
        info.qFile.reset(new QFile(path));
        if(info.qFile->open(QIODevice::ReadOnly)){
            info.mappedData=info.qFile->map(0, info.size);
            if(info.mappedData){
                info.useMmap=true;
                qDebug()<<"mmap upload success, size:"<<info.size;
            }else{
                info.qFile->close();
                info.qFile.clear();
                qDebug()<<"mmap failed, fallback to FILE*";
            }
        }else{
            info.qFile.clear();
        }
        if(!info.useMmap){
            info.pFile=fopen(buf,"rb");
            if(!info.pFile){
                qDebug()<<"file open fail";
                return;
            }
        }
        int timestamp=QDateTime::currentDateTime().toString("hhmmsszzz").toInt();
        while (m_mapTimeStampToFileinfo.count(timestamp)>0) {
            timestamp++;
        }
        info.timestamp=timestamp;
        qDebug()<<"timestamp:"<<timestamp;
        m_mapTimeStampToFileinfo[timestamp]=info;  //存储到map里 key 时间戳 value 文件信息
        STRU_UPLOAD_FILE_RQ rq;  //发上传文件请求
        //兼容中文
        std::string strDir=dir.toStdString();
        strcpy(rq.dir,strDir.c_str());
        std::string strName=info.name.toStdString();
        strcpy(rq.fileName,strName.c_str());
        strcpy(rq.fileType,"file");
        strcpy(rq.md5,info.md5.toStdString().c_str());
        rq.size=info.size;
        strcpy(rq.time,info.time.toStdString().c_str());
        rq.timestamp=timestamp;
        rq.userid=m_id;
        SendData((char*)&rq,sizeof(rq));
        m_activeUploads++;
    } else {
        QFuture<std::string> future = QtConcurrent::run([path]() {
            return getFileMD5(path);
        });

        //等异步计算结束
        QFutureWatcher<std::string>* watcher = new QFutureWatcher<std::string>(this);
        connect(watcher, &QFutureWatcher<std::string>::finished, this, [this, watcher, path, dir, qFileInfo]() {
            std::string md5 = watcher->result();
            watcher->deleteLater();

            FileInfo info;
            info.absolutePath=path;
            info.dir=dir;
            info.md5=QString::fromStdString(md5);
            info.name=qFileInfo.fileName();
            info.size=qFileInfo.size();
            info.time=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            info.type="file";

            char buf[1000]="";
            Utf8ToGB2312(buf,1000,path);
            info.qFile.reset(new QFile(path));
            if(info.qFile->open(QIODevice::ReadOnly)){
                info.mappedData=info.qFile->map(0, info.size);
                if(info.mappedData){
                    info.useMmap=true;
                }else{
                    info.qFile->close();
                    info.qFile.clear();
                }
            }else{
                info.qFile.clear();
            }
            if(!info.useMmap){
                info.pFile=fopen(buf,"rb");
                if(!info.pFile){
                    qDebug()<<"file open fail";
                    return;
                }
            }
            int timestamp=QDateTime::currentDateTime().toString("hhmmsszzz").toInt();
            while (m_mapTimeStampToFileinfo.count(timestamp)>0) {
                timestamp++;
            }
            info.timestamp=timestamp;
            m_mapTimeStampToFileinfo[timestamp]=info;

            STRU_UPLOAD_FILE_RQ rq;
            std::string strDir=dir.toStdString();
            strcpy(rq.dir,strDir.c_str());
            std::string strName=info.name.toStdString();
            strcpy(rq.fileName,strName.c_str());
            strcpy(rq.fileType,"file");
            strcpy(rq.md5,info.md5.toStdString().c_str());
            rq.size=info.size;
            strcpy(rq.time,info.time.toStdString().c_str());
            rq.timestamp=timestamp;
            rq.userid=m_id;
            SendData((char*)&rq,sizeof(rq));
            m_activeUploads++;
        });
        watcher->setFuture(future);
    }
}

void Ckernel::slot_uploadFolder(QString path, QString dir)
{
    qDebug()<<__func__;
    QFileInfo info(path);  //用于获取文件名字
    QDir dr(path);
    //当前文件夹的处理 addFolder
    slot_addFolder(info.fileName(),dir);
    //获取文件夹下所有文件的路径(所有信息)
    QFileInfoList lst= dr.entryInfoList(); //获取路径下所有文件的文件信息列表
    //遍历所有文件，加入上传队列（而非立即上传）
    QString newDir=dir+info.fileName()+"/";
    for(int i=0;i<lst.size();++i){
        QFileInfo file=lst.at(i);
        //如果是.继续
        if(file.fileName()==".") continue;
        //如果是..继续
        if(file.fileName()=="..") continue;
        //如果是文件 加入队列
        if(file.isFile()){
            qDebug()<<"queue file:"<<file.absoluteFilePath()<<"dir:"<<newDir;
            UploadTask task;
            task.path = file.absoluteFilePath();
            task.dir = newDir;
            m_uploadQueue.enqueue(task);
        }
        //如果是文件夹 slot_uploadFolder 递归
        if(file.isDir())
            slot_uploadFolder(file.absoluteFilePath(),newDir);
    }
    //启动队列处理（如果当前没有正在上传的文件）    processUploadQueue();
}

void Ckernel::slot_getCurDirFileList()
{
    //向服务器发送获取当前目录文件列表
    STRU_GET_FILE_INFO_RQ rq;
    rq.userid=m_id;
    //兼容中文
    std::string strDir=m_curDir.toStdString();
    strcpy(rq.dir,strDir.c_str());
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_downloadFile(int fileid, QString dir)
{
    //写请求
    STRU_DOWNLOAD_FILE_RQ rq;
    //兼容中文
    std::string strDir=dir.toStdString();
    strcpy(rq.dir,strDir.c_str());
    rq.fileid=fileid;
    int timestamp=QDateTime::currentDateTime().toString("hhmmsszzz").toInt();
    while(m_mapTimeStampToFileinfo.count(timestamp)>0)
        timestamp++;
    rq.timestamp=timestamp;
    rq.userid=m_id;
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_downloadFolder(int fileid, QString dir)
{
    STRU_DOWNLOAD_FOLDER_RQ rq;
    string strDir=dir.toStdString();
    strcpy(rq.dir,strDir.c_str());
    rq.fileid=fileid;
    int timestamp=QDateTime::currentDateTime().toString("hhmmsszzz").toInt();
    while(m_mapTimeStampToFileinfo.count(timestamp)>0)
        timestamp++;
    rq.timestamp=timestamp;
    rq.userid=m_id;
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_addFolder(QString name, QString dir)  //新建文件夹
{
    //发请求包
    STRU_ADD_FOLDER_RQ rq;
    string strDir=dir.toStdString();
    strcpy(rq.dir,strDir.c_str());
    string strName=name.toStdString();
    strcpy(rq.fileName,strName.c_str());
    string strTime=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString();
    strcpy(rq.time,strTime.c_str());
    rq.timestamp=QDateTime::currentDateTime().toString("hhmmsszzz").toInt();
    rq.userid=m_id;
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_changeDir(QString dir)
{
    //更新当前目录
    m_curDir=dir;
    //刷新列表
    m_mainDialog->slot_deleteAllFileInfo();
    slot_getCurDirFileList();
}

void Ckernel::slot_shareFile(QVector<int> fileidArray, QString dir, QString password)
{
    qDebug()<<__func__;
    //打包
    int packlen=sizeof(STRU_SHARE_FILE_RQ)+sizeof(int)*fileidArray.size();
    STRU_SHARE_FILE_RQ* rq=(STRU_SHARE_FILE_RQ*)malloc(packlen);
    rq->init();
    rq->itemCount=fileidArray.size();
    for(int i=0;i<fileidArray.size();++i)
        rq->fileidArray[i]=fileidArray[i];
    rq->userid=m_id;
    std::string strDir=dir.toStdString();
    strcpy(rq->dir,strDir.c_str());
    QString time=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    strcpy(rq->shareTime,time.toStdString().c_str());
    SendData((char*)rq,packlen);
    free(rq);
}

void Ckernel::slot_dealClientData(unsigned int lSendIP, char *buf, int nlen)  //客户端处理数据
{
    //QString str=QString("来自服务端:%1").arg(QString::fromStdString(buf));
    //QMessageBox::about(NULL,"提示",str);
    int type=*(int*)buf;
   //qDebug()<<__func__<<endl;
    //通过协议头 拿到处理函数并执行，使用数组要对其界限进行判断
    if(type>=_DEF_PACK_BASE&&type< _DEF_PACK_BASE+_DEF_PACK_COUNT){
        PFUN pf=NetMap(type);
        if(pf){  //函数指针的执行
            (this->*pf)(lSendIP,buf,nlen);
        }
    }
    //回收空间
    delete[] buf;
}

void Ckernel::slot_dealLoginRs(unsigned int lSendIp, char *buf, int nlen)
{
    qDebug()<<__func__;
    //拆包
    STRU_LOGIN_RS* rs=(STRU_LOGIN_RS*)buf;
    //根据不同结果有不同提示
    switch(rs->result){
    case tel_not_exist:
        QMessageBox::about(m_loginDialog.data(),"提示","手机号不存在，登录失败");
        break;
    case password_error:
        QMessageBox::about(m_loginDialog.data(),"提示","密码错误,登录失败");
        break;
    case login_success:
        //前台
        m_loginDialog->hide();
        m_mainDialog->show();
        //后台
        m_name=rs->name;
        m_id=rs->userid;
        m_mainDialog->slot_setInfo(m_name);
        m_mainDialog->m_sysPath=m_sysPath;  // 传递本地存储路径给 MainDialog
        //获取根目录下面文件列表
        m_curDir="/";
        slot_getCurDirFileList();

        //刷新 发获取请求
        slot_getMyShare();
        InitDatabase(m_id);
        QTimer::singleShot(200, this, [this](){
            m_mainDialog->slot_refreshObtainedShares();
        });
        QTimer::singleShot(100, this, [this](){
            initTcpClientPool();
        });
        break;
    }
}

void Ckernel::slot_dealRegisterRs(unsigned int lSendIp, char *buf, int nlen)
{
    qDebug()<<__func__;
    //拆包
    STRU_REGISTER_RS* rs=(STRU_REGISTER_RS*)buf;
    //根据不同结果有不同提示
    switch(rs->result){
    case tel_is_exist:
        QMessageBox::about(m_loginDialog.data(),"提示","手机号已存在，注册无效");
        break;
    case name_is_exist:
        QMessageBox::about(m_loginDialog.data(),"提示","昵称已存在，注册无效");
        break;
    case register_success:
        QMessageBox::about(m_loginDialog.data(),"提示","注册成功");
        break;
    }
}

void Ckernel::slot_dealUploadFileRs(unsigned int lSendIp, char *buf, int nlen)  //上传文件回复的处理
{
    //拆包
    STRU_UPLOAD_FILE_RS* rs=(STRU_UPLOAD_FILE_RS*)buf;
    rs->fileid;
    rs->result;
    rs->timestamp;
    //先看结果是否为真
    if(!rs->result){
        qDebug()<<"上传失败";
        m_activeUploads--;
        processUploadQueue();
        return;
    }
    //为真
    //获取文件信息
    if(m_mapTimeStampToFileinfo.count(rs->timestamp)==0){
        qDebug()<<"not found";
        m_activeUploads--;
        processUploadQueue();
        return;
    }
    FileInfo& info=m_mapTimeStampToFileinfo[rs->timestamp];
    info.fileid=rs->fileid;  //更新fileid
    //插入上传信息到”上传中”的控件里
    slot_writeUploadTask(info);
    m_mainDialog->slot_insertUploadFile(info);
    if (info.size >= CHUNK_THRESHOLD && m_tcpClientPool && m_tcpClientPool->isAllOpen()) {
        m_chunkUploadActive.insert(info.timestamp);
        m_chunkUploadFinished[info.timestamp] = 0;
        slot_uploadFileChunked(info);
        return;
    }
    //超大文件由四条连接分别发送各自区间；其余文件继续走单连接流程。    //发送文件块（内容）请求
    STRU_FILE_CONTENT_RQ rq;
    rq.fileid=rs->fileid;
    rq.timestamp=rs->timestamp;
    rq.userid=m_id;
    if(info.useMmap){
        int chunkSize=qMin(_DEF_BUFFER, info.size - info.pos);
        memcpy(rq.content, info.mappedData + info.pos, chunkSize);
        rq.len=chunkSize;
    }else{
        rq.len=fread(rq.content,1,_DEF_BUFFER,info.pFile);
    }
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_dealFileContentRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_FILE_CONTENT_RS* rs=(STRU_FILE_CONTENT_RS*)buf;
    //分片进度单独处理
    if (m_chunkUploadActive.contains(rs->timestamp))
        return;
    //找文件信息结构体
    if(m_mapTimeStampToFileinfo.count(rs->timestamp)==0){
        qDebug()<<"file not found";
        return;
    }
    FileInfo& info=m_mapTimeStampToFileinfo[rs->timestamp];
    //非阻塞暂停检查：暂停时直接返回不发下一块，等恢复时手动重发
    if(checkPaused(info)){
        return;
    }
    //结果
    if(!rs->result){
        //假 跳回
        if(info.useMmap){
            //mmap: pos 不前进即可，无需 seek        }else{
            fseek(info.pFile,-1*(rs->len),SEEK_CUR);
        }
    }
    else{
        //真 pos+len
        info.pos+=rs->len;
        //更新上传进度
        Q_EMIT SIG_updateUploadFileProgress(info.timestamp,info.pos);
        //判断是否结束
        if(info.pos>=info.size){
            //上传完成时补发最终进度信号：            //发出最终进度后，MainDialog 会将该行从上传中移除并加入上传完成列表。            Q_EMIT SIG_updateUploadFileProgress(info.timestamp, info.pos);
            slot_deleteUploadTask(info);
            if(info.useMmap){
                info.qFile->unmap(info.mappedData);
                info.qFile->close();
                info.qFile.clear();
            }else{
                fclose(info.pFile);
            }
            m_mapTimeStampToFileinfo.erase(rs->timestamp);
            m_activeUploads--;
            m_mainDialog->slot_deleteAllFileInfo();
            slot_getCurDirFileList();
            processUploadQueue();
            return;
        }
    }
    //发文件块
    STRU_FILE_CONTENT_RQ rq;
    rq.fileid=rs->fileid;
    rq.timestamp=rs->timestamp;
    rq.userid=m_id;
    if(info.useMmap){
        int chunkSize=qMin(_DEF_BUFFER, info.size - info.pos);
        memcpy(rq.content, info.mappedData + info.pos, chunkSize);
        rq.len=chunkSize;
    }else{
        rq.len=fread(rq.content,1,_DEF_BUFFER,info.pFile);
    }
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_dealChunkUploadRs(unsigned int lSendIp, char *buf, int nlen)
{
    Q_UNUSED(lSendIp);
    if (nlen < (int)sizeof(STRU_CHUNK_UPLOAD_RS))
        return;
    STRU_CHUNK_UPLOAD_RS* rs = (STRU_CHUNK_UPLOAD_RS*)buf;
    if (rs->userid != m_id)
        return;
    if (rs->result == 0) {
        for (QSet<int>::const_iterator it = m_chunkUploadActive.constBegin(); it != m_chunkUploadActive.constEnd(); ++it) {
            if (m_mapTimeStampToFileinfo.count(*it) > 0 && m_mapTimeStampToFileinfo[*it].fileid == rs->fileid) {
                const int ts = *it; FileInfo info = m_mapTimeStampToFileinfo[ts];
                if (info.useMmap && info.qFile) { info.qFile->unmap(info.mappedData); info.qFile->close(); info.qFile.clear(); }
                else if (info.pFile) { fclose(info.pFile); info.pFile = nullptr; }
                slot_deleteUploadTask(info); clearChunkProgress(ts);
                m_mapTimeStampToFileinfo.erase(ts); m_chunkUploadActive.remove(ts); m_chunkUploadFinished.remove(ts);
                m_activeUploads = qMax(0, m_activeUploads - 1);
                QMessageBox::warning(m_mainDialog.data(), "上传失败", "上传文件校验失败，已回滚本地上传任务");
                m_mainDialog->slot_deleteAllFileInfo(); slot_getCurDirFileList(); processUploadQueue(); break;
            }
        }
        return;
    }
    if (rs->result != 2)
        return;

    //按文件id找任务，整段完成时返回2
    int timestamp = 0;
    for (QSet<int>::const_iterator it = m_chunkUploadActive.constBegin();
         it != m_chunkUploadActive.constEnd(); ++it) {
        if (m_mapTimeStampToFileinfo.count(*it) > 0 &&
            m_mapTimeStampToFileinfo[*it].fileid == rs->fileid &&
            rs->result == 2) {
            timestamp = *it;
            break;
        }
    }
    if (timestamp == 0)
        return;

    int done = m_chunkUploadFinished.value(timestamp, 0) + 1;
    m_chunkUploadFinished[timestamp] = done;
    if (done >= POOL_CONNECTIONS) {
        FileInfo info = m_mapTimeStampToFileinfo[timestamp];
        QMetaObject::invokeMethod(this, [this, timestamp]() {
            clearChunkProgress(timestamp);
        }, Qt::QueuedConnection);
        m_chunkUploadActive.remove(timestamp);
        m_chunkUploadFinished.remove(timestamp);
        slot_deleteUploadTask(info);
        m_mapTimeStampToFileinfo.erase(timestamp);
        m_activeUploads--;
        m_mainDialog->slot_deleteAllFileInfo();
        slot_getCurDirFileList();
        processUploadQueue();
    }
}

void Ckernel::slot_dealGetFileInfoRs(unsigned int lSendIp, char *buf, int nlen)  //获取文件列表
{
    //拆包
    STRU_GET_FILE_INFO_RS*rs=(STRU_GET_FILE_INFO_RS*)buf;
    if(m_curDir!=QString::fromStdString(rs->dir)) return;
    //先删除原来的
    m_mainDialog->slot_deleteAllFileInfo();
    //获取元素
    int count=rs->count;
    for(int i=0;i<count;++i){
        FileInfo info;
        info.fileid=rs->fileInfo[i].fileid;
        info.type=QString::fromStdString(rs->fileInfo[i].fileType);
        info.name=QString::fromStdString(rs->fileInfo[i].name);
        info.size=rs->fileInfo[i].size;
        info.time=rs->fileInfo[i].time;
        //插入到控件
        m_mainDialog->slot_insertFileInfo(info);
    }
}

void Ckernel::slot_dealFileHeaderRq(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_FILE_HEADER_RQ*rq=(STRU_FILE_HEADER_RQ*)buf;
    if (rq->size < 0) {
              qDebug() << "【预览】服务器返回错误: 文件不存在或已被删除, fileid=" << rq->fileid;
              QMetaObject::invokeMethod(m_mainDialog.data(), "slot_shareFileDownloaded",
                                        Qt::QueuedConnection,
                                        Q_ARG(int, rq->fileid),
                                        Q_ARG(QString, QString()));
              return;
          }
    //创建文件信息结构体 赋值
    FileInfo info;
    //默认路径 sysPath(不含最后'/')+dir+name
    QString tmpDir=QString::fromStdString(rq->dir);
    QStringList dirList=tmpDir.split("/");
    QString pathsum=m_sysPath;
    for(QString &node:dirList){
        if(!node.isEmpty()){
            pathsum+="/";
            pathsum+=node;
            QDir dir;
            if(!dir.exists(pathsum))
                dir.mkdir(pathsum);
        }
    }
    info.name=QString::fromStdString(rq->fileName);
    info.dir=QString::fromStdString(rq->dir);
    info.absolutePath=QString("%1%2%3").arg(m_sysPath).arg(info.dir).arg(info.name);

    info.fileid=rq->fileid;
    info.md5=QString::fromStdString(rq->md5);

    info.size=rq->size;
    info.time=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    info.timestamp=rq->timestamp;
    info.type="file";
    info.previewShown = false;
    //先用mmap写，不行再用FILE*
    info.qFile.reset(new QFile(info.absolutePath));
    if(info.qFile->open(QIODevice::ReadWrite)){
        if(info.qFile->resize(info.size)){
            info.mappedData=info.qFile->map(0, info.size);
            if(info.mappedData){
                info.useMmap=true;
                qDebug()<<"mmap download success, size:"<<info.size;
            }
        }
    }
    if(!info.useMmap){
        //回退到FILE*读写
        if(info.qFile){ info.qFile->close(); info.qFile.clear(); }
        char pathbuf[1000]="";
        Utf8ToGB2312(pathbuf,1000,info.absolutePath);
        info.pFile=fopen(pathbuf,"wb");
        if(!info.pFile){
            qDebug()<<"file open fail";
            return;
        }
    }
    //保存下载信息到控件
    slot_writeDownloadTask(info);
    m_mainDialog->slot_insertDownloadFile(info);
    //保存map里
    m_mapTimeStampToFileinfo[rq->timestamp]=info;
    //写回复
    STRU_FILE_HEADER_RS rs;
    rs.fileid=rq->fileid;
    rs.result=1;
    rs.timestamp=rq->timestamp;
    rs.userid=m_id;
    SendData((char*)&rs,sizeof(rs));
    if (info.size >= CHUNK_THRESHOLD && m_tcpClientPool && m_tcpClientPool->isAllOpen()) {
        m_chunkDownloadReceived[rq->timestamp].clear();
        int base=info.size/POOL_CONNECTIONS, rem=info.size%POOL_CONNECTIONS;
        for(int i=0;i<POOL_CONNECTIONS;++i){ TcpClientMediator* c=m_tcpClientPool->connection(i); if(!c) continue; STRU_CHUNK_DOWNLOAD_RQ cr; cr.userid=m_id; cr.fileid=info.fileid; cr.timestamp=info.timestamp; cr.segOffset=i*base+qMin(i,rem); cr.segSize=base+(i<rem?1:0); strcpy(cr.dir,info.dir.toStdString().c_str()); c->SendData(0,(char*)&cr,sizeof(cr)); }
    }
}

void Ckernel::slot_dealChunkDownloadRs(unsigned int, char *buf, int nlen)
{
    if(!buf || nlen < (int)sizeof(STRU_CHUNK_DOWNLOAD_RS)) return;
    STRU_CHUNK_DOWNLOAD_RS *rs=(STRU_CHUNK_DOWNLOAD_RS*)buf;
    auto it=m_mapTimeStampToFileinfo.find(rs->timestamp); if(it==m_mapTimeStampToFileinfo.end() || rs->result!=1) return;
    FileInfo &info=it->second; int globalSeq=rs->segOffset/_DEF_BUFFER+rs->seq;
    if(m_chunkDownloadReceived[rs->timestamp].contains(globalSeq)) return;
    if(rs->len<=0 || rs->len>_DEF_BUFFER || rs->segOffset + rs->seq*_DEF_BUFFER + rs->len > info.size) return;
    bool writeOk = false;
    const int writeOffset = rs->segOffset + rs->seq * _DEF_BUFFER;
    if(info.useMmap) {
        memcpy(info.mappedData + writeOffset, rs->content, rs->len);
        writeOk = true;
    } else if(info.pFile && fseek(info.pFile, writeOffset, SEEK_SET) == 0) {
        writeOk = (fwrite(rs->content, 1, rs->len, info.pFile) == (size_t)rs->len);
        if(writeOk) fflush(info.pFile);
    }
    if(!writeOk) return;
    m_chunkDownloadReceived[rs->timestamp].insert(globalSeq); info.pos += rs->len;
    Q_EMIT SIG_updateDownloadFileProgress(rs->timestamp, info.pos);
    if(info.pos>=info.size){ int savedFid=info.fileid; QString savedPath=info.absolutePath; if(info.useMmap){ info.qFile->unmap(info.mappedData); info.qFile->close(); info.qFile.clear(); } else if(info.pFile) { fclose(info.pFile); info.pFile=nullptr; } m_chunkDownloadReceived.remove(rs->timestamp); slot_deleteDownloadTask(info); m_mapTimeStampToFileinfo.erase(it); m_mainDialog->slot_shareFileDownloaded(savedFid,savedPath); }
}

void Ckernel::slot_dealFileContentRq(unsigned int lSendIp, char *buf, int nlen){
      //拆包
      STRU_FILE_CONTENT_RQ* rq = (STRU_FILE_CONTENT_RQ*)buf;

      //拿到文件信息结构体
      if (m_mapTimeStampToFileinfo.count(rq->timestamp) == 0) return;
      FileInfo& info = m_mapTimeStampToFileinfo[rq->timestamp];

      //非阻塞暂停检查：暂停时不做回复，服务器会等待（和上传暂停同理）
      if(checkPaused(info)){
          return;
      }

      //写文件
      if (info.useMmap) {
          //mmap读写
          memcpy(info.mappedData + info.pos, rq->content, rq->len);
          info.pos += rq->len;
          Q_EMIT SIG_updateDownloadFileProgress(rq->timestamp, info.pos);

          if (!info.previewShown && info.pos >= 524288) {
              info.previewShown = true;
              m_mainDialog->slot_tryEarlyPreview(info.fileid, info.absolutePath);
          }

          //发确认包
          STRU_FILE_CONTENT_RS rs;
          rs.result = 1;  // mmap 写入总是成功
          rs.len = rq->len;
          rs.fileid = rq->fileid;
          rs.timestamp = rq->timestamp;
          rs.userid = m_id;
          SendData((char*)&rs, sizeof(rs));

          if (info.pos >= info.size) {
              MD5 md5;
              md5.update((const void*)info.mappedData, info.size);
              QString computedMD5 = QString::fromStdString(md5.toString());

              info.qFile->unmap(info.mappedData);
              info.qFile->close();
              info.qFile.clear();

              bool valid = (computedMD5 == info.md5);
              if (valid) {
                  qDebug() << "[MD5] 下载校验通过:" << info.name;
              } else {
                  qDebug() << "[MD5] 下载校验失败! 期望:" << info.md5 << "实际:" << computedMD5;
                  QFile::remove(info.absolutePath);
              }

              int savedFid = info.fileid;
              QString savedPath = valid ? info.absolutePath : QString();
              slot_deleteDownloadTask(info);
              m_mapTimeStampToFileinfo.erase(rq->timestamp);
              m_mainDialog->slot_shareFileDownloaded(savedFid, savedPath);
          }
      } else {
          STRU_FILE_CONTENT_RS rs;
          int len = fwrite(rq->content, 1, rq->len, info.pFile);

          if (len != rq->len) {
              rs.result = 0;
              fseek(info.pFile, -1 * len, SEEK_CUR);
          } else {
              rs.result = 1;
              info.pos += len;
              Q_EMIT SIG_updateDownloadFileProgress(rq->timestamp, info.pos);

              if (!info.previewShown && info.pos >= 524288) {
                  info.previewShown = true;
                  m_mainDialog->slot_tryEarlyPreview(info.fileid, info.absolutePath);
              }

              if (info.pos >= info.size) {
                  fclose(info.pFile);

                  QString computedMD5 = QString::fromStdString(
                      getFileMD5(info.absolutePath));

                  bool valid = (computedMD5 == info.md5);
                  if (valid) {
                      qDebug() << "[MD5] 下载校验通过:" << info.name;
                  } else {
                      qDebug() << "[MD5] 下载校验失败! 期望:" << info.md5 << "实际:" << computedMD5;
                      QFile::remove(info.absolutePath);
                  }

                  int savedFid = info.fileid;
                  QString savedPath = valid ? info.absolutePath : QString();
                  slot_deleteDownloadTask(info);
                  m_mapTimeStampToFileinfo.erase(rq->timestamp);
                  m_mainDialog->slot_shareFileDownloaded(savedFid, savedPath);
              }
          }

          //写回复
          rs.fileid = rq->fileid;
          rs.len = rq->len;
          rs.timestamp = rq->timestamp;
          rs.userid = m_id;
          SendData((char*)&rs, sizeof(rs));
      }
}

void Ckernel::slot_dealAddFolderRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_ADD_FOLDER_RS* rs=(STRU_ADD_FOLDER_RS*)buf;
    //判断是否成功
    if(rs->result!=1) return;
    //更新文件列表
    slot_getCurDirFileList();
}

void Ckernel::slot_dealQuickUploadRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_QUICK_UPLOAD_RS* rs=(STRU_QUICK_UPLOAD_RS*)buf;
    //获取文件信息
    if(m_mapTimeStampToFileinfo.count(rs->timestamp)==0) return;
    FileInfo& info=m_mapTimeStampToFileinfo[rs->timestamp];
    //关闭文件
    if(info.useMmap){
        if(info.qFile){ info.qFile->unmap(info.mappedData); info.qFile->close(); info.qFile.clear(); }
    }else{
        if(info.pFile) fclose(info.pFile);
    }
    //写入上传已完成信息
    m_mainDialog->slot_insertUploadComplete(info);
    //发送刷新文件列表
    if(m_curDir==info.dir){  //判断是不是当前目录
        m_mainDialog->slot_deleteAllFileInfo();
        slot_getCurDirFileList();
    }
    //删除节点
    m_mapTimeStampToFileinfo.erase(rs->timestamp);
    m_activeUploads--;
    processUploadQueue();
}

void Ckernel::slot_dealShareFileRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_SHARE_FILE_RS* rs =(STRU_SHARE_FILE_RS*)buf;
    //判断是否成功
    if(rs->result!=1) return;
    QMessageBox::about(m_mainDialog.data(), "分享成功",
        "分享成功！\n分享码可在\"我的分享\"中查看");
    //刷新 发获取请求
    slot_getMyShare();
}

void Ckernel::slot_dealMyShareRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_MY_SHARE_RS* rs=(STRU_MY_SHARE_RS*)buf;
    int count=rs->itemCount;
    //遍历 分享文件的信息 添加到控件上
    m_mainDialog->slot_deleteAllShareInfo();
    for(int i=0;i<count;++i){
        m_mainDialog->slot_insertShareFileInfo(
            rs->items[i].name,
            rs->items[i].size,
            rs->items[i].time,
            rs->items[i].shareLink,
            QString());
    }
}

void Ckernel::slot_dealGetShareRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_GET_SHARE_RS* rs=(STRU_GET_SHARE_RS*)buf;
    //result为0失败，1成功
    switch(rs->result){
    case 1:  // 服务端：查到分享内容，获取成功
        if(QString::fromStdString(rs->dir)==m_curDir)
            slot_getCurDirFileList();
        //保存分享码，下次打开时加载
        slot_saveObtainedShare(m_pendingShareCode, m_pendingSharePassword,
                               QString::fromStdString(rs->dir));
        QMessageBox::about(this->m_mainDialog.data(),"提示","分享文件已添加到您的网盘");
        break;
    default:  // 0 或其他：服务端未查到分享内容，链接无效
        QMessageBox::about(this->m_mainDialog.data(),"提示","分享链接无效");
        break;
    }
}

void Ckernel::slot_dealFolderHeadRq(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_FOLDER_HEADER_RQ *rq=(STRU_FOLDER_HEADER_RQ*)buf;
    //创建目录
    QString tmpDir=QString::fromStdString(rq->dir);
    QStringList dirList=tmpDir.split("/");
    QString pathsum=m_sysPath;
    for(QString &node:dirList){
        if(!node.isEmpty()){
            pathsum+="/";
            pathsum+=node;
            QDir dir;
            if(!dir.exists(pathsum)) {
                dir.mkdir(pathsum);
            }
        }
    }
    pathsum+="/";
    pathsum+=QString::fromStdString(rq->fileName);
    QDir dir;
    if(!dir.exists(pathsum)) {
        dir.mkdir(pathsum);
    }
}

void Ckernel::slot_dealDeleteFileRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_DELETE_FILE_RS* rs=(STRU_DELETE_FILE_RS*)buf;
    //看是否刷新
    if(rs->result==1){
        if (m_deletingFromRecycle) {
            m_deletingFromRecycle = false;
            slot_getRecycle();
        }
        if(QString::fromStdString(rs->dir)==m_curDir) {
            m_mainDialog->slot_deleteAllFileInfo();
            slot_getCurDirFileList();
        }
    }
}

void Ckernel::slot_dealContinueUploadRs(unsigned int lSendIp, char *buf, int nlen)
{
    //拆包
    STRU_CONTINUE_UPLOAD_RS* rs=(STRU_CONTINUE_UPLOAD_RS*)buf;
    //【MD5校验】pos=-1 表示服务端拒绝续传：本地文件与服务端记录的MD5不一致
    if(rs->pos < 0){
        QMessageBox::about(m_mainDialog.data(),"提示",
            "无法续传：本地文件与服务端内容不一致，或该上传任务已失效\n请删除该上传任务后重新上传");
        return;
    }
    //通过map拿到文件信息
    if(m_mapTimeStampToFileinfo.count(rs->timestamp)==0) return;
    FileInfo& info=m_mapTimeStampToFileinfo[rs->timestamp];
    //文件位置跳转 pos更新 界面显示 百分比更新
    info.pos=rs->pos;
    if(!info.useMmap){
        fseek(info.pFile,rs->pos,SEEK_SET);  //SEEK_SET起始位置，从起始位置跳pos这么多
    }
    m_mainDialog->slot_updateUploadFileProgress(info.timestamp,info.pos);
    //发送文件块请求
    STRU_FILE_CONTENT_RQ rq;
    //读文件
    int len;
    if(info.useMmap){
        int chunkSize=qMin(_DEF_BUFFER, info.size - info.pos);
        memcpy(rq.content, info.mappedData + info.pos, chunkSize);
        len=chunkSize;
    }else{
        len=fread(rq.content,1,_DEF_BUFFER,info.pFile);
    }
    rq.len=len;
    rq.fileid=info.fileid;
    rq.timestamp=info.timestamp;
    rq.userid=m_id;
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_dealContinueDownloadRs(unsigned int lSendIp, char *buf, int nlen)
{
    //【MD5校验】下载续传回复处理：result=0 表示服务端拒绝续传
    STRU_CONTINUE_DOWNLOAD_RS* rs=(STRU_CONTINUE_DOWNLOAD_RS*)buf;
    if(rs->result != 1){
        QMessageBox::about(m_mainDialog.data(),"提示",
            "本地已下载的文件与服务端文件内容不一致，无法续传\n请删除该下载任务后重新下载");
        return;
    }
    //result=1：服务端随后会直接发送文件块，这里无需额外处理
}

void Ckernel::slot_dealHeartbeatRq(unsigned int lSendIp, char *buf, int nlen)
{
    if (!buf || nlen < (int)sizeof(STRU_HEARTBEAT_RQ))
        return;
    STRU_HEARTBEAT_RQ* rq = (STRU_HEARTBEAT_RQ*)buf;
    STRU_HEARTBEAT_RS rs;
    rs.seq = rq->seq;
    SendData((char*)&rs, sizeof(rs));
}

void Ckernel::slot_dealHeartbeatRs(unsigned int lSendIp, char *buf, int nlen)
{
    Q_UNUSED(lSendIp);
    if (!buf || nlen < (int)sizeof(STRU_HEARTBEAT_RS))
        return;

    const STRU_HEARTBEAT_RS* rs = (const STRU_HEARTBEAT_RS*)buf;
    if (m_heartbeatPendingSeq == 0 || rs->seq != m_heartbeatPendingSeq) {
        qWarning() << "ignoring heartbeat response for seq" << rs->seq
                   << "(waiting for" << m_heartbeatPendingSeq << ")";
        return;
    }

    m_heartbeatPendingSeq = 0;
    m_heartbeatMissed = 0;
    qDebug() << "heartbeat response received, seq" << rs->seq;
}

void Ckernel::slot_searchFile(QString keyword)
{
    STRU_SEARCH_FILE_RQ rq;
    rq.userid = m_id;
    std::string strKw = keyword.toStdString();
    strcpy(rq.keyword, strKw.c_str());
    SendData((char*)&rq, sizeof(rq));
}

void Ckernel::slot_dealSearchFileRs(unsigned int lSendIp, char *buf, int nlen)
{
    m_mainDialog->slot_showSearchResults(buf, nlen);
}

void Ckernel::slot_getMyShare(){
    STRU_MY_SHARE_RQ rq;
    rq.userid=m_id;

    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_getshareByLink(int code, QString dir, QString password)
{
    STRU_GET_SHARE_RQ rq;
    string tmpDir=dir.toStdString();
    strcpy(rq.dir,tmpDir.c_str());
    rq.shareLink=code;
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    string time=now.toStdString();
    strcpy(rq.time,time.c_str());
    rq.userid=m_id;
    //密码先保存在本地，查看分享时用
    m_pendingShareCode = code;
    m_pendingShareTime = now;
    m_pendingSharePassword = password;
    SendData((char*)&rq,sizeof(rq));
}

void Ckernel::slot_deleteFile(QVector<int> FileidArray, QString dir)
{
    qDebug()<<__func__;
    //发请求
    int packlen=sizeof(STRU_DELETE_FILE_RQ)+FileidArray.size()*sizeof(int);
    STRU_DELETE_FILE_RQ* rq=(STRU_DELETE_FILE_RQ*)malloc(packlen);
    rq->init();;
    string stdDir=dir.toStdString();
    strcpy(rq->dir,stdDir.c_str());
    rq->fileCount=FileidArray.size();
    rq->userid=m_id;
    for(int i=0;i<rq->fileCount;++i)
        rq->fileidArray[i]=FileidArray[i];
    SendData((char*)rq,packlen);
    free(rq);
}

void Ckernel::slot_setUploadPause(int timestamp, int isPause)
{
    //isPause 1从正在上传变为暂停，isPause 0从暂停变为继续上传
    //需要找到文件信息结构体
    //map里有程序未退的情况，直接置位
    //map里没有证明程序未退的情况，断点续传 需要走协议
    if(m_mapTimeStampToFileinfo.count(timestamp)>0) {
          m_mapTimeStampToFileinfo[timestamp].isPause = isPause;
          if(isPause == 0) {
              FileInfo& inf = m_mapTimeStampToFileinfo[timestamp];
              STRU_FILE_CONTENT_RQ contentRq;
              contentRq.fileid = inf.fileid;
              contentRq.timestamp = timestamp;
              contentRq.userid = m_id;
              if(inf.useMmap){
                  int chunkSize = qMin((int)_DEF_BUFFER, inf.size - inf.pos);
                  memcpy(contentRq.content, inf.mappedData + inf.pos, chunkSize);
                  contentRq.len = chunkSize;
              }else{
                  contentRq.len = fread(contentRq.content, 1, _DEF_BUFFER, inf.pFile);
              }
              SendData((char*)&contentRq, sizeof(contentRq));
          }
      }
    else{
        //断点续传
        //创建fileinfo 打开文件 放到map里
        FileInfo info;
        bool res=m_mainDialog->slot_getUploadFileInfoByTimestamp(timestamp,info);
        if(!res){
            //没找到
            return;
        }
        info.qFile.reset(new QFile(info.absolutePath));
        if(info.qFile->open(QIODevice::ReadOnly)){
            info.mappedData=info.qFile->map(0, info.size);
            if(info.mappedData){
                info.useMmap=true;
                qDebug()<<"resume upload mmap success";
            }else{
                info.qFile->close(); info.qFile.clear();
            }
        }
        if(!info.useMmap){
            char pathbuf[1000]="";
            Utf8ToGB2312(pathbuf,1000,info.absolutePath);
            info.pFile=fopen(pathbuf,"rb");
            if(!info.pFile){
                qDebug()<<"打开失败"<<info.absolutePath;
                return;
            }
        }
        info.isPause=0;  //避免开始就停在循环那
        m_mapTimeStampToFileinfo[timestamp]=info;
        //发送上传续传请求
        STRU_CONTINUE_UPLOAD_RQ rq;
        string stdDir=info.dir.toStdString();
        strcpy(rq.dir,stdDir.c_str());
        rq.fileid=info.fileid;
        rq.timestamp=timestamp;
        rq.userid=m_id;
        //【MD5校验】计算本地文件MD5一并发送，服务端比对不一致会拒绝续传
        std::string localMd5 = getFileMD5(info.absolutePath);
        strcpy(rq.md5, localMd5.c_str());
        SendData((char*)&rq,sizeof(rq));
    }
}

void Ckernel::slot_setDownloadPause(int timestamp, int isPause)
{
    //isPause 1从正在下载变为暂停，isPause 0从暂停变为继续下载
    //需要找到文件信息结构体
    //map里有程序未退的情况，直接置位
    //map里没有证明程序未退的情况，断点续传 需要走协议
    if(m_mapTimeStampToFileinfo.count(timestamp)>0) {
          m_mapTimeStampToFileinfo[timestamp].isPause = isPause;
          if(isPause == 0) {
              FileInfo& inf = m_mapTimeStampToFileinfo[timestamp];
              STRU_CONTINUE_DOWNLOAD_RQ rq;
              rq.fileid = inf.fileid;
              string dirstr = inf.dir.toStdString();
              strcpy(rq.dir, dirstr.c_str());
              rq.pos = inf.pos;
              rq.timestamp = timestamp;
              rq.userid = m_id;
              SendData((char*)&rq, sizeof(rq));
          }
      }
    else{
        //断点续传
        //下载的信息已经存到数据库，重新加载登录，然后点击开始（继续）
        if(isPause==0){
            //断点续传
            //1、建立信息结构体装到map里
            //信息可以从控件里取出
            FileInfo info;
            bool res=m_mainDialog->slot_getDownloadFileInfoByTimestamp(timestamp,info);
            if(!res){
                //没找到
                return;
            }
            info.qFile.reset(new QFile(info.absolutePath));
            if(info.qFile->open(QIODevice::ReadWrite)){
                if(info.qFile->resize(info.size)){
                    info.mappedData=info.qFile->map(0, info.size);
                    if(info.mappedData){
                        info.useMmap=true;
                        qDebug()<<"resume download mmap success";
                    }
                }
            }
            if(!info.useMmap){
                if(info.qFile){ info.qFile->close(); info.qFile.clear(); }
                char pathbuf[1000]="";
                Utf8ToGB2312(pathbuf,1000,info.absolutePath);
                info.pFile=fopen(pathbuf,"ab");
                if(!info.pFile){
                    qDebug()<<"打开失败"<<info.absolutePath;
                    return;
                }
            }
            info.isPause=0;  //避免开始就停在循环那
            m_mapTimeStampToFileinfo[timestamp]=info;
            //2、发协议 告诉服务器 文件下载到哪了 然后服务器跳转到哪里，从哪里开始继续读，然后文件块发送
            STRU_CONTINUE_DOWNLOAD_RQ rq;
            rq.fileid=info.fileid;
            string dirstr=info.dir.toStdString();
            strcpy(rq.dir,dirstr.c_str());
            rq.pos=info.pos;
            rq.timestamp=info.timestamp;
            rq.userid=m_id;
            //【MD5校验】计算本地已下载部分内容的MD5一并发送，
            std::string partialMd5 = getFileMD5(info.absolutePath);
            strcpy(rq.md5, partialMd5.c_str());
            SendData((char*)&rq,sizeof(rq));
        }
    }
}

//正在上传的表
/*
create table t_upload(
timestamp int,
f_id int,
f_name varchar(260),
f_dir varchar(260),
f_time varchar(60),
f_size int,
f_md5 varchar(60),
f_type varchar(60),
f_absolutePath varchar(260)
);
*/

//正在下载的表
/*
create table t_download(
timestamp int,
f_id int,
f_name varchar(260),
f_dir varchar(260),
f_time varchar(60),
f_size int,
f_md5 varchar(60),
f_type varchar(60),
f_absolutePath varchar(260)
);
*/
#include<QDir>
#include<QDebug>
void Ckernel::InitDatabase(int id)
{
    m_sql=new CSqlite;
    //找到exe 去同级目录 /database/id.db
    QString path=QCoreApplication::applicationDirPath()+"/database/";
    //先查看路径是否存在 要不要创建
    QDir dir;
    if(!dir.exists(path))
        dir.mkdir(path);
    path=path+QString("%1.db").arg(id);
    //查看有没有这个文件
    QFileInfo info(path);
    if(info.exists()){
        //有 直接加载
        //连接
        m_sql->ConnectSql(path);
        //测试 读取数据
//QString sqlbuf="select count(*) from t_upload;";//QStringList lst;//m_sql->SelectSql(sqlbuf,1,lst);//qDebug()<<"upload item count:"<<lst.front();//lst.clear();
        QList<FileInfo> uploadTaskList;
        QList<FileInfo> downloadTaskList;

        slot_getUploadTask(uploadTaskList);
        slot_getDownloadTask(downloadTaskList);
        //加载上传任务
        for(FileInfo& info:uploadTaskList){
            //如果这个文件没有了不能继续
            QFileInfo fi(info.absolutePath);
            if(!fi.exists())  continue;
            //修改任务的初始状态
            info.isPause=1;
            m_mainDialog->slot_insertUploadFile(info);
            //上传续传 控件 看不见进行到多少
            //todo 获取当前位置

            //同步控件位置
        }
        //加载下载任务
        for(FileInfo& info:downloadTaskList){
            //如果这个文件没有了不能继续
            QFileInfo fi(info.absolutePath);
            if(!fi.exists())  continue;
            //修改任务的初始状态
            info.isPause=1;
            //进行到多少 可以知道 因为是本地文件（通过文件信息 ）
            info.pos=fi.size();
            if(fi.size() >= (qint64)info.size){
                slot_deleteDownloadTask(info);
                m_mainDialog->slot_insertDownloadComplete(info);
                continue;
            }
            m_mainDialog->slot_insertDownloadFile(info);
            //控件同步位置
            m_mainDialog->slot_updateDownloadFileProgress(info.timestamp,fi.size());
        }
    }else {
        //没有 创建表
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly)) return;
        file.close();
        //连接
        m_sql->ConnectSql(path);
        //创建表
        QString sqlbuf="create table t_upload(timestamp int,f_id int,f_name varchar(260),f_dir varchar(260),f_time varchar(60),f_size int,f_md5 varchar(60),f_type varchar(60),f_absolutePath varchar(260));";
        m_sql->UpdateSql(sqlbuf);
        sqlbuf="create table t_download(timestamp int,f_id int,f_name varchar(260),f_dir varchar(260),f_time varchar(60),f_size int,f_md5 varchar(60),f_type varchar(60),f_absolutePath varchar(260));";
        m_sql->UpdateSql(sqlbuf);
    }
    m_sql->UpdateSql("CREATE TABLE IF NOT EXISTS t_upload_chunk_progress("
                     "timestamp INTEGER, seg_index INTEGER, seg_offset INTEGER,"
                     "seg_size INTEGER, sent INTEGER, PRIMARY KEY(timestamp, seg_index));");
    m_sql->UpdateSql("CREATE TABLE IF NOT EXISTS t_obtained_shares("
                     "share_code INTEGER PRIMARY KEY,"
                     "password TEXT,"
                     "root_name TEXT,"
                     "obtain_time TEXT);");
}

void Ckernel::slot_writeUploadTask(FileInfo &info)
{
    QString sqlbuf=QString("insert into t_upload values(%1,%2,'%3','%4','%5',%6,'%7','%8','%9');").arg(info.timestamp).arg(info.fileid).arg(info.name).arg(info.dir).arg(info.time).arg(info.size).arg(info.md5).arg(info.type).arg(info.absolutePath);
    m_sql->UpdateSql(sqlbuf);
}

void Ckernel::slot_writeDownloadTask(FileInfo &info)
{
    QString sqlbuf=QString("insert into t_download values(%1,%2,'%3','%4','%5',%6,'%7','%8','%9');").arg(info.timestamp).arg(info.fileid).arg(info.name).arg(info.dir).arg(info.time).arg(info.size).arg(info.md5).arg(info.type).arg(info.absolutePath);
    m_sql->UpdateSql(sqlbuf);
}

void Ckernel::slot_deleteUploadTask(FileInfo &info)
{
    QString sqlbuf=QString("delete from t_upload where timestamp=%1;").arg(info.timestamp);
    m_sql->UpdateSql(sqlbuf);
}

void Ckernel::slot_deleteDownloadTask(FileInfo &info)
{
    //原来删除条件是 timestamp+绝对路径：绝对路径里含单引号时
    QString sqlbuf=QString("delete from t_download where timestamp=%1;").arg(info.timestamp);
    m_sql->UpdateSql(sqlbuf);
}

void Ckernel::slot_getUploadTask(QList<FileInfo> &infoList)
{
    //获取所有任务
    QString sqlbuf="select * from t_upload;";
    QStringList lst;
    m_sql->SelectSql(sqlbuf,9,lst);
    while(lst.size()!=0){
        FileInfo info;
        info.timestamp=QString(lst.front()).toInt(); lst.pop_front();
        info.fileid=QString(lst.front()).toInt(); lst.pop_front();
        info.name=lst.front(); lst.pop_front();
        info.dir=lst.front(); lst.pop_front();
        info.time=lst.front(); lst.pop_front();
        info.size=QString(lst.front()).toInt(); lst.pop_front();
        info.md5=lst.front(); lst.pop_front();
        info.type=lst.front(); lst.pop_front();
        info.absolutePath=lst.front(); lst.pop_front();
        infoList.push_back(info);
    }
}

void Ckernel::slot_getDownloadTask(QList<FileInfo> &infoList)
{
    //获取所有任务
    QString sqlbuf="select * from t_download;";
    QStringList lst;
    m_sql->SelectSql(sqlbuf,9,lst);
    while(lst.size()!=0){
        FileInfo info;
        info.timestamp=QString(lst.front()).toInt(); lst.pop_front();
        info.fileid=QString(lst.front()).toInt(); lst.pop_front();
        info.name=lst.front(); lst.pop_front();
        info.dir=lst.front(); lst.pop_front();
        info.time=lst.front(); lst.pop_front();
        info.size=QString(lst.front()).toInt(); lst.pop_front();
        info.md5=lst.front(); lst.pop_front();
        info.type=lst.front(); lst.pop_front();
        info.absolutePath=lst.front(); lst.pop_front();
        infoList.push_back(info);
    }
}

int Ckernel::loadChunkProgress(int timestamp, int segIndex)
{
    QStringList values;
    QString sql = QString("SELECT sent FROM t_upload_chunk_progress WHERE timestamp=%1 AND seg_index=%2;")
            .arg(timestamp).arg(segIndex);
    if (m_sql->SelectSql(sql, 1, values) && !values.isEmpty())
        return values.first().toInt();
    return 0;
}

void Ckernel::saveChunkProgress(int timestamp, int segIndex, int segOffset, int segSize, int sent)
{
    //用REPLACE更新分片进度
    QString sql = QString("REPLACE INTO t_upload_chunk_progress(timestamp,seg_index,seg_offset,seg_size,sent) VALUES(%1,%2,%3,%4,%5);")
            .arg(timestamp).arg(segIndex).arg(segOffset).arg(segSize).arg(sent);
    m_sql->UpdateSql(sql);
}

void Ckernel::clearChunkProgress(int timestamp)
{
    m_sql->UpdateSql(QString("DELETE FROM t_upload_chunk_progress WHERE timestamp=%1;").arg(timestamp));
}

void Ckernel::initTcpClientPool()
{
    if(m_tcpClientPool) return;
    m_tcpClientPool.reset(new TcpClientPool(POOL_CONNECTIONS));
    if(!m_tcpClientPool->openAll(m_ip.toStdString().c_str(), m_port.toInt())){
        qDebug()<<"TcpClientPool open failed, chunked transfer disabled";
        m_tcpClientPool.reset();
        return;
    }
    for(int i=0; i<POOL_CONNECTIONS; ++i){
        bindPoolConnection(i);
        TcpClientMediator* conn = m_tcpClientPool->connection(i);
        if(conn){
            connect(conn, SIGNAL(SIG_ReadyData(uint,char*,int)),
                    this, SLOT(slot_dealClientData(uint,char*,int)));
        }
    }
    qDebug()<<"TcpClientPool initialized with"<<POOL_CONNECTIONS<<"connections";
}
void Ckernel::bindPoolConnection(int connIndex)
{
    if(!m_tcpClientPool) return;
    TcpClientMediator* conn = m_tcpClientPool->connection(connIndex);
    if(!conn) return;
    STRU_BIND_POOL_RQ rq;
    rq.userid = m_id;
    rq.connIndex = connIndex;
    conn->SendData(0, (char*)&rq, sizeof(rq));
}

void Ckernel::slot_uploadFileChunked(FileInfo& info)
{
    if (!m_tcpClientPool || !m_tcpClientPool->isAllOpen())
        return;
    //把文件分成几段
    int segSize = info.size / POOL_CONNECTIONS;
    int remainder = info.size % POOL_CONNECTIONS;

    for(int i=0; i<POOL_CONNECTIONS; ++i){
        TcpClientMediator* conn = m_tcpClientPool->connection(i);
        if(!conn) continue;

        STRU_CHUNK_UPLOAD_RQ rq;
        rq.userid = m_id;
        rq.timestamp = info.timestamp;
        rq.fileid = info.fileid;
        rq.segOffset = i * segSize + qMin(i, remainder);
        rq.segSize = segSize + (i < remainder ? 1 : 0);
        rq.totalSize = info.size;
        const int persisted = qBound(0, loadChunkProgress(info.timestamp, i), rq.segSize);
        rq.segProgress = persisted;
        std::string strDir = info.dir.toStdString();
        strcpy(rq.dir, strDir.c_str());
        std::string strName = info.name.toStdString();
        strcpy(rq.fileName, strName.c_str());
        strcpy(rq.md5, info.md5.toStdString().c_str());
        strcpy(rq.fileType, "file");
        strcpy(rq.time, info.time.toStdString().c_str());

        //每条连接只发送自己负责的区间，块大小仍沿用协议的 4KB        conn->SendData(0, (char*)&rq, sizeof(rq));
        const QString localPath = info.absolutePath;
        const int offset = rq.segOffset;
        const int length = rq.segSize;
        const int segIndex = i;
        const int timestamp = info.timestamp;
        const int fileid = info.fileid;
        const int userid = m_id;
        saveChunkProgress(timestamp, segIndex, offset, length, persisted);
        QtConcurrent::run([this, conn, localPath, offset, length, segIndex, timestamp, fileid, userid, persisted]() {
            QThread::msleep(50);
            QFile file(localPath);
            int sent = persisted;
            if (!file.open(QIODevice::ReadOnly) || !file.seek(offset + sent))
                return;
            while (sent < length) {
                const int count = qMin(_DEF_BUFFER, length - sent);
                QByteArray data = file.read(count);
                if (data.size() != count)
                    return;
                STRU_FILE_CONTENT_RQ content;
                content.userid = userid;
                content.fileid = fileid;
                content.timestamp = timestamp;
                content.len = count;
                content.seq = sent / _DEF_BUFFER;
                memcpy(content.content, data.constData(), count);
                if (!conn->SendData(0, (char*)&content, sizeof(content)))
                    return;
                sent += count;
                //每个 4KB 块发送成功后立即落库，崩溃时最多重传当前块                //数据库在主线程更新
                QMetaObject::invokeMethod(this, [this, timestamp, segIndex, offset, length, sent]() {
                    saveChunkProgress(timestamp, segIndex, offset, length, sent);
                }, Qt::QueuedConnection);
            }
        });
        qDebug()<<"Chunk upload segment"<<i<<"offset:"<<rq.segOffset<<"size:"<<rq.segSize;
    }
}

void Ckernel::slot_downloadFileChunked(int fileid, QString dir)
{
    STRU_DOWNLOAD_FILE_RQ rq;
    std::string strDir = dir.toStdString();
    strcpy(rq.dir, strDir.c_str());
    rq.fileid = fileid;
    int timestamp = QDateTime::currentDateTime().toString("hhmmsszzz").toInt();
    while(m_mapTimeStampToFileinfo.count(timestamp)>0)
        timestamp++;
    rq.timestamp = timestamp;
    rq.userid = m_id;
    SendData((char*)&rq, sizeof(rq));
    qDebug()<<"Chunk download requested, fileid:"<<fileid<<"timestamp:"<<timestamp;
}

//========== 收藏功能 ==========
void Ckernel::slot_favoriteFile(QVector<int> fileidArray, QString dir, bool add)
{
    qDebug()<<__func__;
    int packlen = sizeof(STRU_FAVORITE_FILE_RQ) + fileidArray.size() * sizeof(int);
    STRU_FAVORITE_FILE_RQ* rq = (STRU_FAVORITE_FILE_RQ*)malloc(packlen);
    rq->init();
    std::string strDir = dir.toStdString();
    strcpy(rq->dir, strDir.c_str());
    rq->fileCount = fileidArray.size();
    rq->userid = m_id;
    rq->command = add ? FAVORITE_ADD : FAVORITE_REMOVE;
    for (int i = 0; i < rq->fileCount; ++i)
        rq->fileidArray[i] = fileidArray[i];
    SendData((char*)rq, packlen);
    free(rq);
}

void Ckernel::slot_getFavorites()
{
    STRU_GET_FAVORITES_RQ rq;
    rq.userid = m_id;
    SendData((char*)&rq, sizeof(rq));
}

void Ckernel::slot_dealFavoriteFileRs(unsigned int lSendIp, char *buf, int nlen)
{
    STRU_FAVORITE_FILE_RS* rs = (STRU_FAVORITE_FILE_RS*)buf;
    if (rs->result == 1) {
        //收藏操作成功，刷新收藏列表        slot_getFavorites();
    }
}

void Ckernel::slot_dealGetFavoritesRs(unsigned int lSendIp, char *buf, int nlen)
{
    m_mainDialog->slot_showFavorites(buf, nlen);
}

//========== 回收站功能 ==========
void Ckernel::slot_recycleFile(QVector<int> fileidArray, QString dir)
{
    qDebug()<<__func__;
    int packlen = sizeof(STRU_RECYCLE_FILE_RQ) + fileidArray.size() * sizeof(int);
    STRU_RECYCLE_FILE_RQ* rq = (STRU_RECYCLE_FILE_RQ*)malloc(packlen);
    rq->init();
    std::string strDir = dir.toStdString();
    strcpy(rq->dir, strDir.c_str());
    rq->fileCount = fileidArray.size();
    rq->userid = m_id;
    for (int i = 0; i < rq->fileCount; ++i)
        rq->fileidArray[i] = fileidArray[i];
    SendData((char*)rq, packlen);
    qDebug() << "recycle request sent, count:" << rq->fileCount;
    free(rq);
}

void Ckernel::slot_getRecycle()
{
    STRU_GET_RECYCLE_RQ rq;
    rq.userid = m_id;
    SendData((char*)&rq, sizeof(rq));
}

void Ckernel::slot_restoreFile(QVector<int> fileidArray)
{
    qDebug()<<__func__;
    int packlen = sizeof(STRU_RESTORE_FILE_RQ) + fileidArray.size() * sizeof(int);
    STRU_RESTORE_FILE_RQ* rq = (STRU_RESTORE_FILE_RQ*)malloc(packlen);
    rq->init();
    rq->fileCount = fileidArray.size();
    rq->userid = m_id;
    for (int i = 0; i < rq->fileCount; ++i)
        rq->fileidArray[i] = fileidArray[i];
    SendData((char*)rq, packlen);
    free(rq);
}

void Ckernel::slot_deleteForever(QVector<int> fileidArray)
{
    qDebug()<<__func__;
    int packlen = sizeof(STRU_DELETE_FOREVER_RQ) + fileidArray.size() * sizeof(int);
    STRU_DELETE_FOREVER_RQ* rq = (STRU_DELETE_FOREVER_RQ*)malloc(packlen);
    rq->init();
    rq->fileCount = fileidArray.size();
    rq->userid = m_id;
    for (int i = 0; i < rq->fileCount; ++i)
        rq->fileidArray[i] = fileidArray[i];
    SendData((char*)rq, packlen);
    free(rq);
}

void Ckernel::slot_dealRecycleFileRs(unsigned int lSendIP, char *buf, int nlen)
{
    if (!buf || nlen < (int)sizeof(STRU_RECYCLE_FILE_RS))
        return;
    STRU_RECYCLE_FILE_RS* rs = (STRU_RECYCLE_FILE_RS*)buf;
    qDebug() << "recycle response:" << rs->result << "dir:" << rs->dir;
    if (rs->result == 1) {
        //直接刷新，路径以服务端返回的为准
        m_mainDialog->slot_deleteAllFileInfo();
        slot_getCurDirFileList();
        slot_getRecycle();
    } else {
        QMessageBox::warning(m_mainDialog.data(), "提示", "移入回收站失败");
    }
}

void Ckernel::slot_dealGetRecycleRs(unsigned int lSendIp, char *buf, int nlen)
{
    m_mainDialog->slot_showRecycle(buf, nlen);
}

void Ckernel::slot_dealRestoreFileRs(unsigned int lSendIp, char *buf, int nlen)
{
    STRU_RESTORE_FILE_RS* rs = (STRU_RESTORE_FILE_RS*)buf;
    if (rs->result == 1) {
        //恢复成功，刷新回收站列表和文件列表        slot_getRecycle();
        m_mainDialog->slot_deleteAllFileInfo();
        slot_getCurDirFileList();
    } else {
        QMessageBox::about(m_mainDialog.data(), "提示", "恢复失败：文件可能已被自动清除（回收站保留 30 天）");
        slot_getRecycle();  // 刷新回收站列表
    }
}

//========== 上传队列处理 ==========//===== 已获取分享 SQLite 管理 =====
void Ckernel::slot_saveObtainedShare(int shareCode, QString password, QString rootName)
{
    if (!m_sql) return;
    QString sql = QString("INSERT OR REPLACE INTO t_obtained_shares VALUES(%1,'%2','%3','%4');")
        .arg(shareCode)
        .arg(password)
        .arg(rootName)
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    m_sql->UpdateSql(sql);
}

void Ckernel::slot_loadObtainedShares()
{
    QList<QPair<int,QString>> list;
    if (m_sql) {
        QStringList lst;
        m_sql->SelectSql("SELECT share_code, password FROM t_obtained_shares ORDER BY obtain_time DESC;", 2, lst);
        while (lst.size() >= 2) {
            int code = lst.front().toInt(); lst.pop_front();
            QString pwd = lst.front(); lst.pop_front();
            list.append(qMakePair(code, pwd));
        }
    }
    //直接调用MainDialog
    m_mainDialog->slot_onObtainedSharesLoaded(list);
}

void Ckernel::slot_clearUploadTasks()
{
    if (m_sql) {
        m_sql->UpdateSql("DELETE FROM t_upload;");
        m_sql->UpdateSql("DELETE FROM t_download;");
    }
}

void Ckernel::processUploadQueue()
{
    while (m_activeUploads < 3 && !m_uploadQueue.isEmpty()) {
        UploadTask task = m_uploadQueue.dequeue();
        qDebug() << "processUploadQueue: starting" << task.path;
        slot_uploadFile(task.path, task.dir);
    }
}

void Ckernel::slot_browseShare(int shareLink, QString subDir, QString password)
{
    STRU_BROWSE_SHARE_RQ rq;
    rq.userid    = m_id;
    rq.shareLink = shareLink;
    std::string strSub = subDir.toStdString();
    std::string strPwd = password.toStdString();
    strncpy(rq.subDir,   strSub.c_str(), _MAX_PATH_SIZE - 1);
    strncpy(rq.password, strPwd.c_str(), _DEF_SHARE_PWD_LEN - 1);
    SendData((char*)&rq, sizeof(rq));
}

void Ckernel::slot_downloadShareFile(int shareLink, int fileid, QString password)
{
    STRU_DOWNLOAD_SHARE_FILE_RQ rq;
    rq.userid = m_id;
    rq.shareLink = shareLink;
    rq.fileid = fileid;
    std::string strPwd = password.toStdString();
    strncpy(rq.password, strPwd.c_str(), _DEF_SHARE_PWD_LEN - 1);
    SendData((char*)&rq, sizeof(rq));
}

void Ckernel::slot_dealBrowseShareRs(unsigned int lSendIp, char* buf, int nlen)
{
    m_mainDialog->slot_showBrowseShareResult(buf, nlen);
}

void Ckernel::slot_dealDeleteForeverRs(unsigned int lSendIp, char* buf, int nlen)
{
    STRU_DELETE_FOREVER_RS* rs = (STRU_DELETE_FOREVER_RS*)buf;
    if (rs->result == 1) {
        slot_getRecycle();
    } else {
        QMessageBox::about(m_mainDialog.data(), "提示", "彻底删除失败");
        slot_getRecycle();
    }
}
