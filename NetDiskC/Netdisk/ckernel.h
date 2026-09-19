#ifndef CKERNEL_H
#define CKERNEL_H

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QMap>
#include <QScopedPointer>
#include "maindialog.h"
#include"packdef.h"
#include<INetMediator.h>  //也可以用class InetMediator,两种选一个
#include"logindialog.h"
#include"common.h"

#include "csqlite.h"
#include "netapi/mediator/tcpclientpool.h"

class QTimer;

//#define USE_SERVER 1

//连接池配置#define CHUNK_THRESHOLD     (100 * 1024 * 1024)  // 100MB 以上启用分片传输
#define POOL_CONNECTIONS    4   // 并行连接数

//协议映射表
//类成员函数指针
class Ckernel;
typedef void (Ckernel::*PFUN)(unsigned int lSendIp,char*buf,int nlen);  //给类型取别名

class Ckernel : public QObject //核心处理类单例模式，1、对构造、拷贝、析构函数私有化 2、提供静态的公有的获取对象的方法
{
    Q_OBJECT
private:
    explicit Ckernel(QObject *parent = nullptr);
    explicit Ckernel(const Ckernel & ckernel);
    ~Ckernel(){}

    void loadIniFile();
    void setNetPackMap();
    void setSystemPath();

signals:
    void SIG_updateUploadFileProgress(int timestamp,int pos);
    void SIG_updateDownloadFileProgress(int timestamp,int pos);
    void SIG_videoReady(QString localPath);  //视频文件下载完毕，通知播放

public:
    static Ckernel* GetInstance(){ //实现方法有很多，涉及到空间的申请，此处空间申请在全局区，只调用一次，无法回收对象，只能回收其内部的成员
        static Ckernel kernel;
        return &kernel;
    }

private slots:
    //普通槽函数
    void slot_destroy();
    void slot_registerCommit(QString tel,QString password,QString name);
    void slot_loginCommit(QString tel,QString password);
    void slot_sendHeartbeat();
    void slot_uploadFile(QString path,QString dir);  //XX绝对路径的文件上传到XX目录下
    void slot_uploadFolder(QString path,QString dir);  //XX绝对路径的文件夹上传到XX目录下
    void slot_getCurDirFileList();
    void slot_downloadFile(int fileid,QString dir);  //XX文件id xx目录下的文件下载
    void slot_downloadFolder(int fileid,QString dir);  //XX文件id xx目录下的文件夹下载
    void slot_addFolder(QString name,QString dir);  //XX路径下创建xx文件夹
    void slot_changeDir(QString dir);  //改变路径
    void slot_shareFile(QVector<int> fileidArray,QString dir,QString password);  //分享 XX目录下的文件列表
    void slot_getMyShare();  //获取个人所有分享
    void slot_getshareByLink(int code,QString dir,QString password);  //获取XX分享码的文件 添加到XX目录
    void slot_deleteFile(QVector<int> FileidArray,QString dir);  //删除XX目录下的一系列文件
    void slot_setUploadPause(int timestamp,int isPause);  //设置上传暂停 0开始 1暂停
    void slot_setDownloadPause(int timestamp,int isPause);  //设置下载暂停 0开始 1暂停
    void slot_searchFile(QString keyword);
    void slot_favoriteFile(QVector<int> fileidArray, QString dir, bool add);
    void slot_recycleFile(QVector<int> fileidArray, QString dir);
    void slot_getFavorites();
    void slot_getRecycle();
    void slot_restoreFile(QVector<int> fileidArray);
    void slot_deleteForever(QVector<int> fileidArray);
    void slot_browseShare(int shareLink, QString subDir, QString password);
    void slot_downloadShareFile(int shareLink, int fileid, QString password);

    //已获取分享SQLite管理    void slot_saveObtainedShare(int shareCode, QString password, QString rootName);
    void slot_loadObtainedShares();
    void slot_clearUploadTasks();  // 清空上传/下载任务表

    //网络响应槽函数
    void slot_dealClientData(unsigned int lSendIP,char*buf,int nlen);
    void slot_dealLoginRs(unsigned int lSendIp,char*buf,int nlen); //登录回复的处理
    void slot_dealRegisterRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealUploadFileRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealFileContentRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealGetFileInfoRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealFileHeaderRq(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealFileContentRq(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealAddFolderRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealQuickUploadRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealShareFileRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealMyShareRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealGetShareRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealFolderHeadRq(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealDeleteFileRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealContinueUploadRs(unsigned int lSendIp,char*buf,int nlen);
    void slot_dealContinueDownloadRs(unsigned int lSendIp,char*buf,int nlen);  //下载续传回复（MD5校验拒绝/允许）
    void slot_dealHeartbeatRq(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealHeartbeatRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealChunkUploadRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealChunkDownloadRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealSearchFileRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealFavoriteFileRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealGetFavoritesRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealRecycleFileRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealGetRecycleRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealRestoreFileRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealBrowseShareRs(unsigned int lSendIp, char* buf, int nlen);
    void slot_dealDeleteForeverRs(unsigned int lSendIp, char* buf, int nlen);  // 彻底删除回复

#ifdef USE_SERVER
    void slot_dealServerData(unsigned int lSendIp,char*buf,int nlen);
#endif
private:
    QScopedPointer<MainDialog> m_mainDialog;
    QScopedPointer<LoginDialog> m_loginDialog;
    QString m_ip;
    QString m_port;

    QString m_name;
    int m_id;
    QString m_curDir;  //网盘当前目录
    QString m_sysPath;   //默认存储的系统路径（绝对路径） exe同级下NetDisk文件夹

    QScopedPointer<INetMediator> m_tcpClient;
    QTimer *m_heartbeatTimer = nullptr;
    int m_heartbeatSeq = 0;
    int m_heartbeatPendingSeq = 0;
    int m_heartbeatMissed = 0;
    QScopedPointer<TcpClientPool> m_tcpClientPool;  // 连接池（超大文件分片传输）
    std::map<int,FileInfo> m_mapTimeStampToFileinfo;  //key时间戳  value文件信息
    PFUN m_netPackMap[_DEF_PACK_COUNT];

    bool m_quit;  //退出标志
    bool m_deletingFromRecycle;  // 标记当前删除操作是否来自回收站
    int m_pendingShareCode;      // 最近一次发出的分享码请求
    QString m_pendingShareTime;  // 对应时间
    QString m_pendingSharePassword;  // 最近一次分享请求的密码
    CSqlite* m_sql;  //数据库
    struct UploadTask { QString path; QString dir; };
    QQueue<UploadTask> m_uploadQueue;
    int m_activeUploads;  // 当前正在上传的文件数
    QSet<int> m_chunkUploadActive; // 正在使用四路连接上传的任务
    QMap<int,QSet<int>> m_chunkDownloadReceived;
    QMap<int, int> m_chunkUploadFinished; // 任务已完成的分片数量
    void processUploadQueue();  // 从队列取出下一个上传任务
    void SendData(char* buf,int len);
private:
    void InitDatabase(int id);
    //连接池初始化（登录后调用）    void initTcpClientPool();
    void bindPoolConnection(int connIndex);
    //分片上传/下载辅助方法    void slot_uploadFileChunked(FileInfo& info);
    void slot_downloadFileChunked(int fileid, QString dir);
    void slot_writeUploadTask(FileInfo &info);
    void slot_writeDownloadTask(FileInfo &info);
    void slot_deleteUploadTask(FileInfo &info);
    void slot_deleteDownloadTask(FileInfo &info);
    void slot_getUploadTask(QList<FileInfo> &infoList);
    void slot_getDownloadTask(QList<FileInfo> &infoList);
    //四路分片进度持久化：程序重启后可从每段已发送位置继续    int loadChunkProgress(int timestamp, int segIndex);
    void saveChunkProgress(int timestamp, int segIndex, int segOffset, int segSize, int sent);
    void clearChunkProgress(int timestamp);
};

#endif // CKERNEL_H
