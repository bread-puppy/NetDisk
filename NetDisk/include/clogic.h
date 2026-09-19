#ifndef CLOGIC_H
#define CLOGIC_H

#include"TCPKernel.h"

class CLogic
{
public:
    CLogic( TcpKernel* pkernel )
    {
        m_pKernel = pkernel;
        m_sql = pkernel->m_sql;
        m_tcp = pkernel->m_tcp;
    }
public:
    //设置协议映射
    void setNetPackMap();
    int getNumber(){
        return 1000000000;
    }
    /************** 发送数据*********************/
    void SendData( sock_fd clientfd, char*szbuf, int nlen )
    {
        m_pKernel->SendData( clientfd ,szbuf , nlen );
    }
    /************** 网络处理 *********************/
    //注册
    void RegisterRq(sock_fd clientfd, char*szbuf, int nlen);
    //登录
    void LoginRq(sock_fd clientfd, char*szbuf, int nlen);
    //处理客户端心跳请求，返回相同序号的心跳应答
    void HeartbeatRq(sock_fd clientfd, char*szbuf, int nlen);
    //连接池绑定和四路分片上传请求
    void BindPoolRq(sock_fd clientfd, char*szbuf, int nlen);
    void ChunkUploadRq(sock_fd clientfd, char*szbuf, int nlen);
    void ChunkDownloadRq(sock_fd clientfd, char*szbuf, int nlen);
    //上传文件请求
    void UploadFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //文件块请求
    void FileContentRq(sock_fd clientfd, char*szbuf, int nlen);
    //获取文件信息请求
    void GetFileInfoRq(sock_fd clientfd, char*szbuf, int nlen);
    //下载文件请求
    void DownloadFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //下载文件夹请求
    void DownloadFolderRq(sock_fd clientfd, char*szbuf, int nlen);
    //文件头回复
    void FileHeaderRs(sock_fd clientfd, char*szbuf, int nlen);
    //文件内容回复
    void FileContentRs(sock_fd clientfd, char*szbuf, int nlen);
    //新建文件夹请求
    void AddFolderRq(sock_fd clientfd, char*szbuf, int nlen);
    //分享文件请求
    void ShareFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //分享一个文件
    void ShareItem(int userid,int fileid,string dir,string time,int link);
    //获取个人分享的所有文件信息
    void MyShareRq(sock_fd clientfd, char*szbuf, int nlen);
    //获取分享添加到目录
    void GetShareRq(sock_fd clientfd, char*szbuf, int nlen);
    void GetShareByFile(int userid,int fileid,string dir,string name,string time);
    void GetShareByFolder(int userid,int fileid,string dir,string name,string time,int fromuserid,string fromdir);
    void DownloadFolder(int userid, int& timestamp, sock_fd clientfd, list<string> &lstRes);
    void DownloadFile(int userid, int& timestamp, sock_fd clientfd, list<string> &lstRes);
    //删除文件请求
    void DeleteFileRq(sock_fd clientfd, char*szbuf, int nlen);
    void DeleteOneItem(int userid, int fileid, string dir);
    void DeleteFile(int userid, int fileid, string dir, string path);
    void DeleteFolder(int userid, int fileid, string dir,string name);
    void ContinueDownloadRq(sock_fd clientfd, char*szbuf, int nlen);
    void ContinueUploadRq(sock_fd clientfd, char*szbuf, int nlen);

    /* ============【新增代码】客户端新增功能的协议处理（以下全部为追加声明） ============ */
    //文件搜索请求    void SearchFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //收藏/取消收藏请求    void FavoriteFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //获取收藏列表请求    void GetFavoritesRq(sock_fd clientfd, char*szbuf, int nlen);
    //移入回收站请求    void RecycleFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //获取回收站列表请求    void GetRecycleRq(sock_fd clientfd, char*szbuf, int nlen);
    //从回收站恢复请求    void RestoreFileRq(sock_fd clientfd, char*szbuf, int nlen);
    //回收站彻底删除请求    void DeleteForeverRq(sock_fd clientfd, char*szbuf, int nlen);
    //浏览分享目录请求（预览用，不复制文件到自己网盘）    void BrowseShareRq(sock_fd clientfd, char*szbuf, int nlen);
    //下载分享文件请求（用于预览）    void DownloadShareFileRq(sock_fd clientfd, char*szbuf, int nlen);

    /* ============【新增代码】目录创建相关 ============ */
    void CreateDirChain(const char* path);
    //服务启动时初始化存储根目录（DEF_PATH），创建失败时打印明确提示    void InitStoragePath();
    /*文件块零拷贝发送（由静态函数改为成员函数）：
     * 发送全程持有该连接的发送锁，避免心跳回包等其他线程的 send
     * 插到文件块中间，导致客户端拆包错位、下载文件损坏 */
    bool SendFileContentZeroCopy(sock_fd clientfd, int fileFd, int len,
                                 int timestamp, int userid, int fileid);
    struct ChunkUploadSession;
    struct ChunkConnectionState;
private:
    TcpKernel* m_pKernel;
    CMysql * m_sql;
    Block_Epoll_Net * m_tcp;

    MyMap<int,UserInfo*> m_mapIDToUserInfo;
    MyMap<int64_t,FileInfo*> m_mapTimestampToFileInfo;  //key userid*1000000000 +timestamp value 文件信息
    MyMap<int,int> m_poolConnectionUsers; //连接 fd -> 已绑定用户 id
    MyMap<int,ChunkConnectionState*> m_chunkConnections; //连接 fd -> 当前分片状态
    MyMap<int64_t,ChunkUploadSession*> m_chunkSessions; //任务 key -> 分片上传会话
};

#endif // CLOGIC_H
