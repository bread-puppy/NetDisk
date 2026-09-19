#include "clogic.h"
#include "md5.h"  //【MD5校验】上传/续传完整性校验使用
#include <sys/sendfile.h>
#include <algorithm>
#include <stddef.h>

#define DEF_PATH "/home/colin/Netdisk/netdisk_storage"

//【密码加密】固定盐：NetDiskS_Password_Salt_2026（密码入库/登录校验统一使用）
static const string PASSWORD_SALT = "NetDiskS_Password_Salt_2026";

//【MD5校验】前向声明：计算文件MD5的辅助函数（实现在文件末尾），
//供上方 UploadFileRq/ContinueDownloadRq 等函数使用static std::string ComputeFileMD5(const char* path, int64_t limitBytes);
static pthread_mutex_t g_chunkSessionCreateLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_shareLinkLock = PTHREAD_MUTEX_INITIALIZER;

/*SQL 字符串转义：
 * 文件名/目录名由客户端直接拼进 SQL，名字里含单引号（如 "张三's笔记"）
 * 会导致 SQL 报错，上传/建文件夹失败，文件在列表里永远不显示；
 * 含恶意字符串时还会破坏查询。把 '\' 和 ''' 转义后再拼接。
 * forLike=true 时额外转义 LIKE 通配符 % 和 _，避免搜索关键字被当作通配符。*/
static std::string SqlEscape(const char* src, bool forLike = false)
{
    std::string out;
    if (!src)
        return out;
    for (const char* p = src; *p; ++p) {
        if (*p == '\\' || *p == '\'')
            out += '\\';
        if (forLike && (*p == '%' || *p == '_'))
            out += '\\';
        out += *p;
    }
    return out;
}

//协议仍保持 STRU_FILE_CONTENT_RQ 的固定布局：包大小、头部、文件内容、尾部 len 分段发送，TCP 会保证字节顺序。/*整个文件块（包头+文件内容+尾部）在发送期间持有该连接的发送锁。
 * 线程池中下载文件块与心跳回包可能由不同线程并发发送到同一连接，
 * 不加锁时心跳包的字节会插到文件块中间，客户端按 4 字节包头拆包必然错位，
 * 表现为下载文件损坏、视频无法播放、下载完成后客户端无法收尾（任务记录残留，
 * 重登后“已完成”记录复活）。锁的粒度是一个完整文件块，保证字节流不被插入。*/
bool CLogic::SendFileContentZeroCopy(sock_fd clientfd, int fileFd, int len,
                                     int timestamp, int userid, int fileid)
{
    if (len < 0 || fileFd < 0)
        return false;

    m_tcp->SendLock(clientfd);

    //空文件仍需发送一个合法的零长度协议包，保持客户端状态机一致。    if (len == 0) {
        STRU_FILE_CONTENT_RQ empty;
        empty.timestamp = timestamp;
        empty.userid = userid;
        empty.fileid = fileid;
        int packetSize = sizeof(empty);
        char packet[sizeof(int) + sizeof(empty)];
        memcpy(packet, &packetSize, sizeof(int));
        memcpy(packet + sizeof(int), &empty, sizeof(empty));
        size_t total = 0;
        bool ok = true;
        while (total < sizeof(packet)) {
            ssize_t n = send(clientfd, packet + total, sizeof(packet) - total, 0);
            if (n > 0) total += (size_t)n;
            else if (n < 0 && errno == EINTR) continue;
            else { ok = false; break; }
        }
        if (ok)
            m_tcp->TouchActivity(clientfd);
        m_tcp->SendUnlock(clientfd);
        return ok;
    }

    STRU_FILE_CONTENT_RQ header;
    header.timestamp = timestamp;
    header.userid = userid;
    header.fileid = fileid;
    header.len = len;

    const int packetSize = sizeof(header);
    const size_t contentOffset = offsetof(STRU_FILE_CONTENT_RQ, content);
    char prefix[sizeof(int) + sizeof(header)];
    memcpy(prefix, &packetSize, sizeof(int));
    memcpy(prefix + sizeof(int), &header, contentOffset);
    size_t prefixLen = sizeof(int) + contentOffset;
    size_t sent = 0;
    while (sent < prefixLen) {
        ssize_t n = send(clientfd, prefix + sent, prefixLen - sent, 0);
        if (n > 0) sent += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { usleep(1000); }
        else { m_tcp->SendUnlock(clientfd); return false; }
    }

    off_t remain = len;
    while (remain > 0) {
        ssize_t n = sendfile(clientfd, fileFd, NULL, (size_t)remain);
        if (n > 0) remain -= n;
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { usleep(1000); }
        else { m_tcp->SendUnlock(clientfd); return false; }
    }

    //content 是定长数组，短块需要补零；其后依次是 len 和 seq 两个 int 字段。    //body(=4116) 多 4 字节，客户端 recv 会一直阻塞等待这 4 字节，表现为下载卡死、    //视频无法播放、下载完成的文件也不可见。现补发 seq 字段使双方字节数严格一致。    char suffix[sizeof(header.content) + sizeof(header.len) + sizeof(header.seq)] = {0};
    memcpy(suffix + (sizeof(header.content) - len), &header.len, sizeof(header.len));
    size_t suffixLen = (sizeof(header.content) - len) + sizeof(header.len) + sizeof(header.seq);
    sent = 0;
    while (sent < suffixLen) {
        ssize_t n = send(clientfd, suffix + sent, suffixLen - sent, 0);
        if (n > 0) sent += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { usleep(1000); }
        else { m_tcp->SendUnlock(clientfd); return false; }
    }
    m_tcp->TouchActivity(clientfd);
    m_tcp->SendUnlock(clientfd);
    return true;
}

//四路上传会话：四条连接共同写入同一个文件，每条连接只负责自己的区间struct CLogic::ChunkUploadSession
{
    ChunkUploadSession():fileFd(-1), userid(0), timestamp(0), fileid(0),
        totalSize(0), completedSegments(0), finalized(false)
    {
        pthread_mutex_init(&lock, NULL);
    }
    ~ChunkUploadSession()
    {
        pthread_mutex_destroy(&lock);
    }
    pthread_mutex_t lock;
    int fileFd;
    int userid;
    int timestamp;
    int fileid;
    int totalSize;
    string path;
    string md5;
    map<int, int> segmentOffsets;
    map<int, int> segmentSizes;
    map<int, int> segmentReceived;
    map<int, bool> segmentFinished;
    int completedSegments;
    bool finalized;
};

struct CLogic::ChunkConnectionState
{
    ChunkConnectionState():session(NULL), offset(0), size(0), received(0), finished(false){}
    ChunkUploadSession* session;
    int offset;
    int size;
    int received;
    bool finished;
};

void CLogic::setNetPackMap()
{
    //memset(m_pKernel->m_NetPackMap, 0, sizeof(m_pKernel->m_NetPackMap));  //豆给的
    NetPackMap(_DEF_PACK_REGISTER_RQ)    = &CLogic::RegisterRq;
    NetPackMap(_DEF_PACK_LOGIN_RQ)       = &CLogic::LoginRq;
    //客户端每 30 秒发送一次心跳，服务端收到后立即回包
    NetPackMap(_DEF_PACK_HEARTBEAT_RQ)   = &CLogic::HeartbeatRq;
    NetPackMap(_DEF_PACK_BIND_POOL_RQ)   = &CLogic::BindPoolRq;
    NetPackMap(_DEF_PACK_CHUNK_UPLOAD_RQ)= &CLogic::ChunkUploadRq;
    NetPackMap(_DEF_PACK_CHUNK_DOWNLOAD_RQ)= &CLogic::ChunkDownloadRq;
    NetPackMap(_DEF_PACK_UPLOAD_FILE_RQ) = &CLogic::UploadFileRq;
    NetPackMap(_DEF_PACK_FILE_CONTENT_RQ)= &CLogic::FileContentRq;
    NetPackMap(_DEF_PACK_GET_FILE_INFO_RQ)= &CLogic::GetFileInfoRq;
    NetPackMap(_DEF_PACK_DOWNLOAD_FILE_RQ)= &CLogic::DownloadFileRq;
    NetPackMap(_DEF_PACK_FILE_HEADER_RS)= &CLogic::FileHeaderRs;
    NetPackMap(_DEF_PACK_FILE_CONTENT_RS)= &CLogic::FileContentRs;
    NetPackMap(_DEF_PACK_ADD_FOLDER_RQ)= &CLogic::AddFolderRq;
    NetPackMap(_DEF_PACK_SHARE_FILE_RQ)=&CLogic::ShareFileRq;
    NetPackMap(_DEF_PACK_MY_SHARE_RQ)=&CLogic::MyShareRq;
    NetPackMap(_DEF_PACK_GET_SHARE_RQ)=&CLogic::GetShareRq;
    NetPackMap(_DEF_PACK_DOWNLOAD_FOLDER_RQ)=&CLogic::DownloadFolderRq;
    NetPackMap(_DEF_PACK_DELETE_FILE_RQ)=&CLogic::DeleteFileRq;
    NetPackMap(_DEF_PACK_CONTINUE_DOWNLOAD_RQ)=&CLogic::ContinueDownloadRq;
    NetPackMap(_DEF_PACK_CONTINUE_UPLOAD_RQ)=&CLogic::ContinueUploadRq;

    /* ============【新增代码】注册客户端新增功能的协议映射（以下全部为追加） ============ */
    NetPackMap(_DEF_PACK_SEARCH_FILE_RQ)        = &CLogic::SearchFileRq;
    NetPackMap(_DEF_PACK_FAVORITE_FILE_RQ)      = &CLogic::FavoriteFileRq;
    NetPackMap(_DEF_PACK_GET_FAVORITES_RQ)      = &CLogic::GetFavoritesRq;
    NetPackMap(_DEF_PACK_RECYCLE_FILE_RQ)       = &CLogic::RecycleFileRq;
    NetPackMap(_DEF_PACK_GET_RECYCLE_RQ)        = &CLogic::GetRecycleRq;
    NetPackMap(_DEF_PACK_RESTORE_FILE_RQ)       = &CLogic::RestoreFileRq;
    NetPackMap(_DEF_PACK_DELETE_FOREVER_RQ)     = &CLogic::DeleteForeverRq;
    NetPackMap(_DEF_PACK_BROWSE_SHARE_RQ)       = &CLogic::BrowseShareRq;
    NetPackMap(_DEF_PACK_DOWNLOAD_SHARE_FILE_RQ)= &CLogic::DownloadShareFileRq;
}

void CLogic::HeartbeatRq(sock_fd clientfd, char* szbuf, int nlen)
{
    if (szbuf == NULL || nlen < (int)sizeof(STRU_HEARTBEAT_RQ))
        return;

    STRU_HEARTBEAT_RQ* rq = (STRU_HEARTBEAT_RQ*)szbuf;
    STRU_HEARTBEAT_RS rs;
    rs.seq = rq->seq;
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

void CLogic::BindPoolRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_BIND_POOL_RS rs;
    if (szbuf == NULL || nlen < (int)sizeof(STRU_BIND_POOL_RQ)) {
        SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    STRU_BIND_POOL_RQ* rq = (STRU_BIND_POOL_RQ*)szbuf;
    if (rq->connIndex < 0 || rq->connIndex >= 4 || rq->userid <= 0) {
        SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    m_poolConnectionUsers.insert(clientfd, rq->userid);
    //标记为连接池连接：这些连接只在任务开始/暂停/恢复时通信，
    //客户端不在其上发心跳，超时检查会误杀它们（大文件传输中断、暂停后无法恢复）    m_tcp->MarkPoolFd(clientfd);
    rs.result = 1;
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

void CLogic::ChunkUploadRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_CHUNK_UPLOAD_RS rs;
    if (szbuf == NULL || nlen < (int)sizeof(STRU_CHUNK_UPLOAD_RQ)) {
        SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    STRU_CHUNK_UPLOAD_RQ* rq = (STRU_CHUNK_UPLOAD_RQ*)szbuf;
    int boundUser = 0;
    if (!m_poolConnectionUsers.find(clientfd, boundUser)) {
        //未绑定的连接不能传分片        SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }
    //同一个客户端退出后换账号重新登录时，连接池连接仍是旧账号的绑定，
    //userid+timestamp+md5 作为任务标识，这里更新绑定即可继续使用连接池。    if (boundUser != rq->userid) {
        m_poolConnectionUsers.insert(clientfd, rq->userid);
        printf("【连接池】fd[%d] 绑定用户 %d -> %d（重新登录换号）\n",
               clientfd, boundUser, rq->userid);
    }
    if (rq->userid <= 0 || rq->timestamp <= 0 ||
        rq->totalSize <= 0 || rq->segSize <= 0 ||
        rq->segOffset < 0 || rq->segOffset > rq->totalSize ||
        rq->segSize > rq->totalSize - rq->segOffset) {
        SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    const int64_t key = (int64_t)rq->userid * getNumber() + rq->timestamp;
    ChunkUploadSession* session = NULL;
    pthread_mutex_lock(&g_chunkSessionCreateLock);
    if (!m_chunkSessions.find(key, session)) {
        session = new ChunkUploadSession;
        session->userid = rq->userid;
        session->timestamp = rq->timestamp;
        session->fileid = rq->fileid;
        session->totalSize = rq->totalSize;
        session->md5 = rq->md5;
        char path[1000] = {0};
        sprintf(path, "%s%d%s%s", DEF_PATH, rq->userid, rq->dir, rq->md5);
        session->path = path;
        session->fileFd = open(path, O_CREAT | O_RDWR, 00777);
        if (session->fileFd < 0 || ftruncate(session->fileFd, rq->totalSize) != 0) {
            if (session->fileFd >= 0) close(session->fileFd);
            delete session;
            pthread_mutex_unlock(&g_chunkSessionCreateLock);
            SendData(clientfd, (char*)&rs, sizeof(rs));
            return;
        }
        m_chunkSessions.insert(key, session);

        //初始上传请求已经打开了同一路径的普通文件句柄，切换到分片模式后释放它        FileInfo* normalInfo = NULL;
        if (m_mapTimestampToFileInfo.find(key, normalInfo) && normalInfo != NULL) {
            if (normalInfo->fileFd >= 0) close(normalInfo->fileFd);
            m_mapTimestampToFileInfo.erase(key);
            delete normalInfo;
        }
    }
    pthread_mutex_unlock(&g_chunkSessionCreateLock);

    pthread_mutex_lock(&session->lock);
    if (session->segmentOffsets.count(clientfd) == 0) {
        session->segmentOffsets[clientfd] = rq->segOffset;
        session->segmentSizes[clientfd] = rq->segSize;
        const int progress = std::max(0, std::min(rq->segProgress, rq->segSize));
        session->segmentReceived[clientfd] = progress;
        session->segmentFinished[clientfd] = (progress == rq->segSize);
    } else if (session->segmentOffsets[clientfd] != rq->segOffset ||
               session->segmentSizes[clientfd] != rq->segSize) {
        pthread_mutex_unlock(&session->lock);
        SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }
    pthread_mutex_unlock(&session->lock);

    ChunkConnectionState* oldState = NULL;
    if (m_chunkConnections.find(clientfd, oldState) && oldState != NULL)
        delete oldState;
    ChunkConnectionState* state = new ChunkConnectionState;
    state->session = session;
    state->offset = rq->segOffset;
    state->size = rq->segSize;
    m_chunkConnections.insert(clientfd, state);

    rs.userid = rq->userid;
    rs.fileid = rq->fileid;
    rs.segOffset = rq->segOffset;
    rs.result = 1;
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

#define _DEF_COUT_FUNC_    cout << "clientfd:"<< clientfd << __func__ << endl;
//注册
void CLogic::RegisterRq(sock_fd clientfd,char* szbuf,int nlen)
{
    //cout << "clientfd:"<< clientfd << __func__ << endl;
    _DEF_COUT_FUNC_

    //拆包 tel password name
    STRU_REGISTER_RQ* rq=(STRU_REGISTER_RQ*)szbuf;
    STRU_REGISTER_RS rs;
    std::string sTel  = SqlEscape(rq->tel);
    std::string sName = SqlEscape(rq->name);
    //根据tel查看手机号是否存在
    char sqlstr[1000]="";
    sprintf(sqlstr,"select u_tel from t_user where u_tel='%s';",sTel.c_str());
    printf("【服务端】执行查询SQL: %s\n", sqlstr); // 豆加这行

    list<string> lstRes;
    bool res=m_sql->SelectMysql(sqlstr,1,lstRes);
    if(!res){
        std::cout<<"select fail:"<<sqlstr<<std::endl;
    }
    if(lstRes.size()!=0)
        rs.result=tel_is_exist;  //存在 返回
    else{
        //不存在，查看昵称是否存在
        sprintf(sqlstr,"select u_tel from t_user where u_name='%s';",sName.c_str());
        list<string> lstRes;
        bool res=m_sql->SelectMysql(sqlstr,1,lstRes);
        if(!res)
            std::cout<<"select fail:"<<sqlstr<<std::endl;
        if(lstRes.size()!=0)
            rs.result=name_is_exist;  //存在 返回
        else{
            //不存在
            rs.result=register_success;
            //【密码加密】数据库只保存 MD5(密码 + 固定盐)，不保存明文密码
            string strPasswordMD5 = MD5(string(rq->password) + PASSWORD_SALT).toString();
            //注册成功，写入信息
            sprintf(sqlstr,"insert into t_user(u_tel,u_password,u_name)values('%s','%s','%s');",sTel.c_str(),strPasswordMD5.c_str(),sName.c_str());
            m_sql->UpdataMysql(sqlstr);
            //取出该人id
            sprintf(sqlstr,"select u_id from t_user where u_tel='%s'and u_password='%s';",sTel.c_str(),strPasswordMD5.c_str());
            lstRes.clear();
            bool res=m_sql->SelectMysql(sqlstr,1,lstRes);
            if(!res)
                std::cout<<"select fail:"<<sqlstr<<std::endl;
            if(lstRes.size()!=0){
                int id=stoi(lstRes.front());
                lstRes.pop_front();

                //网盘：创建该人对应的目录 id 命名
                //默认路径 DEF_PATH  /home/rose/qtpro/NetDisk/
                char pathbuf[_MAX_PATH_SIZE]="";
                sprintf(pathbuf,"%s%d/",DEF_PATH,id);
                //创建路径
                umask(0);  //设置权限掩码
                //改用 CreateDirChain 递归创建：原 mkdir 只能创建一层，
                //（服务端打印 "file open fail" 且不回复客户端）
                CreateDirChain(pathbuf);
            }
        }
    }
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

//登录
void CLogic::LoginRq(sock_fd clientfd ,char* szbuf,int nlen)
{
//cout << "clientfd:"<< clientfd << __func__ << endl;    _DEF_COUT_FUNC_

    //拆包 tel password
    STRU_LOGIN_RQ* rq=(STRU_LOGIN_RQ*)szbuf;
    STRU_LOGIN_RS rs;
    //根据tel查 id password name
    //手机号转义后再拼 SQL
    std::string sTel = SqlEscape(rq->tel);
    char sqlstr[1000]="";
    sprintf(sqlstr,"select u_id, u_password, u_name from t_user where u_tel='%s';",sTel.c_str());
    list<string> lstRes;
    bool res=m_sql->SelectMysql(sqlstr,3,lstRes);
    if(!res)
        std::cout<<"select fail:"<<sqlstr<<std::endl;
    if(lstRes.size()==0)
        rs.result=tel_not_exist;
    else{
        int id=stoi(lstRes.front());
        lstRes.pop_front();
        //有
        string strPassword=lstRes.front();
        lstRes.pop_front();
        string strName=lstRes.front();
        lstRes.pop_front();
        //密码是否一致
        //【密码加密】按注册时相同规则计算 MD5(密码 + 固定盐) 后再比对
        string strPasswordMD5 = MD5(string(rq->password) + PASSWORD_SALT).toString();
        if(strPassword != strPasswordMD5)
            rs.result=password_error;  //不一致 返回
        else{
            //一致
            rs.result=login_success;
            rs.userid=id;
            strcpy(rs.name,strName.c_str());
            //用户身份
            //需要创建用户身份结构
            //首先看是否已经在map里，如果不在，直接创建
            UserInfo* info=nullptr;
            if(!m_mapIDToUserInfo.find(id,info))
                info=new UserInfo;
            else{
                //如果存在，让其下线 todo
            }
            //赋值
            info->name=strName;
            info->clientfd=clientfd;
            info->userid=id;
            //写入map
            m_mapIDToUserInfo.insert(id,info);
        }
    }
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::UploadFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_UPLOAD_FILE_RQ* rq=(STRU_UPLOAD_FILE_RQ*)szbuf;
    std::string sFileName = SqlEscape(rq->fileName);
    std::string sDir      = SqlEscape(rq->dir);
    std::string sMd5      = SqlEscape(rq->md5);
    //查看是否秒传
    {
        //判断这个文件是否已经上传过
        //根据md5 state =1 查数据库得到id  如果state=0客户端拒绝或者挂起这个请求
        char sqlbuf[1000]="";
        sprintf(sqlbuf,"select f_id from t_file where f_MD5='%s'and f_state=1;",sMd5.c_str());
        list<string> lstRes;
        bool res=m_sql->SelectMysql(sqlbuf,1,lstRes);
        if(!res){
            cout<<"select fail:"<<sqlbuf<<endl;
            return;
        }
        if(lstRes.size()>0){ //已上传 查到了
            int fileid=stoi(lstRes.front());
            lstRes.pop_front();
            //写入用户文件关系 由于有触发器 文件引用计数自动+1
            //上传时间也是客户端字段，一并转义
            std::string sTime = SqlEscape(rq->time);
            sprintf(sqlbuf,"insert into t_user_file(u_id,f_id,f_dir,f_name,f_uploadtime) values(%d,%d,'%s','%s','%s');",rq->userid,fileid,sDir.c_str(),sFileName.c_str(),sTime.c_str());
            res=m_sql->UpdataMysql(sqlbuf);
            if(!res){
                printf("upload fail:%s\n",sqlbuf);
            }
            //写回复包 客户端收到之后 就更新列表
            STRU_QUICK_UPLOAD_RS rs;
            rs.result=1;
            rs.timestamp=rq->timestamp;
            rs.userid=rq->userid;
            //发送
            SendData(clientfd,(char*)&rs,sizeof(rs));
            //返回
            return;
        }
    }
    //不是秒传 文件信息创建 打开文件
    FileInfo* info=new FileInfo;
    char strpath[1000]="";
    sprintf(strpath,"%s%d%s%s",DEF_PATH,rq->userid,rq->dir,rq->md5);
    char userDirBuf[1000]="";
    sprintf(userDirBuf,"%s%d%s",DEF_PATH,rq->userid,rq->dir);
    CreateDirChain(userDirBuf);  //char数组直接传入即可
    info->absolutePath=strpath;  //通过这个写数据库 打开文件 文件名字 md5
    info->dir=rq->dir;
    info->fid;
    info->md5=rq->md5;
    info->name=rq->fileName;
    info->size=rq->size;
    info->time=rq->time;
    info->type=rq->fileType;  //原代码 info->type=rq->type 把协议号当文件类型
    info->fileFd=open(strpath,O_CREAT|O_WRONLY|O_TRUNC,00777);
    if(info->fileFd<0){
        //打印 errno 和完整路径，便于定位 open 失败原因：
        std::cout<<"file open fail errno:"<<errno<<" path:"<<strpath<<std::endl;
        delete info;
        STRU_UPLOAD_FILE_RS failRs;
        failRs.fileid=0;
        failRs.result=0;
        failRs.timestamp=rq->timestamp;
        failRs.userid=rq->userid;
        SendData(clientfd,(char*)&failRs,sizeof(failRs));
        return;
    }
    //map存储文件信息
    //(int64_t) 强制转换：userid*1e9 在 int 里会溢出（userid>=3 就溢出），
    //溢出后各处算出的 key 不一致，下载/上传任务查不到文件信息而卡死    int64_t user_time=(int64_t)rq->userid* getNumber()+rq->timestamp;
    m_mapTimestampToFileInfo.insert(user_time,info);
    //数据库记录
    //插入文件信息（引用计数0,状态0->上传结束后改为1
    std::string sPath = SqlEscape(strpath);
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"insert into t_file(f_size,f_path,f_MD5,f_count,f_state,f_type)values( %d,'%s','%s',0,0,'file'); ",rq->size,sPath.c_str(),sMd5.c_str());
    bool res=m_sql->UpdataMysql(sqlbuf);
    if(!res)
        printf("update fail:%s\n",sqlbuf);
    //查文件id
    sprintf(sqlbuf,"select f_id from t_file where f_path='%s'and f_MD5='%s'",sPath.c_str(),sMd5.c_str());
    list<string> lstRes;
    res=m_sql->SelectMysql(sqlbuf,1,lstRes);
    if(!res)
        printf("select fail:%s\n",sqlbuf);
    if(lstRes.size()>0)
        info->fid=stoi(lstRes.front());
    lstRes.clear();
    //插入用户文件关系（由于触发器 引用计数变成1)
    //上传时间也是客户端字段，一并转义
    std::string sTime = SqlEscape(rq->time);
    sprintf(sqlbuf,"insert into t_user_file(u_id, f_id, f_dir , f_name ,f_uploadtime ) values( %d,%d,'%s','%s','%s'); ",rq->userid,info->fid,sDir.c_str(),sFileName.c_str(),sTime.c_str());
    res=m_sql->UpdataMysql(sqlbuf);
    if(!res)
        printf("update fail:%s\n",sqlbuf);
    //写回复包
    STRU_UPLOAD_FILE_RS rs;
    rs.fileid=info->fid;
    rs.result=1;
    rs.timestamp=rq->timestamp;
    rs.userid=rq->userid;
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::FileContentRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_FILE_CONTENT_RQ* rq=(STRU_FILE_CONTENT_RQ*)szbuf;

    //四路分片上传使用 pwrite 按区间写入，不共享普通上传的文件游标    ChunkConnectionState* chunkState = NULL;
    if (m_chunkConnections.find(clientfd, chunkState) &&
        chunkState != NULL && chunkState->session != NULL) {
        ChunkUploadSession* session = chunkState->session;
        STRU_CHUNK_UPLOAD_RS rs;
        rs.fileid = rq->fileid;
        rs.userid = rq->userid;
        rs.segOffset = chunkState->offset;
        bool allFinished = false;
        bool writeOk = false;
        bool segmentFinishedNow = false;

        if (rq->userid == session->userid && rq->timestamp == session->timestamp &&
            rq->fileid == session->fileid && rq->len > 0 && rq->len <= _DEF_BUFFER) {
            pthread_mutex_lock(&session->lock);
            int received = session->segmentReceived[clientfd];
            int segmentSize = session->segmentSizes[clientfd];
            const int expectedSeq = received / _DEF_BUFFER;
            if (!session->segmentFinished[clientfd] && rq->seq == expectedSeq &&
                received + rq->len <= segmentSize) {
                const off_t writeOffset = session->segmentOffsets[clientfd] +
                                          (off_t)rq->seq * _DEF_BUFFER;
                ssize_t written = (lseek(session->fileFd, writeOffset, SEEK_SET) < 0)
                        ? -1 : write(session->fileFd, rq->content, rq->len);
                if (written == rq->len) {
                    session->segmentReceived[clientfd] += rq->len;
                    writeOk = true;
                    if (session->segmentReceived[clientfd] == segmentSize) {
                        session->segmentFinished[clientfd] = true;
                        ++session->completedSegments;
                        segmentFinishedNow = true;
                    }
                }
            }
            allFinished = (session->segmentOffsets.size() == 4 &&
                           session->completedSegments == 4 &&
                           !session->finalized);
            if (allFinished)
                session->finalized = true;
            pthread_mutex_unlock(&session->lock);
        }

        //result=1 表示本块已写入，result=2 表示该连接负责的整段已完成        rs.result = writeOk ? (segmentFinishedNow ? 2 : 1) : 0;
        SendData(clientfd, (char*)&rs, sizeof(rs));

        if (allFinished) {
            //四段全部落盘后统一校验，校验通过才把文件标记为完成            string actualMD5 = ComputeFileMD5(session->path.c_str(), -1);
            bool md5Ok = !actualMD5.empty() && actualMD5 == session->md5;
            if (md5Ok) {
                char sqlbuf[256] = {0};
                sprintf(sqlbuf, "update t_file set f_state=1 where f_id=%d;",
                        session->fileid);
                m_sql->UpdataMysql(sqlbuf);
            } else {
                char sqlbuf[512] = {0};
                sprintf(sqlbuf, "delete from t_user_file where u_id=%d and f_id=%d;",
                        session->userid, session->fileid);
                m_sql->UpdataMysql(sqlbuf);
                unlink(session->path.c_str());
                STRU_CHUNK_UPLOAD_RS fail;
                fail.userid = session->userid;
                fail.fileid = session->fileid;
                fail.segOffset = -1;
                fail.result = 0;
                SendData(clientfd, (char*)&fail, sizeof(fail));
                printf("【四路分片 MD5 校验失败】%s\n", session->path.c_str());
            }
            close(session->fileFd);
            session->fileFd = -1;
        }
        return;
    }

    //获取文件信息
    int64_t user_time=(int64_t)rq->userid* getNumber()+rq->timestamp;
    FileInfo*info=nullptr;
    if(!m_mapTimestampToFileInfo.find(user_time,info)){
        cout<<"file not fount"<<endl;
        return;
    }
    STRU_FILE_CONTENT_RS rs;
    //写入
    int len=write(info->fileFd,rq->content,rq->len);
    if(len!=rq->len){
        rs.result=0;
        lseek(info->fileFd,-1*len,SEEK_CUR);
    }
    else{
        //成功 pos更新位置
        rs.result=1;
        info->pos+=len;
        //看是否到末尾
        if(info->pos>=info->size){
            //是 关闭文件
            close(info->fileFd);
            //【MD5校验】上传完成时重算服务端文件MD5，与客户端上报的MD5比对
            string strPath = info->absolutePath;
            string strMd5  = info->md5;
            string strDir  = info->dir;
            int    uid     = rq->userid;
            string actualMD5 = ComputeFileMD5(strPath.c_str(), -1);
            bool bMd5Ok = (!actualMD5.empty() && actualMD5 == strMd5);
            //回收map节点
            m_mapTimestampToFileInfo.erase(user_time);
            delete  info;
            info=nullptr;
            if(bMd5Ok){
                //MD5一致：更新数据库 把文件信息的状态更新为1,表示已完成
                char sqlbuf[1000]="";
                sprintf(sqlbuf,"update t_file set f_state=1 where f_id=%d;",rq->fileid);
                vector<string> txSql;
                txSql.push_back(sqlbuf);
                bool res=m_sql->ExecuteTransaction(txSql);
                if(!res)
                    cout<<"update fail:"<<sqlbuf<<endl;
            }else{
                //MD5不一致：删除用户文件关系与物理文件，防止脏数据被当作完整文件
                printf("【MD5校验失败】上传文件内容与客户端上报MD5不一致, 已删除: %s (上报:%s 实际:%s)\n",
                       strPath.c_str(), strMd5.c_str(), actualMD5.c_str());
                char sqlbuf[1000]="";
                //目录转义后再拼 SQL
                std::string sDelDir = SqlEscape(strDir.c_str());
                sprintf(sqlbuf,"delete from t_user_file where u_id=%d and f_id=%d and f_dir='%s';",uid,rq->fileid,sDelDir.c_str());
                vector<string> txSql;
                txSql.push_back(sqlbuf);
                if(!m_sql->ExecuteTransaction(txSql))
                    cout<<"事务回滚：清理失败"<<endl;
                unlink(strPath.c_str());
            }
        }
    }
    //返回结果
    rs.fileid=rq->fileid;
    rs.len=rq->len;
    rs.timestamp=rq->timestamp;
    rs.userid=rq->userid;
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::GetFileInfoRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
   STRU_GET_FILE_INFO_RQ* rq=(STRU_GET_FILE_INFO_RQ*)szbuf;
    //根据id dir 查看（视图）获取文件信息
   std::string sDir = SqlEscape(rq->dir);
   char sqlbuf[1000]="";
   sprintf(sqlbuf,"select uf.f_id,uf.f_name,t.f_size,uf.f_uploadtime,t.f_type from t_user_file uf join t_file t on uf.f_id = t.f_id where uf.u_id=%d and uf.f_dir='%s' and t.f_state=1;"
           ,rq->userid,sDir.c_str());
   list<string> lstRes;
   bool res=m_sql->SelectMysql(sqlbuf,5,lstRes);
   if(!res){
       cout<<"select fail:"<<sqlbuf<<endl;
       //查询失败也回一个空列表，客户端清掉旧列表而不是继续显示上个目录的文件
       lstRes.clear();
   }
   //空目录也要回包(count=0)：原先直接 return，客户端收不到回复，
   //进入空文件夹后仍显示上一个目录的文件列表   int count=lstRes.size()/5;
   //写回复包
   int packlen=sizeof(STRU_GET_FILE_INFO_RS)+count*sizeof(STRU_FILE_INFO);
   STRU_GET_FILE_INFO_RS *rs=(STRU_GET_FILE_INFO_RS*)malloc(packlen);
   rs->init();
   rs->count=count;
   strcpy(rs->dir,rq->dir);
   for(int i=0;i<count;++i){
       int f_id=stoi(lstRes.front());  lstRes.pop_front();
       string name=lstRes.front();  lstRes.pop_front();
       int f_size=stoi(lstRes.front());  lstRes.pop_front();
       string time=lstRes.front();  lstRes.pop_front();
       string f_type=lstRes.front();  lstRes.pop_front();
       rs->fileInfo[i].fileid=f_id;
       strcpy(rs->fileInfo[i].fileType,f_type.c_str());
       strcpy(rs->fileInfo[i].name,name.c_str());
       strcpy(rs->fileInfo[i].time,time.c_str());
       rs->fileInfo[i].size=f_size;
   }
   //发送
   SendData(clientfd,(char*)rs,packlen);
   free(rs);  //回收
}

void CLogic::DownloadFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_DOWNLOAD_FILE_RQ *rq=(STRU_DOWNLOAD_FILE_RQ*)szbuf;
    //查数据库 查f_name,f_path,f_MD5,f_size 如果没有 返回
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(rq->dir);
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"SELECT t_user_file.f_name, t_file.f_path, t_file.f_MD5, t_file.f_size "
                    "FROM t_user_file JOIN t_file ON t_user_file.f_id = t_file.f_id "
                    "WHERE t_user_file.u_id = %d AND t_user_file.f_dir = '%s' AND t_user_file.f_id = %d",rq->userid,sDir.c_str(),rq->fileid);
    list<string> lstRes;
    bool res=m_sql->SelectMysql(sqlbuf,4,lstRes);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }
    if(lstRes.size()==0) return;
    string strName=lstRes.front(); lstRes.pop_front();
    string strPath=lstRes.front(); lstRes.pop_front();
    string strMD5=lstRes.front(); lstRes.pop_front();
    int size=stoi(lstRes.front()); lstRes.pop_front();
    //有 写文件信息
    FileInfo* info=new FileInfo;
    info->absolutePath=strPath;
    info->dir=rq->dir;
    info->fid=rq->fileid;
    info->md5=strMD5;
    info->name=strName;
    info->size=size;
    info->type="file";
    info->fileFd=open(info->absolutePath.c_str(),O_RDONLY);
    if(info->fileFd<=0){
        cout<<"file open fail"<<endl;
        //客户端约定：文件头 size<0 表示失败，会弹提示并结束任务。        STRU_FILE_HEADER_RQ failHead;
        failHead.fileid=rq->fileid;
        failHead.timestamp=rq->timestamp;
        failHead.size=-1;
        SendData(clientfd,(char*)&failHead,sizeof(failHead));
        delete info;
        return;
    }
    //key求出来
    int64_t user_time=(int64_t)rq->userid* getNumber()+rq->timestamp;
    //存到map里
    m_mapTimestampToFileInfo.insert(user_time,info);
    //发送文件头请求
    STRU_FILE_HEADER_RQ headrq;
    strcpy(headrq.dir,rq->dir);
    headrq.fileid=rq->fileid;
    strcpy(headrq.fileName,info->name.c_str());
    strcpy(headrq.md5,info->md5.c_str());
    strcpy(headrq.fileType,"file");
    headrq.size=info->size;
    headrq.timestamp=rq->timestamp;
    SendData(clientfd,(char*)&headrq,sizeof(headrq));
}

void CLogic::ChunkDownloadRq(sock_fd clientfd, char *szbuf, int nlen)
{
    if (!szbuf || nlen < (int)sizeof(STRU_CHUNK_DOWNLOAD_RQ)) return;
    STRU_CHUNK_DOWNLOAD_RQ *rq=(STRU_CHUNK_DOWNLOAD_RQ*)szbuf;
    char sqlbuf[1000]={0};
    sprintf(sqlbuf,"select f_path,f_size from user_file_info where u_id=%d and f_id=%d;",rq->userid,rq->fileid);
    list<string> rows; if(!m_sql->SelectMysql(sqlbuf,2,rows) || rows.size()<2) return;
    string path=rows.front(); rows.pop_front(); int size=stoi(rows.front());
    int fd=open(path.c_str(),O_RDONLY); if(fd<0) return;
    int sent=0, seq=0;
    while(sent<rq->segSize){ int len=std::min(_DEF_BUFFER,rq->segSize-sent); STRU_CHUNK_DOWNLOAD_RS rs; rs.userid=rq->userid; rs.fileid=rq->fileid; rs.timestamp=rq->timestamp; rs.segOffset=rq->segOffset; rs.seq=seq++; rs.len=len; if(pread(fd,rs.content,len,rq->segOffset+sent)!=len){rs.result=0; SendData(clientfd,(char*)&rs,sizeof(rs)); break;} SendData(clientfd,(char*)&rs,sizeof(rs)); sent+=len; }
    close(fd);
}

void CLogic::DownloadFolderRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_DOWNLOAD_FOLDER_RQ* rq=(STRU_DOWNLOAD_FOLDER_RQ*)szbuf;
    //查数据库，拿信息  查name,path,MD5,size  -->folder  即f_id,d_name,f_path,f_MD5,f_size,f_dir,f_type
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(rq->dir);
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"select f_type,f_id,f_name,f_path,f_Md5,f_size,f_dir from user_file_info where u_id=%d and f_dir='%s' and f_id=%d;",rq->userid,sDir.c_str(),rq->fileid);
    list<string> lstRes;
    bool res=m_sql->SelectMysql(sqlbuf,7,lstRes);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }
    if(lstRes.size()==0)  return;
    string type=lstRes.front(); lstRes.pop_front();
    int timestamp=rq->timestamp;
    //下载文件夹
    DownloadFolder(rq->userid,timestamp,clientfd,lstRes);
}

void CLogic::DownloadFolder(int userid,int& timestamp,sock_fd clientfd,list<string>&lstRes){
    int fileid=stoi(lstRes.front()); lstRes.pop_front();
    string strName=lstRes.front(); lstRes.pop_front();
    string strPath=lstRes.front(); lstRes.pop_front();
    string strMD5=lstRes.front(); lstRes.pop_front();
    int size=stoi(lstRes.front()); lstRes.pop_front();
    string dir=lstRes.front(); lstRes.pop_front();
    //string type=lstRes.front(); lstRes.pop_front();
    //发送创建文件夹请求
    STRU_FOLDER_HEADER_RQ rq;
    rq.timestamp=++timestamp;
    strcpy(rq.dir,dir.c_str());
    rq.fileid=fileid;
    strcpy(rq.fileName,strName.c_str());
    SendData(clientfd,(char*)&rq,sizeof(rq));
    //拼接路径
    string newdir=dir+ strName+"/";
    //查询 newdir userid 所有文件信息（包含type） 比如查列表 3个文件 21项
    //目录转义后再拼 SQL
    std::string sNewdir = SqlEscape(newdir.c_str());
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"select f_type,f_id,f_name,f_path,f_Md5,f_size,f_dir from user_file_info where u_id=%d and f_dir='%s';",userid,sNewdir.c_str());
    list<string> newlstRes;
    bool res=m_sql->SelectMysql(sqlbuf,7,newlstRes);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }
    while(newlstRes.size()!=0){
        string type=newlstRes.front(); newlstRes.pop_front();
        if(type=="file")  //如果是文件 下载文件流程
            DownloadFile(userid,timestamp,clientfd,newlstRes);
        else  //如果是文件夹 递归
            DownloadFolder(userid,timestamp,clientfd,newlstRes);
    }
}

void CLogic::DownloadFile(int userid,int& timestamp,sock_fd clientfd,list<string>& lstRes){
    int fileid=stoi(lstRes.front()); lstRes.pop_front();
    string strName=lstRes.front(); lstRes.pop_front();
    string strPath=lstRes.front(); lstRes.pop_front();
    string strMD5=lstRes.front(); lstRes.pop_front();
    int size=stoi(lstRes.front()); lstRes.pop_front();
    string dir=lstRes.front(); lstRes.pop_front();
    //string type=lstRes.front(); lstRes.pop_front();
    //有 写文件信息
    FileInfo* info=new FileInfo;
    info->absolutePath=strPath;
    info->dir=dir;
    info->fid=fileid;
    info->md5=strMD5;
    info->name=strName;
    info->size=size;
    info->type="file";
    info->fileFd=open(info->absolutePath.c_str(),O_RDONLY);
    if(info->fileFd<=0){
        cout<<"file open fail"<<endl;
        STRU_FILE_HEADER_RQ failHead;
        failHead.fileid=fileid;
        failHead.timestamp=timestamp;
        failHead.size=-1;
        SendData(clientfd,(char*)&failHead,sizeof(failHead));
        delete info;
        return;
    }
    //key求出来
    int64_t user_time=(int64_t)userid* getNumber()+(++timestamp);
    //存到map里
    m_mapTimestampToFileInfo.insert(user_time,info);
    //发送文件头请求
    STRU_FILE_HEADER_RQ headrq;
    strcpy(headrq.dir,dir.c_str());
    headrq.fileid=fileid;
    strcpy(headrq.fileName,info->name.c_str());
    strcpy(headrq.md5,info->md5.c_str());
    strcpy(headrq.fileType,"file");
    headrq.size=info->size;
    headrq.timestamp=timestamp;
    SendData(clientfd,(char*)&headrq,sizeof(headrq));
}

void CLogic::DeleteFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    //拆包
    STRU_DELETE_FILE_RQ* rq=(STRU_DELETE_FILE_RQ*)szbuf;
    //id列表
    for(int i=0;i<rq->fileCount;++i){
        int fileid=rq->fileidArray[i];
        //删除每一项
        DeleteOneItem(rq->userid,fileid,rq->dir);
    }
    //写回复
    STRU_DELETE_FILE_RS rs;
    rs.result=1;
    strcpy(rs.dir,rq->dir);
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::DeleteOneItem(int userid,int fileid,string dir){
    //删除文件需要 u_id f_dir f_id
    //需要知道是什么类型 type name path
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(dir.c_str());
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"select f_type,f_name,f_path from user_file_info where u_id=%d and f_id=%d and f_dir='%s';",userid,fileid,sDir.c_str());
    list<string> lst;
    bool res=m_sql->SelectMysql(sqlbuf,3,lst);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }
    if(lst.size()==0) return;
    string type=lst.front();lst.pop_front();
    string name=lst.front();lst.pop_front();
    string path=lst.front();lst.pop_front();
    if(type=="file")
        DeleteFile(userid,fileid,dir,path);
    else
        DeleteFolder(userid,fileid,dir,name);
}

void CLogic::DeleteFile(int userid,int fileid,string dir,string path){
    //删除用户文件对应关系
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(dir.c_str());
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"delete from t_user_file where u_id=%d and f_id=%d and f_dir='%s';",userid,fileid,sDir.c_str());
    bool res=m_sql->UpdataMysql(sqlbuf);
    if(!res){
        cout<<"delete fail:"<<sqlbuf<<endl;
        return;
    }
    //再次查询id看能不能找到数据库记录，如果不能，删除本地文件
    sprintf(sqlbuf,"select f_id from t_file where f_id=%d;",fileid);
    list<string> lst;
    res=m_sql->SelectMysql(sqlbuf,1,lst);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }
    if(lst.size()==0){
        unlink(path.c_str());  //文件io 删除文件
    }
}

void CLogic::DeleteFolder(int userid,int fileid,string dir,string name){
    //删除用户文件对应关系 u_id f_dir f_id
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(dir.c_str());
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"delete from t_user_file where u_id=%d and f_id=%d and f_dir='%s';",userid,fileid,sDir.c_str());
    bool res=m_sql->UpdataMysql(sqlbuf);
    if(!res){
        cout<<"delete fail:"<<sqlbuf<<endl;
        return;
    }
    //拼接新路径
    std::string newdir=dir+name+"/";
    //查表 根据新路径查表 得到列表 f_type f_id name path
    std::string sNewdir = SqlEscape(newdir.c_str());
    sprintf(sqlbuf,"select f_type,f_id,f_name,f_path from user_file_info where u_id=%d and f_dir='%s';",userid,sNewdir.c_str());
    list<string> lst;
    res=m_sql->SelectMysql(sqlbuf,4,lst);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }

    while(lst.size()!=0){
        //循环
        string type=lst.front();lst.pop_front();
        int fileid=stoi(lst.front());lst.pop_front();
        string name=lst.front();lst.pop_front();
        string path=lst.front();lst.pop_front();
        if(type=="file")  //如果是文件
            DeleteFile(userid, fileid,newdir,path);
        else  //如果是文件夹
            DeleteFolder(userid, fileid,newdir, name);
    }
}

void CLogic::ContinueDownloadRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_CONTINUE_DOWNLOAD_RQ* rq=(STRU_CONTINUE_DOWNLOAD_RQ*)szbuf;
    //看是否存在文件信息
    int64_t user_time=(int64_t)rq->userid*getNumber()+rq->timestamp;
    FileInfo* info=nullptr;
    bool bCreated=false;  //【MD5校验】本次是否新建了文件信息（拒绝续传时需要回收）
    if(!m_mapTimestampToFileInfo.find(user_time,info)){
        //没有 创建文件信息（由查表而来） 添加到map
        info=new FileInfo;
        bCreated=true;
        //查数据库 查f_name,f_path,f_MD5,f_size 如果没有 返回
        //目录转义后再拼 SQL
        std::string sDir = SqlEscape(rq->dir);
        char sqlbuf[1000]="";
        sprintf(sqlbuf,"SELECT t_user_file.f_name, t_file.f_path, t_file.f_MD5, t_file.f_size "
                        "FROM t_user_file JOIN t_file ON t_user_file.f_id = t_file.f_id "
                        "WHERE t_user_file.u_id = %d AND t_user_file.f_dir = '%s' AND t_user_file.f_id = %d",rq->userid,sDir.c_str(),rq->fileid);
        list<string> lstRes;
        bool res=m_sql->SelectMysql(sqlbuf,4,lstRes);
        if(!res){
            cout<<"select fail:"<<sqlbuf<<endl;
            delete info;
            return;
        }
        if(lstRes.size()==0){ delete info; return; }
        string strName=lstRes.front(); lstRes.pop_front();
        string strPath=lstRes.front(); lstRes.pop_front();
        string strMD5=lstRes.front(); lstRes.pop_front();
        int size=stoi(lstRes.front()); lstRes.pop_front();
        //有 写文件信息
        //删除原先重复声明的局部变量 info（它会遮蔽外层指针，
        info->absolutePath=strPath;
        info->dir=rq->dir;
        info->fid=rq->fileid;
        info->md5=strMD5;
        info->name=strName;
        info->size=size;
        info->type="file";
        info->fileFd=open(info->absolutePath.c_str(),O_RDONLY);
        if(info->fileFd<=0){
            cout<<"file open fail"<<endl;
            delete info;
            return;
        }
        m_mapTimestampToFileInfo.insert(user_time,info);
    }
    //【MD5校验】续传前校验：客户端本地已下载部分的MD5 与 服务端文件前pos字节的MD5 比对，
    STRU_CONTINUE_DOWNLOAD_RS ckrs;
    ckrs.fileid=rq->fileid;
    ckrs.timestamp=rq->timestamp;
    ckrs.result=1;
    bool bMd5Match=false;
    if(rq->pos >= 0 && rq->pos <= info->size){
        string serverPartialMD5 = ComputeFileMD5(info->absolutePath.c_str(), rq->pos);
        bMd5Match = (serverPartialMD5 == rq->md5);
        if(!bMd5Match)
            printf("【续传拒绝】下载续传MD5不一致: 客户端本地(%s) 服务端前%d字节(%s)\n",
                   rq->md5, rq->pos, serverPartialMD5.c_str());
    }else{
        printf("【续传拒绝】下载续传位置非法 pos=%d size=%d\n", rq->pos, info->size);
    }
    if(!bMd5Match){
        ckrs.result=0;
        SendData(clientfd,(char*)&ckrs,sizeof(ckrs));
        //拒绝：回收本次创建的文件信息，客户端应删除该任务后重新完整下载
        if(bCreated){
            close(info->fileFd);
            m_mapTimestampToFileInfo.erase(user_time);
            delete info;
        }
        return;
    }
    //发送"允许续传"回复，随后照常开始发文件块
    SendData(clientfd,(char*)&ckrs,sizeof(ckrs));
    //有信息了
    //文件指针跳转 pos位置 同步pos
    lseek(info->fileFd,rq->pos,SEEK_SET);  //SET是起始位置
    info->pos=rq->pos;
    //读文件块 发送文件块请求
    STRU_FILE_CONTENT_RQ contentRq;
    contentRq.fileid=rq->fileid;
    contentRq.timestamp=rq->timestamp;
    contentRq.userid=rq->userid;
    //原代码对大文件(>=100MB)回一个 STRU_CHUNK_DOWNLOAD_RS 提示客户端
    //走分片下载，但客户端收到该包后 len=0 直接忽略，也不会再发分片请求，    //续传统一直接发文件块即可，与客户端“回复后服务端直接发块”的约定一致。    int firstLen = std::min(_DEF_BUFFER, info->size - info->pos);
    SendFileContentZeroCopy(clientfd, info->fileFd, firstLen,
                            rq->timestamp, rq->userid, rq->fileid);
}

void CLogic::ContinueUploadRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_CONTINUE_UPLOAD_RQ* rq=(STRU_CONTINUE_UPLOAD_RQ*)szbuf;
    int64_t user_time=(int64_t)rq->userid*getNumber()+rq->timestamp;
    auto sendReject=[&](){
        STRU_CONTINUE_UPLOAD_RS rs;
        rs.fileid=rq->fileid;
        rs.pos=-1;  //-1 表示拒绝续传
        rs.timestamp=rq->timestamp;
        SendData(clientfd,(char*)&rs,sizeof(rs));
    };
    //看map中是否存在
    FileInfo* info=nullptr;
    bool bCreated=false;  //【MD5校验】本次是否新建了文件信息（拒绝续传时需要回收）
    if(!m_mapTimestampToFileInfo.find(user_time,info)){
        //不存在 创建
        info=new FileInfo;
        bCreated=true;
        info->dir=rq->dir;
        info->fid=rq->fileid;
        info->type="file";
        //查表 获取信息 查4列 分别是f_path,f_md5,f_name,f_size
        //目录转义后再拼 SQL
        std::string sDir = SqlEscape(rq->dir);
        char sqlbuf[1000]="";
        sprintf(sqlbuf,"select f_name,f_path,f_size,f_MD5 from user_file_info where u_id=%d and f_id=%d and f_dir='%s' and f_state=0;",rq->userid,rq->fileid,sDir.c_str());//f_state=0 表示未传完，只有未传完的记录才能续传
        list<string> lst;
        bool res=m_sql->SelectMysql(sqlbuf,4,lst);
        if(!res){
            cout<<"select fail:"<<sqlbuf<<endl;
            delete info;
            sendReject();
            return;
        }
        if(lst.size()==0){
            delete info;
            sendReject();
            return;
        }
        info->name=lst.front();lst.pop_front();
        info->absolutePath=lst.front();lst.pop_front();
        info->size=stoi(lst.front());lst.pop_front();
        info->md5=lst.front();lst.pop_front();
        //给info赋值 打开文件 info加到map中        info->fileFd=open(info->absolutePath.c_str(),O_WRONLY);
        if(info->fileFd<=0){
            cout<<"file open fail:"<<errno<<endl;
            delete info;
            sendReject();
            return;
        }
        m_mapTimestampToFileInfo.insert(user_time,info);
    }
    //【MD5校验】续传前比对本地文件MD5与服务端记录的MD5，
    if(strcmp(rq->md5, info->md5.c_str()) != 0){
        printf("【续传拒绝】上传续传MD5不一致: 客户端本地(%s) 服务端记录(%s)\n",
               rq->md5, info->md5.c_str());
        if(bCreated){
            close(info->fileFd);
            m_mapTimestampToFileInfo.erase(user_time);
            delete info;
        }
        sendReject();
        return;
    }
    //现在已经有信息 lseek跳转并读取文件当前位置（文件末尾） 更新pos
    info->pos=lseek(info->fileFd,0,SEEK_END);
    //写回复 返回
    STRU_CONTINUE_UPLOAD_RS rs;
    rs.fileid=rq->fileid;
    rs.pos=info->pos;
    rs.timestamp=rq->timestamp;
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::FileHeaderRs(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_FILE_HEADER_RS* rs=(STRU_FILE_HEADER_RS*)szbuf;
    //拿到文件信息
    int64_t user_time=(int64_t)rs->userid*getNumber()+rs->timestamp;
    FileInfo*info=nullptr;
    if(!m_mapTimestampToFileInfo.find(user_time,info)) return;
    //发文件内容请求
    STRU_FILE_CONTENT_RQ rq;
    //读文件
    int firstLen = std::min(_DEF_BUFFER, info->size - info->pos);
    SendFileContentZeroCopy(clientfd, info->fileFd, firstLen,
                            rs->timestamp, rs->userid, rs->fileid);
}

void CLogic::FileContentRs(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_FILE_CONTENT_RS* rs=(STRU_FILE_CONTENT_RS*) szbuf;
    //文件信息结构
    int64_t user_time=(int64_t)rs->userid*getNumber()+rs->timestamp;
    FileInfo* info=nullptr;
    if(!m_mapTimestampToFileInfo.find(user_time,info)) return;
    //判断是否成功
    if(rs->result!=1){
        //不成功 跳回去
        lseek(info->fileFd,-1*(rs->len),SEEK_CUR);
    }
    else{
        //成功 pos+=len
        info->pos+=rs->len;
        //判断是否结束
        if(info->pos>=info->size){
            //是 关闭文件 回收 退出
            close(info->fileFd);
            m_mapTimestampToFileInfo.erase(user_time);
            delete info;
            info=nullptr;
            return;
        }
    }
    //写文件内容请求
    STRU_FILE_CONTENT_RQ rq;
    //读文件
    int nextLen = std::min(_DEF_BUFFER, info->size - info->pos);
    if (nextLen <= 0) return;
    rq.fileid=rs->fileid;
    rq.timestamp=rs->timestamp;
    rq.userid=rs->userid;
    //发送
    SendFileContentZeroCopy(clientfd, info->fileFd, nextLen,
                            rs->timestamp, rs->userid, rs->fileid);
}

void CLogic::AddFolderRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_ADD_FOLDER_RQ* rq=(STRU_ADD_FOLDER_RQ*)szbuf;
    //文件夹名/目录转义后再拼 SQL
    std::string sFileName = SqlEscape(rq->fileName);
    std::string sDir      = SqlEscape(rq->dir);
    //数据库写表，插入文件信息
    //f_size,f_path,f_count,f_MD5,f_state,f_type
    char pathbuf[1000]="";
    sprintf(pathbuf,"%s%d%s%s",DEF_PATH,rq->userid,rq->dir,rq->fileName);
    std::string sPath = SqlEscape(pathbuf);
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"insert into t_file(f_size,f_path,f_count,f_MD5,f_state,f_type) values(0,'%s',0,'?',1,'folder');",sPath.c_str());
    bool res=m_sql->UpdataMysql(sqlbuf);
    if(!res){
        cout<<"update fail:"<<sqlbuf<<endl;
        return;
    }
    //查询id
    sprintf(sqlbuf,"select f_id from t_file where f_path='%s';",sPath.c_str());
    list<string>lstRes;
    res=m_sql->SelectMysql(sqlbuf,1,lstRes);
    if(!res){
        cout<<"SelectMysql fail:"<<sqlbuf<<endl;
        return;
    }
    if(lstRes.size()==0) return;
    int id=stoi(lstRes.front());
    lstRes.pop_front();
    //写入用户文件关系-- 隐藏 触发器引用计数会+1
    //u_id,f_id,f_dir,f_name,f_uploadtime
    sprintf(sqlbuf,"insert into t_user_file(u_id,f_id,f_dir,f_name,f_uploadtime) values(%d,%d,'%s','%s','%s');",rq->userid,id,sDir.c_str(),sFileName.c_str(),rq->time);
    res=m_sql->UpdataMysql(sqlbuf);
    if(!res){
        cout<<"update fail:"<<sqlbuf<<endl;
        return;
    }
    //创建目录
    umask(0);
    CreateDirChain(pathbuf);
    //写回复
    STRU_ADD_FOLDER_RS rs;
    rs.result=1;
    rs.timestamp=rq->timestamp;
    rs.userid=rq->userid;
    //发送
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::ShareFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    //拆包
    STRU_SHARE_FILE_RQ* rq=(STRU_SHARE_FILE_RQ*)szbuf;
    //随机生成分享链接
    //分享码规则 9位
    //"生成-查重-写库"整段持锁：锁内先随机出码、再查表去重、
    //再把码写到所有分享项上，锁外才回包。并发分享时同一时刻只有一个请求    //在分配码，不会两个请求拿到同一个码。    pthread_mutex_lock(&g_shareLinkLock);
    int link=0;
    do{
        link=1+random()%9;  //随机出1-9
        link*=100000000;
        link+=random()%100000000;
        //去重 查链接是否已经存在
        char sqlbuf[1000]="";
        sprintf(sqlbuf,"select s_link from t_user_file where s_link=%d;",link);
        list<string> lstRes;
        bool res=m_sql->SelectMysql(sqlbuf,1,lstRes);
        if(!res){
            //置 link=0 重新随机一个码再查            cout<<"select fail:"<<sqlbuf<<endl;
            link=0;
            continue;
        }
        if(lstRes.size()>0)
            link=0;
    }while(link==0);
    //遍历所有文件，将其分享链接设置
    int itemCount=rq->itemCount;
    for(int i=0;i<itemCount;++i){
        ShareItem(rq->userid,rq->fileidArray[i],rq->dir,rq->shareTime,link);
    }
    pthread_mutex_unlock(&g_shareLinkLock);
    //写回复
    STRU_SHARE_FILE_RS rs;
    rs.result=1;
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::MyShareRq(sock_fd clientfd, char *szbuf, int nlen)
{
    //拆包
    STRU_MY_SHARE_RQ * rq=(STRU_MY_SHARE_RQ*)szbuf;
    //根据id查询 获取分享文件列表
    //查的内容f_name f_size s_link
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"select f_name, f_size, s_linkTime, s_link from user_file_info where u_id = %d AND s_link > 0",rq->userid);
    list<string> lst;
    bool res=m_sql->SelectMysql(sqlbuf,4,lst);
    if(!res){
        cout<<"Select fail"<<sqlbuf<<endl;
        return;
    }
    int count=lst.size();
    if((count/4==0)||(count%4!=0)) return;

    count/=4;
    //写回复
    int packlen=sizeof(STRU_MY_SHARE_RS)+count*sizeof(STRU_MY_SHARE_FILE);
    STRU_MY_SHARE_RS* rs=(STRU_MY_SHARE_RS*)malloc(packlen);
    rs->init();
    rs->itemCount=count;
    for(int i=0;i<count;++i){
        string name=lst.front(); lst.pop_front();
        long long size = stoll(lst.front()); lst.pop_front();
        string time=lst.front(); lst.pop_front();
        int link=stoi(lst.front()); lst.pop_front();

        strcpy(rs->items[i].name,name.c_str());
        rs->items[i].size=size;
        strcpy(rs->items[i].time,time.c_str());
        rs->items[i].shareLink=link;
    }
    //发送
    SendData(clientfd,(char*)rs,packlen);
    free(rs);
}

void CLogic::GetShareRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_;
    STRU_GET_SHARE_RQ* rq=(STRU_GET_SHARE_RQ*)szbuf;
    //拆包
    //根据分享码 查询到一系列文件
    //查询属性：f_id  f_name  f_dir（分享人的）  f_type u_id（分享人的）
    //select f_id,f_name,f_dir,f_type,u_id from user_file_info where s_link=205637607;
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"select f_id,f_name,f_dir,f_type,u_id from user_file_info where s_link=%d;",rq->shareLink);
    list<string> lst;
    bool res=m_sql->SelectMysql(sqlbuf,5,lst);
    if(!res){
        cout<<"select fail:"<<sqlbuf<<endl;
        return;
    }
    STRU_GET_SHARE_RS rs;
    if(lst.size()==0){
        rs.result=0;
        SendData(clientfd,(char*)&rs,sizeof(rs));
        return;
    }
    rs.result=1;
    if(lst.size()%5!=0) return;
    //遍历文件列表
    while(lst.size()!=0){
        int fileid=stoi(lst.front());lst.pop_front();
        string name=lst.front(); lst.pop_front();
        string fromdir=lst.front(); lst.pop_front();
        string type=lst.front(); lst.pop_front();
        int fromuserid=stoi(lst.front());lst.pop_front();
        if(type=="file"){
        //如果是文件
            //插入信息到用户文件关系表
            GetShareByFile(rq->userid,fileid,rq->dir,name,rq->time);
        }else{
            //如果是文件夹
            GetShareByFolder(rq->userid,fileid,rq->dir,name,rq->time,fromuserid,fromdir);
        }
    }
    //写回复包
    strcpy(rs.dir,rq->dir);
    //发送
    SendData(clientfd,(char*)&rs,sizeof(rs));
}

void CLogic::GetShareByFile(int userid,int fileid,string dir,string name,string time){
    //目录/名字转义后再拼 SQL
    std::string sDir  = SqlEscape(dir.c_str());
    std::string sName = SqlEscape(name.c_str());
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"insert into t_user_file(u_id, f_id, f_dir , f_name ,f_uploadtime ) values( %d,%d,'%s','%s','%s'); ",userid,fileid,sDir.c_str(),sName.c_str(),time.c_str());
    bool res=m_sql->UpdataMysql(sqlbuf);
    if(!res)
        printf("update fail:%s\n",sqlbuf);
}

void CLogic::GetShareByFolder(int userid,int fileid,string dir,string name,string time,int fromuserid,string fromdir){
    //如果是文件夹
        //插入信息到 用户文件关系表
        GetShareByFile( userid,fileid,dir, name, time);
        //拼接路径 获取人目录 /->/06/
        string newdir=dir+name+"/";
        //拼接路径 分享人的目录 /->/06/
        string newfromdir=fromdir+name+"/";
        //根据新路径 在分享人那边查询 文件夹下的文件
        //目录转义后再拼 SQL
        std::string sNewfromdir = SqlEscape(newfromdir.c_str());
        char sqlbuf[1000]="";
        sprintf(sqlbuf,"select f_id,f_name,f_type from user_file_info where u_id=%d and f_dir='%s';",fromuserid,sNewfromdir.c_str());
        list<string> lst;
        bool res=m_sql->SelectMysql(sqlbuf,3,lst);
        if(!res){
            cout<<"select fail:"<<sqlbuf<<endl;
            return;
        }
        //遍历列表  -->递归
        while(lst.size()!=0){
            int fileid=stoi(lst.front()); lst.pop_front();
            string name=lst.front(); lst.pop_front();
            string type=lst.front(); lst.pop_front();
            if(type=="file")  //文件
                GetShareByFile(userid,fileid,newdir,name,time);
            else  //文件夹
                GetShareByFolder(userid,fileid,newdir,name,time,fromuserid,newfromdir);
        }
}

void CLogic::ShareItem(int userid, int fileid, string dir, string time, int link)
{
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(dir.c_str());
    char sqlbuf[1000]="";
    sprintf(sqlbuf,"update t_user_file set s_link=%d,s_linkTime='%s' where u_id =%d and f_id =%d and f_dir ='%s';",link,time.c_str(),userid,fileid,sDir.c_str());
    bool res=m_sql->UpdataMysql(sqlbuf);
    if(!res){
        cout<<"UpdateMysql fail"<<sqlbuf<<endl;
        return;
    }
}

/* =====================================================================
 * 【新增代码】以下为客户端新增功能的服务端实现（搜索/收藏/回收站/分享预览），
 * 全部为追加内容，未改动上方任何原有代码。
 * ===================================================================== */

//文件搜索请求：按关键字模糊查询当前用户的文件列表void CLogic::SearchFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_SEARCH_FILE_RQ* rq = (STRU_SEARCH_FILE_RQ*)szbuf;
    //按关键字模糊查询用户的所有文件（只查已完成上传的文件）
    std::string sKeyword = SqlEscape(rq->keyword, true);
    char sqlbuf[1000] = "";
    sprintf(sqlbuf, "select uf.f_id, uf.f_name, uf.f_dir, t.f_size, uf.f_uploadtime, t.f_type "
                    "from t_user_file uf join t_file t on uf.f_id = t.f_id "
                    "where uf.u_id = %d and t.f_state = 1 and uf.f_name like '%%%s%%';",
            rq->userid, sKeyword.c_str());
    list<string> lstRes;
    bool res = m_sql->SelectMysql(sqlbuf, 6, lstRes);
    if(!res){
        cout << "select fail:" << sqlbuf << endl;
        return;
    }
    int count = lstRes.size() / 6;
    //写回复包
    int packlen = sizeof(STRU_SEARCH_FILE_RS) + count * sizeof(STRU_SEARCH_RESULT_ITEM);
    STRU_SEARCH_FILE_RS* rs = (STRU_SEARCH_FILE_RS*)malloc(packlen);
    rs->init();
    rs->count = count;
    for(int i = 0; i < count; ++i){
        int fid = stoi(lstRes.front()); lstRes.pop_front();
        string name = lstRes.front(); lstRes.pop_front();
        string dir = lstRes.front(); lstRes.pop_front();
        int size = stoi(lstRes.front()); lstRes.pop_front();
        string time = lstRes.front(); lstRes.pop_front();
        string type = lstRes.front(); lstRes.pop_front();
        rs->items[i].fileid = fid;
        strcpy(rs->items[i].name, name.c_str());
        strcpy(rs->items[i].dir, dir.c_str());
        rs->items[i].size = size;
        strcpy(rs->items[i].time, time.c_str());
        strcpy(rs->items[i].fileType, type.c_str());
    }
    SendData(clientfd, (char*)rs, packlen);
    free(rs);
}

//收藏/取消收藏请求void CLogic::FavoriteFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_FAVORITE_FILE_RQ* rq = (STRU_FAVORITE_FILE_RQ*)szbuf;
    STRU_FAVORITE_FILE_RS rs;
    rs.result = 1;
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(rq->dir);
    char sqlbuf[1000] = "";
    for(int i = 0; i < rq->fileCount; ++i){
        int fid = rq->fileidArray[i];
        if(rq->command == FAVORITE_ADD){
            sprintf(sqlbuf, "select f_id from t_favorite where u_id = %d and f_id = %d and f_dir = '%s';",
                    rq->userid, fid, sDir.c_str());
            list<string> lst;
            bool res = m_sql->SelectMysql(sqlbuf, 1, lst);
            if(!res){
                cout << "select fail:" << sqlbuf << endl;
                continue;
            }
            if(lst.size() == 0){
                sprintf(sqlbuf, "insert into t_favorite(u_id, f_id, f_dir) values(%d, %d, '%s');",
                        rq->userid, fid, sDir.c_str());
                m_sql->UpdataMysql(sqlbuf);
            }
        }else{
            //取消收藏
            sprintf(sqlbuf, "delete from t_favorite where u_id = %d and f_id = %d and f_dir = '%s';",
                    rq->userid, fid, sDir.c_str());
            m_sql->UpdataMysql(sqlbuf);
        }
    }
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

//获取收藏列表请求void CLogic::GetFavoritesRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_GET_FAVORITES_RQ* rq = (STRU_GET_FAVORITES_RQ*)szbuf;
    //连接收藏表、用户文件表和文件表，查出收藏的文件信息
    char sqlbuf[1000] = "";
    sprintf(sqlbuf, "select uf.f_id, uf.f_name, uf.f_dir, t.f_size, uf.f_uploadtime, t.f_type "
                    "from t_favorite f "
                    "join t_user_file uf on f.u_id = uf.u_id and f.f_id = uf.f_id and f.f_dir = uf.f_dir "
                    "join t_file t on uf.f_id = t.f_id "
                    "where f.u_id = %d and t.f_state = 1;", rq->userid);
    list<string> lstRes;
    bool res = m_sql->SelectMysql(sqlbuf, 6, lstRes);
    if(!res){
        cout << "select fail:" << sqlbuf << endl;
        return;
    }
    int count = lstRes.size() / 6;
    //写回复包
    int packlen = sizeof(STRU_GET_FAVORITES_RS) + count * sizeof(STRU_FAVORITE_ITEM);
    STRU_GET_FAVORITES_RS* rs = (STRU_GET_FAVORITES_RS*)malloc(packlen);
    rs->init();
    rs->count = count;
    for(int i = 0; i < count; ++i){
        int fid = stoi(lstRes.front()); lstRes.pop_front();
        string name = lstRes.front(); lstRes.pop_front();
        string dir = lstRes.front(); lstRes.pop_front();
        int size = stoi(lstRes.front()); lstRes.pop_front();
        string time = lstRes.front(); lstRes.pop_front();
        string type = lstRes.front(); lstRes.pop_front();
        rs->items[i].fileid = fid;
        strcpy(rs->items[i].name, name.c_str());
        strcpy(rs->items[i].dir, dir.c_str());
        rs->items[i].size = size;
        strcpy(rs->items[i].time, time.c_str());
        strcpy(rs->items[i].fileType, type.c_str());
    }
    SendData(clientfd, (char*)rs, packlen);
    free(rs);
}

//移入回收站请求：把文件从用户文件列表移入回收站表（保留物理文件，便于恢复）void CLogic::RecycleFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_RECYCLE_FILE_RQ* rq = (STRU_RECYCLE_FILE_RQ*)szbuf;
    //当前时间作为删除时间
    time_t now = time(NULL);
    struct tm* ptm = localtime(&now);
    char delTime[64] = "";
    strftime(delTime, sizeof(delTime), "%Y-%m-%d %H:%M:%S", ptm);
    //目录转义后再拼 SQL
    std::string sDir = SqlEscape(rq->dir);
    char sqlbuf[1000] = "";
    for(int i = 0; i < rq->fileCount; ++i){
        int fid = rq->fileidArray[i];
        //查出文件的类型、名字、物理路径、md5、大小
        sprintf(sqlbuf, "select uf.f_name, t.f_type, t.f_path, t.f_MD5, t.f_size "
                        "from t_user_file uf join t_file t on uf.f_id = t.f_id "
                        "where uf.u_id = %d and uf.f_id = %d and uf.f_dir = '%s';",
                rq->userid, fid, sDir.c_str());
        list<string> lst;
        bool res = m_sql->SelectMysql(sqlbuf, 5, lst);
        if(!res){
            cout << "select fail:" << sqlbuf << endl;
            continue;
        }
        if(lst.size() == 0) continue;
        string name = lst.front(); lst.pop_front();
        string type = lst.front(); lst.pop_front();
        string path = lst.front(); lst.pop_front();
        string md5  = lst.front(); lst.pop_front();
        int size = stoi(lst.front()); lst.pop_front();
        std::string sName = SqlEscape(name.c_str());
        std::string sPath = SqlEscape(path.c_str());
        std::string sMd5  = SqlEscape(md5.c_str());
        sprintf(sqlbuf, "insert into t_recycle(u_id, f_id, f_dir, f_name, f_type, f_size, f_path, f_MD5, del_time) "
                        "values(%d, %d, '%s', '%s', '%s', %d, '%s', '%s', '%s') "
                        "on duplicate key update f_dir=values(f_dir), f_name=values(f_name), "
                        "f_type=values(f_type), f_size=values(f_size), f_path=values(f_path), "
                        "f_MD5=values(f_MD5), del_time=values(del_time);",
                rq->userid, fid, sDir.c_str(), sName.c_str(), type.c_str(), size, sPath.c_str(), sMd5.c_str(), delTime);
        if (!m_sql->UpdataMysql(sqlbuf)) {
            cout << "recycle insert/update fail:" << sqlbuf << endl;
            continue;
        }
        //从用户文件列表移除（沿用现有删除关系的SQL写法）
        sprintf(sqlbuf, "delete from t_user_file where u_id = %d and f_id = %d and f_dir = '%s';",
                rq->userid, fid, sDir.c_str());
        m_sql->UpdataMysql(sqlbuf);
    }
    //写回复
    STRU_RECYCLE_FILE_RS rs;
    rs.result = 1;
    strcpy(rs.dir, rq->dir);
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

//获取回收站列表请求void CLogic::GetRecycleRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_GET_RECYCLE_RQ* rq = (STRU_GET_RECYCLE_RQ*)szbuf;
    char sqlbuf[1000] = "";
    sprintf(sqlbuf, "select f_id, f_name, f_dir, f_size, del_time, f_type from t_recycle where u_id = %d;",
            rq->userid);
    list<string> lstRes;
    bool res = m_sql->SelectMysql(sqlbuf, 6, lstRes);
    if(!res){
        cout << "select fail:" << sqlbuf << endl;
        return;
    }
    int count = lstRes.size() / 6;
    //写回复包
    int packlen = sizeof(STRU_GET_RECYCLE_RS) + count * sizeof(STRU_RECYCLE_ITEM);
    STRU_GET_RECYCLE_RS* rs = (STRU_GET_RECYCLE_RS*)malloc(packlen);
    rs->init();
    rs->count = count;
    for(int i = 0; i < count; ++i){
        int fid = stoi(lstRes.front()); lstRes.pop_front();
        string name = lstRes.front(); lstRes.pop_front();
        string dir = lstRes.front(); lstRes.pop_front();
        int size = stoi(lstRes.front()); lstRes.pop_front();
        string delTime = lstRes.front(); lstRes.pop_front();
        string type = lstRes.front(); lstRes.pop_front();
        rs->items[i].fileid = fid;
        strcpy(rs->items[i].name, name.c_str());
        strcpy(rs->items[i].dir, dir.c_str());
        rs->items[i].size = size;
        strcpy(rs->items[i].deleteTime, delTime.c_str());
        strcpy(rs->items[i].fileType, type.c_str());
    }
    SendData(clientfd, (char*)rs, packlen);
    free(rs);
}

//从回收站恢复请求：把文件重新加回用户文件列表void CLogic::RestoreFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_RESTORE_FILE_RQ* rq = (STRU_RESTORE_FILE_RQ*)szbuf;
    STRU_RESTORE_FILE_RS rs;
    rs.result = 1;
    char sqlbuf[1000] = "";
    //当前时间作为恢复时间
    time_t now = time(NULL);
    struct tm* ptm = localtime(&now);
    char restoreTime[64] = "";
    strftime(restoreTime, sizeof(restoreTime), "%Y-%m-%d %H:%M:%S", ptm);
    for(int i = 0; i < rq->fileCount; ++i){
        int fid = rq->fileidArray[i];
        //从回收站表取出原始信息
        sprintf(sqlbuf, "select f_dir, f_name, f_type, f_size, f_path, f_MD5 from t_recycle where u_id = %d and f_id = %d;",
                rq->userid, fid);
        list<string> lst;
        bool res = m_sql->SelectMysql(sqlbuf, 6, lst);
        if(!res || lst.size() == 0){
            rs.result = 0;
            continue;
        }
        string dir  = lst.front(); lst.pop_front();
        string name = lst.front(); lst.pop_front();
        string type = lst.front(); lst.pop_front();
        int size = stoi(lst.front()); lst.pop_front();
        string path = lst.front(); lst.pop_front();
        string md5  = lst.front(); lst.pop_front();
        std::string sDir  = SqlEscape(dir.c_str());
        std::string sName = SqlEscape(name.c_str());
        std::string sPath = SqlEscape(path.c_str());
        std::string sMd5  = SqlEscape(md5.c_str());
        sprintf(sqlbuf, "select f_id from t_file where f_path = '%s' and f_MD5 = '%s';", sPath.c_str(), sMd5.c_str());
        list<string> lstTmp;
        m_sql->SelectMysql(sqlbuf, 1, lstTmp);
        if(lstTmp.size() == 0){
            sprintf(sqlbuf, "insert into t_file(f_size, f_path, f_count, f_MD5, f_state, f_type) values(%d, '%s', 0, '%s', 1, '%s');",
                    size, sPath.c_str(), sMd5.c_str(), type.c_str());
            m_sql->UpdataMysql(sqlbuf);
            sprintf(sqlbuf, "select f_id from t_file where f_path = '%s' and f_MD5 = '%s';", sPath.c_str(), sMd5.c_str());
            list<string> lstNew;
            m_sql->SelectMysql(sqlbuf, 1, lstNew);
            if(lstNew.size() > 0)
                fid = stoi(lstNew.front());
        }
        //重新加入用户文件列表
        sprintf(sqlbuf, "insert into t_user_file(u_id, f_id, f_dir, f_name, f_uploadtime) values(%d, %d, '%s', '%s', '%s');",
                rq->userid, fid, sDir.c_str(), sName.c_str(), restoreTime);
        m_sql->UpdataMysql(sqlbuf);
        //从回收站表移除
        sprintf(sqlbuf, "delete from t_recycle where u_id = %d and f_id = %d;", rq->userid, fid);
        m_sql->UpdataMysql(sqlbuf);
    }
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

//回收站彻底删除请求：删除回收站记录并清理物理文件void CLogic::DeleteForeverRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_DELETE_FOREVER_RQ* rq = (STRU_DELETE_FOREVER_RQ*)szbuf;
    STRU_DELETE_FOREVER_RS rs;
    rs.result = 1;
    char sqlbuf[1000] = "";
    for(int i = 0; i < rq->fileCount; ++i){
        int fid = rq->fileidArray[i];
        //从回收站表取出信息
        sprintf(sqlbuf, "select f_dir, f_name, f_type, f_path, f_MD5 from t_recycle where u_id = %d and f_id = %d;",
                rq->userid, fid);
        list<string> lst;
        bool res = m_sql->SelectMysql(sqlbuf, 5, lst);
        if(!res || lst.size() == 0) continue;
        string dir  = lst.front(); lst.pop_front();
        string name = lst.front(); lst.pop_front();
        string type = lst.front(); lst.pop_front();
        string path = lst.front(); lst.pop_front();
        string md5  = lst.front(); lst.pop_front();
        std::string sPath = SqlEscape(path.c_str());
        std::string sMd5  = SqlEscape(md5.c_str());
        //如果是文件夹：清理其子文件关系（子文件不在回收站表中）
        if(type != "file"){
            string childDir = dir + name + "/";
            std::string sChildDir = SqlEscape(childDir.c_str());
            sprintf(sqlbuf, "delete from t_user_file where u_id = %d and f_dir like '%s%%';",
                    rq->userid, sChildDir.c_str());
            m_sql->UpdataMysql(sqlbuf);
        }
        //文件表中若已无该文件记录（引用计数归零被触发器删除），则删除磁盘物理文件
        if(type == "file"){
            sprintf(sqlbuf, "select f_id from t_file where f_path = '%s' and f_MD5 = '%s';",
                    sPath.c_str(), sMd5.c_str());
            list<string> lstFile;
            m_sql->SelectMysql(sqlbuf, 1, lstFile);
            if(lstFile.size() == 0){
                unlink(path.c_str());
            }
        }
        //删除回收站记录
        sprintf(sqlbuf, "delete from t_recycle where u_id = %d and f_id = %d;", rq->userid, fid);
        m_sql->UpdataMysql(sqlbuf);
    }
    SendData(clientfd, (char*)&rs, sizeof(rs));
}

//浏览分享目录请求（预览用，不复制文件到自己网盘）void CLogic::BrowseShareRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_BROWSE_SHARE_RQ* rq = (STRU_BROWSE_SHARE_RQ*)szbuf;
    string subDir = rq->subDir;
    if(subDir.empty()) subDir = "/";
    char sqlbuf[1000] = "";
    list<string> lstRes;
    if(subDir == "/"){
        //根目录：直接列出该分享链接对应的条目（分享人当时勾选的文件/文件夹）
        sprintf(sqlbuf, "select uf.f_id, uf.f_name, t.f_size, uf.f_uploadtime, t.f_type "
                        "from t_user_file uf join t_file t on uf.f_id = t.f_id "
                        "where uf.s_link = %d and t.f_state = 1;", rq->shareLink);
        bool res = m_sql->SelectMysql(sqlbuf, 5, lstRes);
        if(!res){
            cout << "select fail:" << sqlbuf << endl;
            return;
        }
    }else{
        //子目录：先找到分享根条目，把浏览路径映射到分享人的真实目录
        sprintf(sqlbuf, "select u_id, f_dir, f_name, f_type from t_user_file where s_link = %d;", rq->shareLink);
        list<string> lstRoot;
        bool res = m_sql->SelectMysql(sqlbuf, 4, lstRoot);
        if(!res || lstRoot.size() == 0){
            //链接无效
            STRU_BROWSE_SHARE_RS rs;
            rs.init();
            rs.result = browse_share_invalid_link;
            strcpy(rs.curDir, subDir.c_str());
            SendData(clientfd, (char*)&rs, sizeof(rs));
            return;
        }
        int ownerid = 0;
        bool found = false;
        while(lstRoot.size() != 0 && !found){
            int uid = stoi(lstRoot.front()); lstRoot.pop_front();
            string rdir = lstRoot.front(); lstRoot.pop_front();
            string rname = lstRoot.front(); lstRoot.pop_front();
            string rtype = lstRoot.front(); lstRoot.pop_front();
            ownerid = uid;
            //分享的是文件夹：其子树内的所有目录都可以浏览
            if(rtype != "file"){
                string prefix = rdir + rname + "/";
                if(subDir == prefix || subDir.compare(0, prefix.size(), prefix) == 0){
                    found = true;
                }
            }
        }
        if(!found){
            //目录不存在：返回空列表
            STRU_BROWSE_SHARE_RS rs;
            rs.init();
            rs.result = browse_share_success;
            strcpy(rs.curDir, subDir.c_str());
            SendData(clientfd, (char*)&rs, sizeof(rs));
            return;
        }
        //查询分享人该目录下的文件列表
        //子目录路径来自客户端，转义后再拼 SQL
        std::string sSubDir = SqlEscape(subDir.c_str());
        sprintf(sqlbuf, "select uf.f_id, uf.f_name, t.f_size, uf.f_uploadtime, t.f_type "
                        "from t_user_file uf join t_file t on uf.f_id = t.f_id "
                        "where uf.u_id = %d and uf.f_dir = '%s' and t.f_state = 1;", ownerid, sSubDir.c_str());
        res = m_sql->SelectMysql(sqlbuf, 5, lstRes);
        if(!res){
            cout << "select fail:" << sqlbuf << endl;
            return;
        }
    }
    int count = lstRes.size() / 5;
    //写回复包
    int packlen = sizeof(STRU_BROWSE_SHARE_RS) + count * sizeof(STRU_FILE_INFO);
    STRU_BROWSE_SHARE_RS* rs = (STRU_BROWSE_SHARE_RS*)malloc(packlen);
    rs->init();
    rs->result = browse_share_success;
    strcpy(rs->curDir, subDir.c_str());
    rs->count = count;
    for(int i = 0; i < count; ++i){
        int fid = stoi(lstRes.front()); lstRes.pop_front();
        string name = lstRes.front(); lstRes.pop_front();
        int size = stoi(lstRes.front()); lstRes.pop_front();
        string time = lstRes.front(); lstRes.pop_front();
        string type = lstRes.front(); lstRes.pop_front();
        rs->items[i].fileid = fid;
        strcpy(rs->items[i].name, name.c_str());
        rs->items[i].size = size;
        strcpy(rs->items[i].time, time.c_str());
        strcpy(rs->items[i].fileType, type.c_str());
    }
    SendData(clientfd, (char*)rs, packlen);
    free(rs);
}

//下载分享文件请求（用于预览）void CLogic::DownloadShareFileRq(sock_fd clientfd, char *szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    //拆包
    STRU_DOWNLOAD_SHARE_FILE_RQ* rq = (STRU_DOWNLOAD_SHARE_FILE_RQ*)szbuf;
    //按分享链接和文件id查出文件信息
    char sqlbuf[1000] = "";
    sprintf(sqlbuf, "select uf.f_name, t.f_path, t.f_MD5, t.f_size "
                    "from t_user_file uf join t_file t on uf.f_id = t.f_id "
                    "where uf.s_link = %d and uf.f_id = %d and t.f_state = 1;", rq->shareLink, rq->fileid);
    list<string> lstRes;
    bool res = m_sql->SelectMysql(sqlbuf, 4, lstRes);
    if(!res || lstRes.size() == 0){
        //文件不存在：发一个 size 为负的文件头，客户端收到后弹出预览失败提示
        STRU_FILE_HEADER_RQ headrq;
        headrq.fileid = rq->fileid;
        headrq.size = -1;
        SendData(clientfd, (char*)&headrq, sizeof(headrq));
        return;
    }
    string strName = lstRes.front(); lstRes.pop_front();
    string strPath = lstRes.front(); lstRes.pop_front();
    string strMD5  = lstRes.front(); lstRes.pop_front();
    int size = stoi(lstRes.front()); lstRes.pop_front();
    //写文件信息，后续走与普通下载相同的"文件头+内容块"流程
    FileInfo* info = new FileInfo;
    info->absolutePath = strPath;
    info->dir = "/";  //客户端预览固定保存到网盘根目录
    info->fid = rq->fileid;
    info->md5 = strMD5;
    info->name = strName;
    info->size = size;
    info->type = "file";
    info->fileFd = open(info->absolutePath.c_str(), O_RDONLY);
    if(info->fileFd <= 0){
        cout << "file open fail" << endl;
        delete info;
        return;
    }
    //原实现 time(NULL)%1e9 以“秒”为粒度：同一秒内预览两个文件时
    //时间戳相同 -> map key 相同，后一个任务覆盖前一个，两路数据互相串包，    //下载内容交叉损坏（视频无法播放）。改为进程内自增计数器，保证每次唯一。    static int g_sharePreviewTsCounter = 0;
    int timestamp = 0;
    if (g_sharePreviewTsCounter == 0)
        g_sharePreviewTsCounter = (int)(time(NULL) % 1000000000);
    timestamp = __sync_add_and_fetch(&g_sharePreviewTsCounter, 1) % 1000000000;
    if (timestamp == 0)
        timestamp = __sync_add_and_fetch(&g_sharePreviewTsCounter, 1) % 1000000000;
    int64_t user_time = (int64_t)rq->userid * getNumber() + timestamp;
    //存到map里
    m_mapTimestampToFileInfo.insert(user_time, info);
    //发送文件头请求
    STRU_FILE_HEADER_RQ headrq;
    strcpy(headrq.dir, "/");
    headrq.fileid = rq->fileid;
    strcpy(headrq.fileName, info->name.c_str());
    strcpy(headrq.md5, info->md5.c_str());
    strcpy(headrq.fileType, "file");
    headrq.size = info->size;
    headrq.timestamp = timestamp;
    SendData(clientfd, (char*)&headrq, sizeof(headrq));
}

/* =====================================================================
 * 【新增代码】递归创建多级目录（等价于 mkdir -p）。
 * 背景：服务端把文件存到 DEF_PATH+用户id 目录下，该目录原本只在注册时用
 * mkdir 创建一层。一旦父目录（如 /home/colin/Netdisk）不存在，mkdir 静默失败，
 * 之后上传时 open() 就会失败，服务端打印 "file open fail" 且不回复客户端，
 * 表现为客户端点上传毫无反应。此函数按路径逐级创建目录，目录已存在时
 * mkdir 返回 -1 直接忽略即可。
 * ===================================================================== */
void CLogic::CreateDirChain(const char* path)
{
    if(!path || path[0] == '\0') return;
    char tmp[1024] = "";
    snprintf(tmp, sizeof(tmp), "%s", path);
    //去掉末尾多余的 '/'
    int len = strlen(tmp);
    while(len > 1 && tmp[len-1] == '/')
        tmp[--len] = '\0';
    //从第二个字符开始扫描（跳过根目录 '/'），每遇到一层就创建
    for(char* p = tmp + 1; *p; ++p){
        if(*p == '/'){
            *p = '\0';
            umask(0);
            mkdir(tmp, 0777);  //目录已存在时返回失败，忽略
            *p = '/';
        }
    }
    umask(0);
    mkdir(tmp, 0777);
}

//服务启动时确保存储根目录存在；创建失败时打印明确提示，方便排查void CLogic::InitStoragePath()
{
    CreateDirChain(DEF_PATH);
    if(access(DEF_PATH, F_OK) != 0)
        printf("【警告】无法创建存储目录 %s ，请检查服务器运行用户是否有权限，"
               "或修改 clogic.cpp 中的 DEF_PATH 宏\n", DEF_PATH);
}

/* =====================================================================
 * 【新增代码】计算文件MD5（limitBytes>0 时只计算前 limitBytes 字节，
 * 用于下载续传时校验本地半截文件与服务端文件前段内容是否一致）。
 * 失败返回空字符串。
 * ===================================================================== */
static std::string ComputeFileMD5(const char* path, int64_t limitBytes)
{
    MD5 md5;
    FILE* pFile = fopen(path, "rb");
    if(!pFile)
        return std::string();
    char buf[65536];
    int64_t total = 0;
    while(true){
        size_t want = sizeof(buf);
        if(limitBytes > 0 && total + (int64_t)want > limitBytes)
            want = (size_t)(limitBytes - total);
        size_t len = fread(buf, 1, want, pFile);
        if(len > 0){
            md5.update(buf, len);
            total += len;
        }
        if(len < want || (limitBytes > 0 && total >= limitBytes))
            break;
    }
    fclose(pFile);
    return md5.toString();
}
