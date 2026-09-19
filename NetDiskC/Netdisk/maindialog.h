#ifndef MAINDIALOG_H
#define MAINDIALOG_H

#include <QDialog>
#include<QCloseEvent>
#include<QMenu>
#include<QSet>
#include<QQueue>
#include<QPair>
#include<QMap>
#include"common.h"
#include"mytablewidgetitem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainDialog; }
QT_END_NAMESPACE
class Ckernel;
class PlayerDialog;
class MainDialog : public QDialog
{
    Q_OBJECT
signals:
    void SIG_close(); //定义信号，缺省前缀SIG
    void SIG_uploadFile(QString path,QString dir);  //XX绝对路径的文件上传到XX目录下
    void SIG_uploadFolder(QString path,QString dir);  //XX绝对路径的文件夹上传到XX目录下
    void SIG_downloadFile(int fileid,QString dir);  //XX文件id xx目录下的文件下载
    void SIG_downloadFolder(int fileid,QString dir);  //XX文件id xx目录下的文件夹下载
    void SIG_addFolder(QString name,QString dir);  //XX路径下创建xx文件夹
    void SIG_changeDir(QString dir);  //改变路径
    void SIG_shareFile(QVector<int> FileidArray,QString dir,QString password);  //分享XX目录下的文件列表
    void SIG_getshareByLink(int code,QString dir,QString password);  //获取XX分享码的文件 添加到XX目录
    void SIG_deleteFile(QVector<int> FileidArray,QString dir);  //删除XX目录下的一系列文件
    void SIG_setUploadPause(int timestamp,int isPause);  //设置上传暂停 0开始 1暂停
    void SIG_setDownloadPause(int timestamp,int isPause);  //设置下载暂停 0开始 1暂停
    void SIG_searchFile(QString keyword);
    void SIG_favoriteFile(QVector<int> fileidArray, QString dir, bool add);  // 收藏/取消收藏
    void SIG_recycleFile(QVector<int> fileidArray, QString dir);  // 移入回收站
    void SIG_getFavorites();  // 获取收藏列表
    void SIG_getRecycle();    // 获取回收站列表
    void SIG_restoreFile(QVector<int> fileidArray);  // 从回收站恢复
    void SIG_deleteForever(QVector<int> fileidArray); // 彻底删除
    void SIG_browseShare(int shareLink, QString subDir, QString password); // 浏览分享目录
    void SIG_downloadShareFile(int shareLink, int fileid, QString password); // 下载分享文件用于预览
    void SIG_saveObtainedShare(int shareCode, QString password, QString rootName); // 保存已获取的分享到本地
    void SIG_loadObtainedShares();  // 请求加载已获取的分享列表
    void SIG_clearUploadTasks();    // 清空SQLite中的上传任务
public:
    MainDialog(QWidget *parent = nullptr);
    ~MainDialog();

    void closeEvent(QCloseEvent * event);  //关闭事件，点击窗口的叉就执行，然后发送信号
private slots:
    void slot_setInfo(QString name);
    void on_pb_file_clicked();
    void on_pb_transmit_clicked();
    void on_pb_share_clicked();
    void on_pb_viewShare_clicked();
    void on_pb_addFile_clicked();

    void slot_addFolder(bool flag);
    void slot_uploadFile(bool flag);
    void slot_uploadFolder(bool flag);
    void slot_downloadFile(bool flag);
    void slot_shareFile(bool flag);
    void slot_deleteFile(bool flag);
    void slot_getShare(bool flag);
    void slot_uploadPause(bool flag);
    void slot_uploadResume(bool flag);
    void slot_downloadPause(bool flag);
    void slot_downloadResume(bool flag);
    void slot_playVideo(bool flag);   //播放视频

    void slot_insertUploadFile(FileInfo& info);
    void slot_insertUploadComplete(FileInfo& info);
    void slot_insertDownloadFile(FileInfo& info);
    void slot_insertDownloadComplete(FileInfo& info);
    void slot_insertShareFileInfo(QString name,int size,QString time,int shareLink,QString password);
    void slot_insertViewShareInfo(QString name, int size, QString time, int shareLink);

    void slot_updateUploadFileProgress(int timestamp,int pos);
    void slot_updateDownloadFileProgress(int timestamp,int pos);
    void slot_deleteDownloadFileByRow(int row);
    void slot_deleteUploadFileByRow(int row);

    void slot_insertFileInfo(FileInfo&info);

    void on_table_file_cellClicked(int row, int column);   //选中某一行
    void on_table_file_customContextMenuRequested(const QPoint &pos);

    void slot_openPath(bool flag);

    void slot_deleteAllFileInfo();
    void slot_deleteAllShareInfo();

    void on_table_file_cellDoubleClicked(int row, int column);

    void on_pb_prev_clicked();

    void on_table_upload_cellClicked(int row, int column);

    void on_table_download_cellClicked(int row, int column);

    bool slot_getDownloadFileInfoByTimestamp(int timestamp,FileInfo& info);
    bool slot_getUploadFileInfoByTimestamp(int timestamp,FileInfo& info);

    //视频播放相关    void slot_onVideoReady(QString localPath);
    bool isVideoFile(const QString& name);
    void playVideoFile(const QString& localPath);
    //校验本地视频能否解出第一帧：
    //播放前校验，损坏则删除并重新下载    bool checkVideoPlayable(const QString& localPath);

    void slot_searchFile(bool flag);
    void on_pb_searchBack_clicked();
    void slot_showSearchResults(const char* buf, int nlen);  // 展示搜索结果

    //收藏相关    void slot_favoriteFile(bool flag);
    void slot_cancelFavorite(bool flag);
    void on_pb_store_clicked();
    void slot_insertFavoriteInfo(int fileid, QString name, QString dir, int size, QString time, QString type);
    void slot_deleteAllFavoriteInfo();
    void slot_showFavorites(const char* buf, int nlen);

    //回收站相关    void slot_recycleFile(bool flag);
    void on_pb_bin_clicked();
    void on_pb_restoreFile_clicked();
    void on_pb_deleteForever_clicked();
    void slot_insertRecycleInfo(int fileid, QString name, QString dir, int size, QString time, QString type);
    void slot_deleteAllRecycleInfo();
    void slot_showRecycle(const char* buf, int nlen);

    void on_table_favorite_cellClicked(int row, int column);
    void on_table_recycle_cellClicked(int row, int column);
    void on_table_viewShare_cellClicked(int row, int col);
    void on_pb_shareBack_clicked();
    void on_pb_clearUpload_clicked();  // 清空上传列表
    void on_pb_clearComplete_clicked();  // 清空"已完成"列表
    void slot_deleteCompleteSelected();  // 删除"已完成"列表选中的行

    void slot_refreshObtainedShares();       // 自动加载已获取的分享列表
    void slot_onObtainedSharesLoaded(QList<QPair<int,QString>> list);  // 收到列表数据后展示

    //分享预览相关    void slot_showBrowseShareResult(const char* buf, int nlen);
    void slot_browseShareEnterFolder(int row, int col);
    bool isImageFile(const QString& name);
    bool isTextFile(const QString& name);
    void showTextPreview(const QString& localPath, const QString& name);
    void showVideoThumbnail(const QString& localPath, const QString& name);
    void showSharePreview(const QString& name, const QString& type,
                          const QString& localPath);
    Q_INVOKABLE void slot_shareFileDownloaded(int fileid, QString localPath);  // 分享文件下载完成
    void slot_tryEarlyPreview(int fileid, QString localPath);  // 512KB早期预览
private:
    Ui::MainDialog *ui;
    QMenu m_menuAddFile;
    QMenu m_menuFileInfo;
    QMenu m_menuUpload;
    QMenu m_menuDownload;
    QMenu m_menuFavorite;   // 收藏页右键菜单
    QMenu m_menuRecycle;    // 回收站页右键菜单
    QMenu m_menuComplete;   // 已完成页右键菜单
    PlayerDialog* m_playerDialog;
    QString m_sysPath;
    QSet<QString> m_pendingVideoPaths;

    //分享预览状态    int     m_browseShareCode;      // 当前正在预览的分享码
    QString m_browseSharePwd;       // 对应密码
    QString m_browseShareCurDir;    // 当前浏览目录（用于面包屑返回）
    QMap<int, QString> m_pendingSharePreviewFiles;  // 等待预览的分享文件：fileid -> fileName

    //多分享合并加载    QQueue<QPair<int,QString>> m_browseQueue;  // 待加载的分享队列
    QMap<int,QString> m_sharePasswords;         // shareCode → password 持久映射
    int     m_currentLoadingCode;               // 当前正在加载的分享码
    QString m_currentLoadingPwd;                // 当前正在加载的分享密码
    bool    m_isLoadingShares;                  // 是否在多分享加载模式

    friend class Ckernel;
};
#endif // MAINDIALOG_H

//点击叉，执行关闭事件，弹窗询问，发送关闭信号，核心类接收，回收资源
