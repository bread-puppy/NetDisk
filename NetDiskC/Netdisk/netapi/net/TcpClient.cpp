#include "TcpClient.h"


#include"INetMediator.h"



TcpClient::TcpClient( INetMediator * pMediator ):m_sock( INVALID_SOCKET ),m_isStop(false)
{
    m_pMediator = pMediator;
    m_isConnected = false;
    m_hThreadHandle = NULL;
}

TcpClient::~TcpClient()  //使用时, 父类指针指向子类, 使用虚析构
{
    TcpClient::UnInitNet();
}
//初始化网络	//加载库 创建套接字 绑定
bool TcpClient::InitNet(const char *szBufIP, unsigned short port)
{
    if( !m_isLoadlib )
    {
        //1.加载库
        WORD wVersionRequested;
        WSADATA wsaData;
        int err;

    /* 使用 Windef.h 中声明的 MAKEWORD 宏 */
        wVersionRequested = MAKEWORD(2, 2);

        err = WSAStartup(wVersionRequested, &wsaData);
        if (err != 0) {
            return false;
        }
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            WSACleanup();
            return false;
        }
        m_isLoadlib = true;
    }

    m_isConnected = false;

    //2.创建套接字 进程与外界网络通信需要的接口 决定了与外界通讯的方式(tcp udp)
    m_sock = socket( AF_INET , SOCK_STREAM , IPPROTO_TCP ); // IPv4 UDP
    if ( m_sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    //3. 连接服务器
    sockaddr_in addr;
    addr.sin_family = AF_INET ;
    addr.sin_addr.S_un.S_addr = inet_addr( szBufIP );  /*inet_addr("192.168.31.115")*/ ;  //绑定任意网卡
    addr.sin_port = htons( port );  //htons 转换为网络字节序 大端存储  43232


    if( connect( m_sock ,(const sockaddr* ) &addr , sizeof(addr) ) == SOCKET_ERROR )
    {
        UnInitNet();
        return false;
    }

    //设置客户端发送缓冲区
    //int nSendBuf=64*1024;//设置为64K
    //setsockopt(m_sock,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));
    //设置客户端接收缓冲区
    //int nRecvBuf=64*1024;//设置为64K
    //setsockopt(m_sock,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));

//DWORD nNetTimeout= 300;//1秒 - 1000

//setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&nNetTimeout, sizeof(DWORD));

    //禁用 TCP-NODELAY（即启用 Nagle 算法的反面——立即发送）
    int value = 1;
    setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&value, sizeof(int));

    value = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&value, sizeof(int));

    m_isConnected = true;
    //收数据 -- 创建线程 CreateThread  WinAPI  strcpy  C/C++ RunTime 库函数 创建内存块
    m_hThreadHandle = (HANDLE)_beginthreadex(  NULL, 0 ,&RecvThread ,this , 0 , NULL );     //( CreateThread 创建内存块 )
    //_endthreadex(); -- 回收内存块      //( ExitThread 不回收内存块 ) --内存泄露

    return true;
}

unsigned int __stdcall TcpClient::RecvThread( void * lpvoid)
{
    TcpClient* pthis = (TcpClient*) lpvoid;
    pthis->RecvData();

    return 0;
}

//关闭网络
void TcpClient::UnInitNet()
{
    m_isStop = true ; // 尝试退出线程循环
    if( m_hThreadHandle )
    {
        if( WaitForSingleObject(m_hThreadHandle , 100 ) == WAIT_TIMEOUT )
        {
            TerminateThread( m_hThreadHandle , -1 );
        }
        CloseHandle(m_hThreadHandle);
        m_hThreadHandle = NULL;
    }
    if( m_sock && m_sock != INVALID_SOCKET )
        closesocket(m_sock);

    if( m_isLoadlib )
    {
        WSACleanup();
        m_isLoadlib = false;
    }
}
//发送 : 同时兼容tcp udp
bool TcpClient::SendData(unsigned int lSendIP , char* szbuf , int nlen )
{
    if( !szbuf|| nlen <= 0 ) return false;

    lSendIP = m_sock;

    int DataLen = nlen + 4;
    std::vector<char> vecbuf;
    vecbuf.resize( DataLen );

    char* buf = &*vecbuf.begin();
    char* tmp = buf;
    *(int*) tmp = nlen;
    tmp+= sizeof(int);

    memcpy( tmp , szbuf , nlen);

    int res = send( lSendIP , buf , DataLen , 0 );
    return (res > 0);
}
//接收

void TcpClient::RecvData()
{
    int nPackSize = 0;
    int nRes = 0;
    while( !m_isStop )
    {
        nRes = recv( m_sock , (char*)&nPackSize , sizeof(int) , 0 );
        if( nRes == 0)
        {
            this->m_isConnected = false;
            this->m_pMediator->disConnect();
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            break;
        }
        if( nRes < 0 )
        {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            break;
        }
        int offset = 0;
        int remain = nPackSize;
        char * buf = new char[nPackSize];
        bool recvOk = true;
        while( remain > 0 )
        {
            nRes = recv( m_sock , buf + offset , remain , 0 );
            if( nRes > 0 )
            {
                remain -= nRes;
                offset += nRes;
            }
            else if( nRes == 0 )
            {
                recvOk = false;
                break;
            }
            else
            {
                recvOk = false;
                break;
            }
        }
        if( recvOk )
        {
            this->m_pMediator->DealData( m_sock , buf , offset );
        }
        else
        {
            delete[] buf;
            this->m_isConnected = false;
            this->m_pMediator->disConnect();
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            break;
        }
    }
}

bool TcpClient::IsConnected()
{
    return  m_isConnected;
}
