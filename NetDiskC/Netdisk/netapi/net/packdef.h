#pragma once

#include<memory.h>
#include <QString>
#include <cstdio>

#define _MAX_PATH_SIZE      (260)
//故改回 4096，保证双方协议结构体字节布局一致。
#define _DEF_BUFFER         (4096)
#define _DEF_CONTENT_SIZE	(4096)
#define _MAX_SIZE           (40)
#define _DEF_SHARE_PWD_LEN  20

//自定义协议   先写协议头 再写协议结构
//登录 注册 获取好友信息 添加好友 聊天 发文件 下线请求
#define _DEF_PACK_BASE	(10000)
#define _DEF_PACK_COUNT (100)

//注册
#define _DEF_PACK_REGISTER_RQ	(_DEF_PACK_BASE + 0 )
#define _DEF_PACK_REGISTER_RS	(_DEF_PACK_BASE + 1 )
//登录
#define _DEF_PACK_LOGIN_RQ	(_DEF_PACK_BASE + 2 )
#define _DEF_PACK_LOGIN_RS	(_DEF_PACK_BASE + 3 )


//返回的结果
//注册请求的结果
#define tel_is_exist		(0)
#define name_is_exist		(1)
#define register_success	(2)
//登录请求的结果
#define tel_not_exist		(0)
#define password_error		(1)
#define login_success		(2)


typedef int PackType;

//协议结构
//注册
typedef struct STRU_REGISTER_RQ
{
	STRU_REGISTER_RQ():type(_DEF_PACK_REGISTER_RQ)
	{
		memset( tel  , 0, sizeof(tel));
		memset( name  , 0, sizeof(name));
		memset( password , 0, sizeof(password) );
	}
	//需要手机号码 , 密码, 昵称
	PackType type;
	char tel[_MAX_SIZE];
	char name[_MAX_SIZE];
	char password[_MAX_SIZE];

}STRU_REGISTER_RQ;

typedef struct STRU_REGISTER_RS
{
	//回复结果
	STRU_REGISTER_RS(): type(_DEF_PACK_REGISTER_RS) , result(register_success)
	{
	}
	PackType type;
	int result;

}STRU_REGISTER_RS;

//登录
typedef struct STRU_LOGIN_RQ
{
	//登录需要: 手机号 密码 
	STRU_LOGIN_RQ():type(_DEF_PACK_LOGIN_RQ)
	{
		memset( tel , 0, sizeof(tel) );
		memset( password , 0, sizeof(password) );
	}
	PackType type;
	char tel[_MAX_SIZE];
	char password[_MAX_SIZE];

}STRU_LOGIN_RQ;

typedef struct STRU_LOGIN_RS
{
	//需要 结果 , 用户的id
	STRU_LOGIN_RS(): type(_DEF_PACK_LOGIN_RS) , result(login_success),userid(0)
	{
        memset(name,0,sizeof(name));
	}
	PackType type;
	int result;
	int userid;
    char name[_MAX_SIZE];

}STRU_LOGIN_RS;

////////////////////文件上传/////////////////
//上传文件请求
#define _DEF_PACK_UPLOAD_FILE_RQ       (_DEF_PACK_BASE + 4 )
//上传文件回复
#define _DEF_PACK_UPLOAD_FILE_RS       (_DEF_PACK_BASE + 5 )

//文件内容请求
#define _DEF_PACK_FILE_CONTENT_RQ       (_DEF_PACK_BASE + 6 )
//文件内容回复
#define _DEF_PACK_FILE_CONTENT_RS       (_DEF_PACK_BASE + 7 )

//上传文件请求
struct STRU_UPLOAD_FILE_RQ
{
    STRU_UPLOAD_FILE_RQ():type(_DEF_PACK_UPLOAD_FILE_RQ)
      ,userid(0),size(0),timestamp(0){
        memset( fileName , 0, sizeof(fileName) );
        memset( dir , 0, sizeof(dir) );
        memset( md5 , 0, sizeof(md5) );
        memset( fileType , 0, sizeof(fileType) );
        memset( time , 0, sizeof(time) );
    }
    PackType type;
    int timestamp;//时间戳用于区分不同任务
    int userid; //服务器与时间戳配合,区分不同任务
    char fileName[_MAX_PATH_SIZE]; //上传文件名字
    int size;//大小
    char dir[_MAX_PATH_SIZE];//上传到什么目录
    char md5[_MAX_SIZE]; //上传文件的md5, 用于验证文件是否完整无误
    char fileType[_MAX_SIZE];//文件类型
    char time[_MAX_SIZE]; //上传时间
};

//上传文件回复
struct STRU_UPLOAD_FILE_RS
{
    STRU_UPLOAD_FILE_RS(): type(_DEF_PACK_UPLOAD_FILE_RS), userid(0), fileid(0),result(1),timestamp(0){
}
PackType type;
int timestamp;//时间戳用于区分不同任务
int userid;//用户id
int fileid; //文件id
int result; //结果
};

//文件内容请求
struct STRU_FILE_CONTENT_RQ
{
    STRU_FILE_CONTENT_RQ():type(_DEF_PACK_FILE_CONTENT_RQ),
        userid(0),fileid(0),len(0),timestamp(0),seq(0){
        memset( content , 0 , sizeof(content));
    }
    PackType type;
    int timestamp;//时间戳用于区分不同任务
    int userid;//用户id
    int fileid;//文件id
    char content[_DEF_BUFFER];//文件内容 也叫文件块   _DEF_BUFFER  4096
    int len;//文件内容长度
    int seq; // 分片内数据块序号，从0开始
};

//文件内容回复
struct STRU_FILE_CONTENT_RS
{
    STRU_FILE_CONTENT_RS():type(_DEF_PACK_FILE_CONTENT_RS),
        timestamp(0),userid(0),fileid(0),result(1),len(0){}
    PackType type;
    int timestamp;//时间戳用于区分不同任务
    int userid;//用户id
    int fileid;//文件id
    int result;//结果
    int len;//文件内容长度
};

////////////////////获取文件信息////////////////
//文件内容请求
#define _DEF_PACK_GET_FILE_INFO_RQ (_DEF_PACK_BASE+8)
//文件内容回复
#define _DEF_PACK_GET_FILE_INFO_RS (_DEF_PACK_BASE+9)
#define _DEF_TYPE_LEN       (10)
//获取文件信息请求
struct STRU_GET_FILE_INFO_RQ{
    STRU_GET_FILE_INFO_RQ():type(_DEF_PACK_GET_FILE_INFO_RQ),userid(0){
        memset(dir,0,sizeof(dir));
    }
    PackType type;
    int userid;
    char dir[_MAX_PATH_SIZE];
};
//文件信息
struct STRU_FILE_INFO{
    STRU_FILE_INFO():fileid(0),size(0){
        memset(name,0,sizeof(name));
        memset(time,0,sizeof(time));
        memset(fileType,0,sizeof(fileType));
    }
    int fileid;
    char name[_MAX_PATH_SIZE];
    char time[_MAX_SIZE];
    int size;
    char fileType[_DEF_TYPE_LEN];
};

//获取文件信息回复
struct STRU_GET_FILE_INFO_RS{
    STRU_GET_FILE_INFO_RS():type(_DEF_PACK_GET_FILE_INFO_RS),count(0){
        memset(dir,0,sizeof(dir));
    }
    void init(){
        type=_DEF_PACK_GET_FILE_INFO_RS;
        count=0;
        memset(dir,0,sizeof(dir));
    }
    PackType type;
    char dir[_MAX_PATH_SIZE];
    int count;
    //文件信息数组
    STRU_FILE_INFO fileInfo[ ];  //柔性数组
};

////////////////////文件下载/////////////////
//下载文件请求
#define _DEF_PACK_DOWNLOAD_FILE_RQ			(_DEF_PACK_BASE + 10 )

//下载文件夹请求
#define _DEF_PACK_DOWNLOAD_FOLDER_RQ		(_DEF_PACK_BASE + 11 )

//下载文件回复
#define _DEF_PACK_DOWNLOAD_FILE_RS			(_DEF_PACK_BASE + 12 )

//下载文件头请求
#define _DEF_PACK_FILE_HEADER_RQ			(_DEF_PACK_BASE + 13 )

//下载文件头回复
#define _DEF_PACK_FILE_HEADER_RS			(_DEF_PACK_BASE + 14 )


//下载文件请求
struct STRU_DOWNLOAD_FILE_RQ
{
    STRU_DOWNLOAD_FILE_RQ():type(_DEF_PACK_DOWNLOAD_FILE_RQ)
      ,userid(0),fileid(0),timestamp(0){
        memset( dir , 0, sizeof(dir) );
    }
    PackType type;
    int timestamp;//时间戳用于区分不同任务
    int userid; //服务器与时间戳配合,区分不同任务
    int fileid; //文件id
    char dir[ _MAX_PATH_SIZE ]; //文件所属目录

};

//下载文件夹请求
struct STRU_DOWNLOAD_FOLDER_RQ
{
    STRU_DOWNLOAD_FOLDER_RQ():type(_DEF_PACK_DOWNLOAD_FOLDER_RQ)
      ,userid(0),fileid(0),timestamp(0){
        memset( dir , 0, sizeof(dir) );
    }
    PackType type;
    int timestamp;//时间戳用于区分不同任务
    int userid; //服务器与时间戳配合,区分不同任务
    int fileid; //文件id
    char dir[ _MAX_PATH_SIZE ]; //文件所属目录

};

//下载文件回复 ( 一般也不会出现问题, 所以这个包也不用 )
struct STRU_DOWNLOAD_FILE_RS
{
    STRU_DOWNLOAD_FILE_RS():type(_DEF_PACK_DOWNLOAD_FILE_RS)
    ,timestamp(0),userid(0),fileid(0),result(1){
    }
    PackType type;
    int timestamp;//时间戳用于区分不同任务
    int userid; //服务器与时间戳配合,区分不同任务
    int fileid; //文件id
    int result; //结果
};

//文件头请求
struct STRU_FILE_HEADER_RQ
{
    STRU_FILE_HEADER_RQ():type(_DEF_PACK_FILE_HEADER_RQ)
      ,fileid(0),size(0),timestamp(0){
        memset( fileName , 0, sizeof(fileName) );
        memset( dir , 0, sizeof(dir) );
        memset( md5 , 0, sizeof(md5) );
        memset( fileType , 0, sizeof(fileType) );
    }
    PackType type;
    int timestamp;
    int fileid;
    char fileName[_MAX_PATH_SIZE];
    int size;//大小
    char dir[_MAX_PATH_SIZE];//路径
    char md5[_MAX_SIZE];
    char fileType[_MAX_SIZE];//文件类型
};

//下载文件夹头请求
#define _DEF_PACK_FOLDER_HEADER_RQ			(_DEF_PACK_BASE + 24 )
//文件夹头请求
struct STRU_FOLDER_HEADER_RQ
{
    STRU_FOLDER_HEADER_RQ():type(_DEF_PACK_FOLDER_HEADER_RQ)
      ,fileid(0),timestamp(0){
        memset( fileName , 0, sizeof(fileName) );
        memset( dir , 0, sizeof(dir) );
    }
    PackType type;
    int timestamp;
    int fileid;
    char fileName[_MAX_PATH_SIZE];
    char dir[_MAX_PATH_SIZE];//路径
};


//文件头回复
struct STRU_FILE_HEADER_RS
{
    STRU_FILE_HEADER_RS(): type(_DEF_PACK_FILE_HEADER_RS)
      , userid(0), fileid(0),result(1),timestamp(0){

    }
    PackType type;
    int timestamp;
    int userid;
    int fileid;
    int result;
};

//////////////////新建文件夹/////////////////////
//新建文件夹请求
#define _DEF_PACK_ADD_FOLDER_RQ       (_DEF_PACK_BASE + 15 )
//新建文件夹回复
#define _DEF_PACK_ADD_FOLDER_RS       (_DEF_PACK_BASE + 16 )

//新建文件夹请求
struct STRU_ADD_FOLDER_RQ
{
    STRU_ADD_FOLDER_RQ():type(_DEF_PACK_ADD_FOLDER_RQ)
      ,timestamp(0),userid(0){
        memset( fileName , 0, sizeof(fileName) );
        memset( dir , 0, sizeof(dir) );
        memset( time , 0, sizeof(time) );
    }
    PackType type;
    int timestamp;
    int userid;
    char fileName[_MAX_PATH_SIZE];
    char dir[_MAX_PATH_SIZE];//路径
    char time[_MAX_SIZE]; //上传时间
};

//新建文件夹回复
struct STRU_ADD_FOLDER_RS
{
    STRU_ADD_FOLDER_RS(): type(_DEF_PACK_ADD_FOLDER_RS)
     ,timestamp(0) ,userid(0), result(1){

    }
    PackType type;
    int timestamp;
    int userid;
    int result;
};

//////////////////秒传/////////////////////
//秒传回复
#define _DEF_PACK_QUICK_UOLOAD_RS    (_DEF_PACK_BASE + 17 )
struct STRU_QUICK_UPLOAD_RS{
    STRU_QUICK_UPLOAD_RS():type(_DEF_PACK_QUICK_UOLOAD_RS),timestamp(0),userid(0),result(1){

    }
    PackType type;
    int timestamp;
    int userid;
    int result;
};

///////////////分享文件 ////////////////////
///分享文件请求
#define _DEF_PACK_SHARE_FILE_RQ       (_DEF_PACK_BASE + 18 )
/// 分享文件回复
#define _DEF_PACK_SHARE_FILE_RS       (_DEF_PACK_BASE + 19 )

//分享文件请求 : 包含 谁 分享 什么目录下面的 哪些文件( 文件id 数组 )  分享时间
//删除 password 字段：服务端的 STRU_SHARE_FILE_RQ 没有该字段，
//会把密码内容当成文件个数，分享功能报错。现与服务端结构保持严格一致。
struct STRU_SHARE_FILE_RQ
{
    void init(){
        type = _DEF_PACK_SHARE_FILE_RQ;
        userid = 0;
        memset( dir , 0 , sizeof(dir) );
        memset( shareTime , 0 , sizeof(shareTime) );
        itemCount = 0;
    }
    PackType type;
    int userid;
    char dir[_MAX_PATH_SIZE ];
    char shareTime[_MAX_SIZE];
    int itemCount;
    int fileidArray[];
};

//收到回复 就刷新分享列表
//删除 password 字段，与服务端结构对齐
struct STRU_SHARE_FILE_RS
{
    STRU_SHARE_FILE_RS(): type( _DEF_PACK_SHARE_FILE_RS ),result(0){
    }
    PackType type;
    int result;
};

///////////////刷新分享列表 ////////////////////
//获取自己的分享请求
#define _DEF_PACK_MY_SHARE_RQ   (_DEF_PACK_BASE + 20 )
//获取自己的分享回复
#define _DEF_PACK_MY_SHARE_RS   (_DEF_PACK_BASE + 21 )
//获取自己的分享请求  谁获取  考虑加个时间 比如获取半个月的  todo
struct STRU_MY_SHARE_RQ
{
    STRU_MY_SHARE_RQ():type( _DEF_PACK_MY_SHARE_RQ) , userid(0){

    }
    PackType type;
    int userid;
    //考虑加入时间 获取指定时间范围的分享
};

//分享文件信息: 名字 大小 分享时间 链接
//删除 password 字段：服务端的 STRU_MY_SHARE_FILE 没有该字段，
//现与服务端结构保持严格一致。
struct STRU_MY_SHARE_FILE
{
    char name[_MAX_PATH_SIZE];
    int size;
    char time[_MAX_SIZE];
    int shareLink;
};

//获取自己的分享回复  分享文件的列表 文件: 名字 大小 分享时间 链接 (密码 todo )
struct STRU_MY_SHARE_RS
{
    void init() {
        type = _DEF_PACK_MY_SHARE_RS; itemCount = 0;
    }
    PackType type;
    int itemCount;
    STRU_MY_SHARE_FILE items[];
};

/////////////////////////////获取分享//////////////////////////////////
//获取分享请求
#define _DEF_PACK_GET_SHARE_RQ       (_DEF_PACK_BASE + 22 )
//获取分享回复
#define _DEF_PACK_GET_SHARE_RS       (_DEF_PACK_BASE + 23 )

//获取分享回复状态
#define get_share_success       0
#define get_share_password_error 1
#define get_share_link_invalid  2

//获取分享
//删除末尾的 password 字段，与服务端 STRU_GET_SHARE_RQ 结构严格对齐
//（服务端不校验密码，该字段只影响双方结构体大小一致性）
struct STRU_GET_SHARE_RQ
{
    STRU_GET_SHARE_RQ():type(_DEF_PACK_GET_SHARE_RQ)
      ,userid(0), shareLink(0){
        memset(dir , 0 , sizeof(dir));
        memset(time , 0 , sizeof(time));
    }
    PackType type;
    int userid;
    int shareLink; // 9位 首位是1-9 数字
    char dir[_MAX_PATH_SIZE];
    char time[_MAX_SIZE];
    //直接加载这个路径下面
};

//获取分享回复 :收到刷新
struct STRU_GET_SHARE_RS
{
    STRU_GET_SHARE_RS():type(_DEF_PACK_GET_SHARE_RS)
      ,result(0) {
        memset(dir , 0 , sizeof(dir));
    }
    PackType type;
    int result;
    char dir[_MAX_PATH_SIZE];
};


//////////////////删除文件///////////////////
//删除文件请求
#define _DEF_PACK_DELETE_FILE_RQ       (_DEF_PACK_BASE + 25 )
//删除文件回复
#define _DEF_PACK_DELETE_FILE_RS       (_DEF_PACK_BASE + 26 )

//删除文件请求 : 某人 删除某路径下的 某文件 fileid数组
struct STRU_DELETE_FILE_RQ
{
    void init()
    {
        type = _DEF_PACK_DELETE_FILE_RQ;
        userid = 0;
        fileCount = 0;
        memset( dir , 0 , sizeof(dir) );
    }
    PackType type;
    int userid;
    char dir[_MAX_PATH_SIZE];
    int fileCount;
    int fileidArray[];
};

//删除文件回复
struct STRU_DELETE_FILE_RS
{
    STRU_DELETE_FILE_RS():type(_DEF_PACK_DELETE_FILE_RS)
      ,result(1){
        memset( dir , 0 , sizeof(dir) );
    }
    PackType type;
    int result;
    char dir[_MAX_PATH_SIZE];
};

/////////////////下载续传协议 //////////////
//请求
#define _DEF_PACK_CONTINUE_DOWNLOAD_RQ     (_DEF_PACK_BASE + 27)
//告诉服务器 从哪里开始传数据块 就可以直接传了  不需要等待收到回复

//服务器中 map 有和没有  有 timestamp  没有 需要唯一确认文件 uid fid fdir
//【MD5校验】新增 md5 字段：客户端本地已下载部分的MD5，服务端据此做续传一致性校验
struct STRU_CONTINUE_DOWNLOAD_RQ
{
    STRU_CONTINUE_DOWNLOAD_RQ():type(_DEF_PACK_CONTINUE_DOWNLOAD_RQ){
        userid = 0;
        timestamp = 0;
        fileid = 0;
        pos = 0;
        memset( dir , 0 , sizeof(dir));
        memset( md5 , 0 , sizeof(md5));
    }

    PackType type;
    int userid;
    int timestamp;
    int fileid;
    int pos;
    char dir[_MAX_PATH_SIZE];
    char md5[_MAX_SIZE];  // 本地已下载部分内容的MD5
};

//【MD5校验】新增：下载续传回复（+56），result=1允许续传（随后开始发文件块），0=拒绝续传
#define _DEF_PACK_CONTINUE_DOWNLOAD_RS     (_DEF_PACK_BASE + 56)

struct STRU_CONTINUE_DOWNLOAD_RS
{
    STRU_CONTINUE_DOWNLOAD_RS():type(_DEF_PACK_CONTINUE_DOWNLOAD_RS){
        fileid = 0;
        timestamp = 0;
        result = 1;
    }
    PackType type;
    int fileid;
    int timestamp;
    int result;  // 1=允许续传 0=拒绝（MD5不一致等原因）
};
/// 上传续传协议 /////////////
/// 会询问服务器 已经上传到哪里了  所以要有回复 服务器把已经到哪里了 传给客户端
/// 服务器 map 有 userid + timestamp 没有 查表 与下载续传查表 流程一样.
//请求
#define _DEF_PACK_CONTINUE_UPLOAD_RQ     (_DEF_PACK_BASE + 28)
//回复  客户端在回复处理时 , 发文件块
#define _DEF_PACK_CONTINUE_UPLOAD_RS     (_DEF_PACK_BASE + 29)

//【MD5校验】新增 md5 字段：客户端本地文件的MD5，服务端据此做续传一致性校验
struct STRU_CONTINUE_UPLOAD_RQ
{
    STRU_CONTINUE_UPLOAD_RQ():type(_DEF_PACK_CONTINUE_UPLOAD_RQ){
        userid = 0;
        timestamp = 0;
        fileid = 0;
        memset( dir , 0 , sizeof(dir));
        memset( md5 , 0 , sizeof(md5));
    }

    PackType type;
    int userid;
    int timestamp;
    int fileid;
    char dir[_MAX_PATH_SIZE];
    char md5[_MAX_SIZE];  // 本地文件MD5
};

struct STRU_CONTINUE_UPLOAD_RS
{
    STRU_CONTINUE_UPLOAD_RS():type(_DEF_PACK_CONTINUE_UPLOAD_RS){
        fileid = 0;
        timestamp = 0;
        pos = 0;
        //memset( dir , 0 , sizeof(dir));
    }

    PackType type;
    int fileid;
    int timestamp;
    int pos;
    //char dir[_MAX_PATH_SIZE];
};

///////////// 连接池绑定 //////////////////
//超大文件分片并行传输：将额外连接与用户会话绑定
#define _DEF_PACK_BIND_POOL_RQ      (_DEF_PACK_BASE + 30)
#define _DEF_PACK_BIND_POOL_RS      (_DEF_PACK_BASE + 31)

struct STRU_BIND_POOL_RQ
{
    STRU_BIND_POOL_RQ():type(_DEF_PACK_BIND_POOL_RQ), userid(0), connIndex(0){}
    PackType type;
    int userid;
    int connIndex;  // 池中连接索引 0..N-1
};

struct STRU_BIND_POOL_RS
{
    STRU_BIND_POOL_RS():type(_DEF_PACK_BIND_POOL_RS), result(0){}
    PackType type;
    int result;  // 1 成功
};

///////////// 分片上传 //////////////////
//超大文件（>100MB）拆分到多条池连接并行上传
#define _DEF_PACK_CHUNK_UPLOAD_RQ    (_DEF_PACK_BASE + 32)
#define _DEF_PACK_CHUNK_UPLOAD_RS    (_DEF_PACK_BASE + 33)

struct STRU_CHUNK_UPLOAD_RQ
{
    STRU_CHUNK_UPLOAD_RQ():type(_DEF_PACK_CHUNK_UPLOAD_RQ),
        userid(0), timestamp(0), fileid(0),
        segOffset(0), segSize(0), totalSize(0), segProgress(0){
        memset(fileName, 0, sizeof(fileName));
        memset(dir, 0, sizeof(dir));
        memset(md5, 0, sizeof(md5));
        memset(fileType, 0, sizeof(fileType));
        memset(time, 0, sizeof(time));
    }
    PackType type;
    int userid;
    int timestamp;
    int fileid;
    int segOffset;      // 本段在文件中的字节偏移
    int segSize;        // 本段大小
    int totalSize;      // 文件总大小
    int segProgress;    // 本地已完成的分片字节数，用于断点续传
    char fileName[_MAX_PATH_SIZE];
    char dir[_MAX_PATH_SIZE];
    char md5[_MAX_SIZE];
    char fileType[_MAX_SIZE];
    char time[_MAX_SIZE];
};

struct STRU_CHUNK_UPLOAD_RS
{
    STRU_CHUNK_UPLOAD_RS():type(_DEF_PACK_CHUNK_UPLOAD_RS),
        userid(0), fileid(0), segOffset(0), result(1){}
    PackType type;
    int userid;
    int fileid;
    int segOffset;
    int result;
};

//分片下载请求（每条池连接独立请求其分段）
#define _DEF_PACK_CHUNK_DOWNLOAD_RQ  (_DEF_PACK_BASE + 34)
#define _DEF_PACK_CHUNK_DOWNLOAD_RS  (_DEF_PACK_BASE + 35)

struct STRU_CHUNK_DOWNLOAD_RQ
{
    STRU_CHUNK_DOWNLOAD_RQ():type(_DEF_PACK_CHUNK_DOWNLOAD_RQ),
        userid(0), fileid(0), timestamp(0),
        segOffset(0), segSize(0){
        memset(dir, 0, sizeof(dir));
    }
    PackType type;
    int userid;
    int fileid;
    int timestamp;
    int segOffset;
    int segSize;
    char dir[_MAX_PATH_SIZE];
};

struct STRU_CHUNK_DOWNLOAD_RS
{
    STRU_CHUNK_DOWNLOAD_RS():type(_DEF_PACK_CHUNK_DOWNLOAD_RS), userid(0), fileid(0), timestamp(0), segOffset(0), seq(0), len(0), result(1){ memset(content,0,sizeof(content)); }
    PackType type;
    int userid;
    int fileid;
    int timestamp;
    int segOffset;
    int seq;
    int len;
    int result;
    char content[_DEF_BUFFER];
};

//心跳协议
  #define _DEF_PACK_HEARTBEAT_RQ      (_DEF_PACK_BASE + 36)
  #define _DEF_PACK_HEARTBEAT_RS      (_DEF_PACK_BASE + 37)

  struct STRU_HEARTBEAT_RQ
  {
      STRU_HEARTBEAT_RQ():type(_DEF_PACK_HEARTBEAT_RQ), seq(0){}
      PackType type;
      int seq;
  };

  struct STRU_HEARTBEAT_RS
  {
      STRU_HEARTBEAT_RS():type(_DEF_PACK_HEARTBEAT_RS), seq(0){}
      PackType type;
      int seq;
  };

  //MD5校验协议
  #define _DEF_PACK_MD5_CHECK_RQ     (_DEF_PACK_BASE + 38)
  #define _DEF_PACK_MD5_CHECK_RS     (_DEF_PACK_BASE + 39)

  struct STRU_MD5_CHECK_RQ
  {
      STRU_MD5_CHECK_RQ():type(_DEF_PACK_MD5_CHECK_RQ), fileid(0), userid(0){}
      PackType type;
      int fileid;
      int userid;
  };

  struct STRU_MD5_CHECK_RS
  {
      STRU_MD5_CHECK_RS():type(_DEF_PACK_MD5_CHECK_RS), fileid(0), match(0){}
      PackType type;
      int fileid;
      int match;  // 1=一致, 0=不一致
  };

  //文件搜索协议
  #define _DEF_PACK_SEARCH_FILE_RQ    (_DEF_PACK_BASE + 40)
  #define _DEF_PACK_SEARCH_FILE_RS    (_DEF_PACK_BASE + 41)

  struct STRU_SEARCH_FILE_RQ
  {
      STRU_SEARCH_FILE_RQ():type(_DEF_PACK_SEARCH_FILE_RQ), userid(0){
          memset(keyword, 0, sizeof(keyword));
      }
      PackType type;
      int userid;
      char keyword[_MAX_PATH_SIZE];
  };

  struct STRU_SEARCH_RESULT_ITEM
  {
      int fileid;
      char name[_MAX_PATH_SIZE];
      char dir[_MAX_PATH_SIZE];
      int size;
      char time[_MAX_SIZE];
      char fileType[_DEF_TYPE_LEN];
  };

  struct STRU_SEARCH_FILE_RS
  {
      void init() {
          type = _DEF_PACK_SEARCH_FILE_RS;
          count = 0;
      }
      PackType type;
      int count;
      STRU_SEARCH_RESULT_ITEM items[];
  };

  //手机验证码协议
  #define _DEF_PACK_SEND_VERIFY_CODE_RQ   (_DEF_PACK_BASE + 42)
  #define _DEF_PACK_SEND_VERIFY_CODE_RS   (_DEF_PACK_BASE + 43)

  struct STRU_SEND_VERIFY_CODE_RQ
  {
      STRU_SEND_VERIFY_CODE_RQ():type(_DEF_PACK_SEND_VERIFY_CODE_RQ){
          memset(tel, 0, sizeof(tel));
      }
      PackType type;
      char tel[_MAX_SIZE];
  };

  struct STRU_SEND_VERIFY_CODE_RS
  {
      STRU_SEND_VERIFY_CODE_RS():type(_DEF_PACK_SEND_VERIFY_CODE_RS), result(0){
          memset(code, 0, sizeof(code));
      }
      PackType type;
      int result;
      char code[8];
  };

  //传输控制协议（暂停/恢复/取消）
  #define _DEF_PACK_TRANSFER_CTRL_RQ     (_DEF_PACK_BASE + 44)
  #define _DEF_PACK_TRANSFER_CTRL_RS     (_DEF_PACK_BASE + 45)

  //========== 收藏文件协议 ==========
  //添加/取消收藏请求
  #define _DEF_PACK_FAVORITE_FILE_RQ    (_DEF_PACK_BASE + 46)
  //添加/取消收藏回复
  #define _DEF_PACK_FAVORITE_FILE_RS    (_DEF_PACK_BASE + 47)

  #define FAVORITE_ADD    1   // 添加收藏
  #define FAVORITE_REMOVE 0   // 取消收藏

  //收藏文件请求
  struct STRU_FAVORITE_FILE_RQ
  {
      void init()
      {
          type = _DEF_PACK_FAVORITE_FILE_RQ;
          userid = 0;
          command = FAVORITE_ADD;
          fileCount = 0;
          memset(dir, 0, sizeof(dir));
      }
      PackType type;
      int userid;
      int command;   // 1=添加收藏, 0=取消收藏
      char dir[_MAX_PATH_SIZE];
      int fileCount;
      int fileidArray[];
  };

  //收藏文件回复
  struct STRU_FAVORITE_FILE_RS
  {
      STRU_FAVORITE_FILE_RS():type(_DEF_PACK_FAVORITE_FILE_RS), result(0){}
      PackType type;
      int result;  // 1=成功
  };

  //获取收藏列表请求
  #define _DEF_PACK_GET_FAVORITES_RQ    (_DEF_PACK_BASE + 48)
  //获取收藏列表回复
  #define _DEF_PACK_GET_FAVORITES_RS    (_DEF_PACK_BASE + 49)

  //获取收藏列表请求
  struct STRU_GET_FAVORITES_RQ
  {
      STRU_GET_FAVORITES_RQ():type(_DEF_PACK_GET_FAVORITES_RQ), userid(0){}
      PackType type;
      int userid;
  };

  //收藏文件信息
  struct STRU_FAVORITE_ITEM
  {
      int fileid;
      char name[_MAX_PATH_SIZE];
      char dir[_MAX_PATH_SIZE];
      int size;
      char time[_MAX_SIZE];
      char fileType[_DEF_TYPE_LEN];
  };

  //获取收藏列表回复
  struct STRU_GET_FAVORITES_RS
  {
      void init() {
          type = _DEF_PACK_GET_FAVORITES_RS;
          count = 0;
      }
      PackType type;
      int count;
      STRU_FAVORITE_ITEM items[];  // 柔性数组
  };

  //========== 回收站协议 ==========
  //移入回收站请求
  #define _DEF_PACK_RECYCLE_FILE_RQ    (_DEF_PACK_BASE + 50)
  //移入回收站回复
  #define _DEF_PACK_RECYCLE_FILE_RS    (_DEF_PACK_BASE + 51)

  //移入回收站请求
  struct STRU_RECYCLE_FILE_RQ
  {
      void init()
      {
          type = _DEF_PACK_RECYCLE_FILE_RQ;
          userid = 0;
          fileCount = 0;
          memset(dir, 0, sizeof(dir));
      }
      PackType type;
      int userid;
      char dir[_MAX_PATH_SIZE];
      int fileCount;
      int fileidArray[];
  };

  //移入回收站回复
  struct STRU_RECYCLE_FILE_RS
  {
      STRU_RECYCLE_FILE_RS():type(_DEF_PACK_RECYCLE_FILE_RS), result(1){
          memset(dir, 0, sizeof(dir));
      }
      PackType type;
      int result;
      char dir[_MAX_PATH_SIZE];
  };

  //获取回收站列表请求
  #define _DEF_PACK_GET_RECYCLE_RQ     (_DEF_PACK_BASE + 52)
  //获取回收站列表回复
  #define _DEF_PACK_GET_RECYCLE_RS     (_DEF_PACK_BASE + 53)

  //获取回收站列表请求
  struct STRU_GET_RECYCLE_RQ
  {
      STRU_GET_RECYCLE_RQ():type(_DEF_PACK_GET_RECYCLE_RQ), userid(0){}
      PackType type;
      int userid;
  };

  //回收站文件信息
  struct STRU_RECYCLE_ITEM
  {
      int fileid;
      char name[_MAX_PATH_SIZE];
      char dir[_MAX_PATH_SIZE];        // 原始目录
      int size;
      char deleteTime[_MAX_SIZE];      // 删除时间
      char fileType[_DEF_TYPE_LEN];
  };

  //获取回收站列表回复
  struct STRU_GET_RECYCLE_RS
  {
      void init() {
          type = _DEF_PACK_GET_RECYCLE_RS;
          count = 0;
      }
      PackType type;
      int count;
      STRU_RECYCLE_ITEM items[];  // 柔性数组
  };

  //从回收站恢复请求
  #define _DEF_PACK_RESTORE_FILE_RQ    (_DEF_PACK_BASE + 54)
  //从回收站恢复回复
  #define _DEF_PACK_RESTORE_FILE_RS    (_DEF_PACK_BASE + 55)

  //从回收站恢复请求
  struct STRU_RESTORE_FILE_RQ
  {
      void init()
      {
          type = _DEF_PACK_RESTORE_FILE_RQ;
          userid = 0;
          fileCount = 0;
      }
      PackType type;
      int userid;
      int fileCount;
      int fileidArray[];
  };

  //从回收站恢复回复
  struct STRU_RESTORE_FILE_RS
  {
      STRU_RESTORE_FILE_RS():type(_DEF_PACK_RESTORE_FILE_RS), result(0){}
      PackType type;
      int result;  // 1=成功
  };

  #define TRANSFER_PAUSE    1
  #define TRANSFER_RESUME   2
  #define TRANSFER_CANCEL   3

  struct STRU_TRANSFER_CTRL_RQ
  {
      STRU_TRANSFER_CTRL_RQ():type(_DEF_PACK_TRANSFER_CTRL_RQ),
          userid(0), timestamp(0), command(0){}
      PackType type;
      int userid;
      int timestamp;
      int command;
  };

  struct STRU_TRANSFER_CTRL_RS
  {
      STRU_TRANSFER_CTRL_RS():type(_DEF_PACK_TRANSFER_CTRL_RS),
          userid(0), timestamp(0), result(0), pos(0){}
      PackType type;
      int userid;
      int timestamp;
      int result;
      int pos;
  };

//浏览分享目录（预览用，不复制文件到自己网盘）
#define _DEF_PACK_BROWSE_SHARE_RQ   (_DEF_PACK_BASE + 60)
#define _DEF_PACK_BROWSE_SHARE_RS   (_DEF_PACK_BASE + 61)

#define browse_share_success        0
#define browse_share_invalid_link   1
#define browse_share_wrong_password 2

struct STRU_BROWSE_SHARE_RQ
{
    STRU_BROWSE_SHARE_RQ():type(_DEF_PACK_BROWSE_SHARE_RQ),
        userid(0), shareLink(0){
        memset(subDir,   0, sizeof(subDir));
        memset(password, 0, sizeof(password));
    }
    PackType type;
    int userid;
    int shareLink;
    char subDir[_MAX_PATH_SIZE];       // 子目录，初次请求填 ""
    char password[_DEF_SHARE_PWD_LEN];
};

struct STRU_BROWSE_SHARE_RS
{
    void init(){
        type   = _DEF_PACK_BROWSE_SHARE_RS;
        result = 0;
        count  = 0;
        memset(curDir, 0, sizeof(curDir));
    }
    PackType type;
    int result;
    char curDir[_MAX_PATH_SIZE];       // 当前目录（回显）
    int count;
    STRU_FILE_INFO items[];            // 复用已有文件信息结构
};

//下载分享文件（用于预览）
#define _DEF_PACK_DOWNLOAD_SHARE_FILE_RQ   (_DEF_PACK_BASE + 62)

struct STRU_DOWNLOAD_SHARE_FILE_RQ
{
    STRU_DOWNLOAD_SHARE_FILE_RQ():type(_DEF_PACK_DOWNLOAD_SHARE_FILE_RQ),
        userid(0), shareLink(0), fileid(0){
        memset(password, 0, sizeof(password));
    }
    PackType type;
    int userid;
    int shareLink;
    int fileid;  // 要下载的文件ID
    char password[_DEF_SHARE_PWD_LEN];
};

//========== 回收站彻底删除协议 ==========
//现新增专用协议，服务端直接从回收站表删除，解决彻底删除无效的问题。
#define _DEF_PACK_DELETE_FOREVER_RQ   (_DEF_PACK_BASE + 64)
#define _DEF_PACK_DELETE_FOREVER_RS   (_DEF_PACK_BASE + 65)

struct STRU_DELETE_FOREVER_RQ
{
    void init()
    {
        type = _DEF_PACK_DELETE_FOREVER_RQ;
        userid = 0;
        fileCount = 0;
    }
    PackType type;
    int userid;
    int fileCount;
    int fileidArray[];  // 待彻底删除的文件id数组
};

struct STRU_DELETE_FOREVER_RS
{
    STRU_DELETE_FOREVER_RS():type(_DEF_PACK_DELETE_FOREVER_RS), result(0){}
    PackType type;
    int result;  // 1=成功
};
