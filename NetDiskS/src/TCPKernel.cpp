#include<TCPKernel.h>
#include "packdef.h"
#include<stdio.h>
#include<sys/time.h>
#include"clogic.h"

using namespace std;


//设置网络协议映射
void TcpKernel::setNetPackMap()
{
    //清空映射
    bzero( m_NetPackMap , sizeof(m_NetPackMap) );

    //协议映射赋值
    m_logic->setNetPackMap();

}


TcpKernel::TcpKernel()
{

}

TcpKernel::~TcpKernel()
{
    if( m_logic ) delete m_logic;
}

TcpKernel *TcpKernel::GetInstance()
{
    static TcpKernel kernel;
    return &kernel;
}

int TcpKernel::Open( int port)
{
    initRand();

    m_sql = new CMysql;
    //数据库 使用127.0.0.1 地址  用户名root 密码colin123 数据库 wechat 没有的话需要创建 不然报错    if(  !m_sql->ConnectMysql( _DEF_DB_IP , _DEF_DB_USER, _DEF_DB_PWD, _DEF_DB_NAME )  )
    {
        printf("Conncet Mysql Failed...\n");
        return FALSE;
    }
    else
    {
        printf("MySql Connect Success...\n");
        //【新增代码】创建客户端新增功能（收藏/回收站）所需的数据表，
        //使用 IF NOT EXISTS 保证老库升级时不会报错。        m_sql->UpdataMysql("create table if not exists t_favorite("
                           "u_id int not null,"
                           "f_id int not null,"
                           "f_dir varchar(260) not null,"
                           "primary key(u_id, f_id, f_dir));");
        m_sql->UpdataMysql("create table if not exists t_recycle("
                           "u_id int not null,"
                           "f_id int not null,"
                           "f_dir varchar(260) not null,"
                           "f_name varchar(260) not null,"
                           "f_type varchar(10) not null,"
                           "f_size int default 0,"
                           "f_path varchar(260) not null,"
                           "f_MD5 varchar(40) not null,"
                           "del_time varchar(60) default '',"
                           "primary key(u_id, f_id));");
    }
    //初始网络
    m_tcp = new Block_Epoll_Net;
    bool res = m_tcp->InitNet( port , &TcpKernel::DealData ) ;
    if( !res )
        err_str( "net init fail:" ,-1);

    m_logic = new CLogic(this);

    //【新增代码】启动时确保存储根目录存在（DEF_PATH），创建失败会打印警告
    m_logic->InitStoragePath();

    setNetPackMap();

    return TRUE;
}

void TcpKernel::Close()
{
    m_sql->DisConnect();

}

//随机数初始化
void TcpKernel::initRand()
{
    struct timeval time;
    gettimeofday( &time , NULL);
    srand( time.tv_sec + time.tv_usec );
    //分享码生成用的是 BSD random()（clogic.cpp 里 ShareFileRq），
    //random() 要由 srandom() 播种，srand() 只能播 rand()。    //生成的第一个分享码都相同，随机到一样的码。这里补上播种。    srandom( time.tv_sec + time.tv_usec );
}

void TcpKernel::DealData(sock_fd clientfd,char *szbuf,int nlen)
{
    PackType type = *(PackType*)szbuf;
    if( (type >= _DEF_PACK_BASE) && ( type < _DEF_PACK_BASE + _DEF_PACK_COUNT) )
    {
        PFUN pf = NetPackMap( type );
        if( pf )
        {
            (TcpKernel::GetInstance()->m_logic->*pf)( clientfd , szbuf , nlen);
        }
    }

    return;
}

void TcpKernel::EventLoop()
{
    printf("event loop\n");
    m_tcp->EventLoop();
}

void TcpKernel::SendData(sock_fd clientfd, char *szbuf, int nlen)
{
    m_tcp->SendData(clientfd , szbuf ,nlen );
}
