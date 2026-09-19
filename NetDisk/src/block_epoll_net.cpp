#include "block_epoll_net.h"

//客户端每 30 秒发送一次心跳，连续三个周期没有数据则认为连接失效static const time_t HEARTBEAT_TIMEOUT_SECONDS = 90;


bool Block_Epoll_Net::InitNet(int port, void (*recv_callback)(int, char *, int))
{
    m_recv_callback = recv_callback;
    InitThreadPool();

    int flags = 1;
    int ret = 0;
    m_listenEv = new myevent_s(this);

    //创建tcp套接字
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if ( m_listenfd  == -1 ){
        perror("create socket error");
        return false;
    }

    //设置地址复用
    ret = setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&flags, sizeof(flags));
    if ( ret == -1 ){
        perror("setsockopt error");
        return false;
    }

    //监听套接字m_listenfd 采用 LT 非阻塞模式
    //设置非阻塞
    setNonBlockFd( m_listenfd );

    struct sockaddr_in local_addr;
    bzero( &local_addr , sizeof(sockaddr_in) );
    //设置地址
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    //绑定地址
    ret = bind(m_listenfd, (struct sockaddr *)&local_addr, sizeof(struct sockaddr_in));
    if( ret == -1 ) {
        perror("bind error");
        close(m_listenfd);
        return false;
    }

    if (listen(m_listenfd, 128) == -1 ){
        perror("listen error");
        close(m_listenfd);
        return false;
    }

    //创建epoll
    m_epoll_fd = epoll_create( MAX_EVENTS );

    m_listenEv->eventset(m_listenfd ,m_epoll_fd );
    //将监听套接字 添加到epoll中 , 模式LT 非阻塞
    m_listenEv->eventadd( EPOLLIN);


    return true;
}

bool Block_Epoll_Net::InitThreadPool()
{

    m_threadpool = new thread_pool;

    //创建拥有10个线程的线程池 最大线程数200 环形队列最大值50000
    if( (m_threadpool->Pool_create(200,10,50000)) == false )
    {
        perror("Create Thread_Pool Failed:");
        exit(-1);
    }

    return true;
}

void Block_Epoll_Net::EventLoop()
{
    printf("EventLoop:server running\n");
    int  i = 0;
    while (1) {

        /* 等待事件发生 */
        int nfd = epoll_wait( m_epoll_fd, events, MAX_EVENTS+1, 1000);
        if (nfd < 0) {
            printf("epoll_wait error, exit\n");
//break;            continue;
        }
        for (i = 0; i < nfd; i++) {
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;
            int fd = ev->fd;
            if ( (events[i].events & EPOLLIN) ) {
                if( fd == m_listenfd )
                    accept_event();
                else
                    recv_event( ev );
            }
            if ((events[i].events & EPOLLOUT) ) {
                epollout_event( ev );
            }
        }
        //每秒检查一次无数据连接，不使用 UDP，也不影响正常业务收发        CheckHeartbeatTimeout();
    }
}

void Block_Epoll_Net::accept_event()
{
    struct sockaddr_in caddr;
    socklen_t len = sizeof(caddr);
    int clientfd ;
    if ((clientfd = accept(m_listenfd, (struct sockaddr *)&caddr, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
            /* 暂时不做出错处理 */
        }
        printf("%s: accept, %s\n", __func__, strerror(errno));
        return;
    }
////客户端接收使用阻塞模式 更简单 适合入门////设置非阻塞//setNonBlockFd( clientfd );
    //设置接收缓冲区大小
    setRecvBufSize( clientfd );
    //设置发送缓冲区大小
    setRecvBufSize( clientfd );
    //设置 无延迟
    setNoDelay( clientfd );

    myevent_s * clientEv = new myevent_s(this);
    clientEv->eventset( clientfd , m_epoll_fd );
    clientEv->eventadd(  EPOLLIN/*|EPOLLET*/|EPOLLONESHOT );

    //m_mapSockfdToEvent[clientfd] = clientEv;
    m_mapSockfdToEvent.insert(clientfd , clientEv);
    pthread_mutex_lock(&m_activityLock);
    m_lastActivity[clientfd] = time(NULL);
    pthread_mutex_unlock(&m_activityLock);

    printf("new connect [%s:%d][time:%ld] \n",
           inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port), time(NULL) );
    return;
}




void Block_Epoll_Net::recv_event( myevent_s *ev)
{
     m_threadpool->Producer_add(  recv_task , (void*) ev );
}

void* Block_Epoll_Net::recv_task(void* arg)
{
    myevent_s * ev = (myevent_s*)arg;
    //利用全局指针 方便操作
    Block_Epoll_Net * pthis = ev->pNet;

    //接收和处理分离
    int nRelReadNum = 0;
    int nPackSize = 0;
    char *pSzBuf = NULL;
    do
    {
        //read 一次不一定能读满 4 字节包头，读不满就拆包会把后续
        //字节流整体错位（客户端后续所有数据都变成垃圾长度，表现为下载卡死、        //文件列表刷新不出来）。这里循环读满 4 字节包头。        nRelReadNum = 0;
        while (nRelReadNum < (int)sizeof(nPackSize)) {
            int nOnce = read(ev->fd, (char*)&nPackSize + nRelReadNum,
                             sizeof(nPackSize) - nRelReadNum);
            if (nOnce <= 0)
                break;
            nRelReadNum += nOnce;
            pthis->TouchActivity(ev->fd);
        }
        if(nRelReadNum < (int)sizeof(nPackSize))
            break;

        //包头长度异常（对端已错位或恶意数据）直接断开连接，
        if (nPackSize <= 0 || nPackSize > 64 * 1024 * 1024)
            break;

        pSzBuf = new char[nPackSize];
        int nOffSet = 0;
        nRelReadNum = 0;
        bool bRecvOk = true;
        //接收包的数据
        while(nPackSize)
        {
            nRelReadNum = recv(ev->fd,pSzBuf+nOffSet,nPackSize,0);
            if(nRelReadNum < 0 && errno == EINTR)
                continue;  // 被信号打断，重试
            if(nRelReadNum <= 0){
                //对端断开/出错时不能再按完整包处理：
                //半包进入处理流程会破坏协议状态                bRecvOk = false;
                break;
            }

            nOffSet += nRelReadNum;
            nPackSize -= nRelReadNum;
            //大包分多次到达时，每次成功接收都算作连接仍有活动            pthis->TouchActivity(ev->fd);
        }
        if(!bRecvOk){
            delete[] pSzBuf;
            break;  // 走统一清理流程关闭连接
        }
        DataBuffer * buffer = new DataBuffer(ev->pNet , ev->fd , pSzBuf , nOffSet );
        pthis->m_threadpool->Producer_add(  Buffer_Deal , (void*) buffer );

        //这次接收完 要重新注册事件  此时 EPOLL MODE -> EPOLLIN|EPOLLONESHOT 没有修改, 使用重复值        ev->eventadd(  ev->events );

        return 0;

    }while(0);

    ev->eventdel();
    pthis->RemoveActivity(ev->fd);
    //回收event结构
    //pthis->m_mapSockfdToEvent.erase( ev->fd );
    pthis->m_mapSockfdToEvent.erase( ev->fd );
    close(ev->fd);
    delete ev;
    return NULL;

}

void Block_Epoll_Net::TouchActivity(int fd)
{
    pthread_mutex_lock(&m_activityLock);
    m_lastActivity[fd] = time(NULL);
    pthread_mutex_unlock(&m_activityLock);
}

void Block_Epoll_Net::RemoveActivity(int fd)
{
    pthread_mutex_lock(&m_activityLock);
    m_lastActivity.erase(fd);
    pthread_mutex_unlock(&m_activityLock);
}

void Block_Epoll_Net::CheckHeartbeatTimeout()
{
    vector<int> expired;
    const time_t now = time(NULL);

    pthread_mutex_lock(&m_activityLock);
    for (map<int, time_t>::const_iterator it = m_lastActivity.begin();
         it != m_lastActivity.end(); ++it) {
        //连接池（四路分片）连接只在任务开始/暂停/恢复时通信，
        //客户端不会在这些连接上发心跳。若按普通连接的超时规则处理，        //连接池连接的清理交给客户端断开时统一处理。        if (m_poolFds.count(it->first) > 0)
            continue;
        if (now - it->second >= HEARTBEAT_TIMEOUT_SECONDS)
            expired.push_back(it->first);
    }
    pthread_mutex_unlock(&m_activityLock);

    for (size_t i = 0; i < expired.size(); ++i) {
        myevent_s* ev = NULL;
        if (m_mapSockfdToEvent.find(expired[i], ev) && ev != NULL) {
            shutdown(expired[i], SHUT_RDWR);
            printf("心跳超时，关闭 fd[%d]\n", expired[i]);
            TouchActivity(expired[i]);
        } else {
            RemoveActivity(expired[i]);
        }
    }
}

void * Block_Epoll_Net::Buffer_Deal( void * arg )
{
    DataBuffer * buffer = (DataBuffer *)arg;
    if( !buffer ) return NULL;

    buffer->pNet->m_recv_callback(buffer->sockfd,buffer->buf,buffer->nlen);

 //printf("pszbuf = %p \n",buffer->buf);    if(buffer->buf != NULL)
    {
        delete [] buffer->buf;
        buffer->buf = NULL;
    }
    delete buffer;
    return 0;
}

void Block_Epoll_Net::epollout_event( myevent_s *ev )
{
    //epoll LT模式 阻塞模式 发送阻塞 , 不用监听EPOLLOUT事件}



int Block_Epoll_Net::SendData(int fd, char *szbuf, int nlen)
{
    //先包大小, 再数据包 , 一次放入缓冲区
    /*
     +--------------+------------------+---------------------+
     |<-- 4bytes -->|<-- 4bytes协议头 ->|<--  协议其他内容    ->|
     +--------------+------------------+---------------------+
     |<- packsize ->|<------------ 数据包 struct ------------>|
     * */

    int nPackSize = nlen + 4;
    vector<char> vecbuf( nPackSize , 0);
    //vecbuf.resize( nPackSize );

    char* buf = &* vecbuf.begin();
    char* tmp = buf;
    *(int*)tmp = nlen;//按四个字节int写入
    tmp += sizeof(int );
    memcpy( tmp , szbuf , nlen );

    //客户端收到的包不完整、后续字节流错位。加每连接发送锁并循环发完：    //锁保证同一连接不会出现两个线程的包交错；循环保证整包完整发出。    SendLock(fd);
    int res = 0;
    int total = 0;
    while (total < nPackSize) {
        res = send(fd, (const char*)buf + total, nPackSize - total, 0);
        if (res > 0) {
            total += res;
        } else if (res < 0 && errno == EINTR) {
            continue;  // 被信号打断，重试
        } else {
            break;  // 出错或对端关闭，放弃剩余部分
        }
    }
    if (total > 0)
        TouchActivity(fd);
    SendUnlock(fd);

    return (total == nPackSize) ? total : -1;
}

//每连接发送锁：线程池多线程并发向同一 fd 发包时，
//两个 send 的字节会在 socket 缓冲区交错，客户端按长度拆包必然错位//（表现为下载文件损坏、视频无法播放、下载卡死）。惰性创建、不销毁：void Block_Epoll_Net::SendLock(int fd)
{
    pthread_mutex_t* lock = NULL;
    if (!m_mapSockfdToSendLock.find(fd, lock) || lock == NULL) {
        static pthread_mutex_t createLock = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&createLock);
        if (!m_mapSockfdToSendLock.find(fd, lock) || lock == NULL) {
            lock = new pthread_mutex_t;
            pthread_mutex_init(lock, NULL);
            m_mapSockfdToSendLock.insert(fd, lock);
        }
        pthread_mutex_unlock(&createLock);
    }
    pthread_mutex_lock(lock);
}

void Block_Epoll_Net::SendUnlock(int fd)
{
    pthread_mutex_t* lock = NULL;
    if (m_mapSockfdToSendLock.find(fd, lock) && lock != NULL)
        pthread_mutex_unlock(lock);
}

void Block_Epoll_Net::MarkPoolFd(int fd)
{
    pthread_mutex_lock(&m_activityLock);
    m_poolFds[fd] = true;
    pthread_mutex_unlock(&m_activityLock);
}

void Block_Epoll_Net::setNonBlockFd(int fd)
{
    int flags = 0;
    flags = fcntl(fd, F_GETFL, 0);
    int ret = fcntl(fd, F_SETFL, flags|O_NONBLOCK);
    if( ret == -1)
        perror("setNonBlockFd fail:");
}

void Block_Epoll_Net::setRecvBufSize( int fd)
{
    //接收缓冲区
    int nRecvBuf = 256*1024;//设置为 256 K
    setsockopt(fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
}

void Block_Epoll_Net::setSendBufSize( int fd)
{
    //发送缓冲区
    int nSendBuf=128*1024;//设置为 128 K
    setsockopt(fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));
}

#include<netinet/tcp.h>
void Block_Epoll_Net::setNoDelay( int fd)
{
    //设置nodelay
    int value = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY ,(char *)&value, sizeof(int));
}
