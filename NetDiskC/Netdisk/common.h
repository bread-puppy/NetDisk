#ifndef COMMON_H
#define COMMON_H
#include<QString>
#include<QFile>
#include<QSharedPointer>

//文件信息
struct FileInfo
{
    FileInfo():fileid(0) , size(0),pFile( nullptr ), pos(0) , isPause(0),timestamp(0),
        qFile(nullptr), mappedData(nullptr), useMmap(false){}

    int fileid;
    QString name;
    QString dir;  //目录
    QString time;
    int size;  //int是32位，有符号最大值是2G，假定网盘最大容量是2G
    QString md5;
    QString type;
    QString absolutePath;  //文件本地绝对路径

    int pos; //上传或下载到什么位置
    int timestamp;  //时间戳  文件身份
    int isPause; //暂停  0 1
    bool previewShown; // 分享预览是否已触发（收到512KB即显示）

    //文件指针（传统 FILE* 读写，作为 mmap 失败时的回退方案）
    FILE* pFile;
    //mmap 内存映射（优先使用，零拷贝 I/O）    QSharedPointer<QFile> qFile;
    uchar* mappedData;
    bool useMmap;
    //字节单位换算
    static QString getSize(int size){  //得到KB，MB
        QString res;
        int count=0;
        int tmp=size;
        while(tmp!=0){
            tmp/=1024;
            if(tmp!=0) count++;
        }
        switch(count){
        case 0:
            res=QString("0.%1KB").arg((int)(size%1024/1024.0*100),2,10,QChar('0'));  //0.0x KB
            //arg()参数，第二个多宽 第三进制 第四 不够宽度 缺省的字符
            if(size!=0&&res=="0.00KB")
                res="0.01KB";
            break;
        case 1:
            res=QString("%1.%2KB").arg(size/1024).arg((int)(size%1024/1024.0*100),2,10,QChar('0'));
            break;
        case 2:
        case 3:
            res=QString("%1.%2MB").arg(size/1024/1024).arg((int)(size/1024%1024/1024.0*100),2,10,QChar('0'));
            break;
        default:  //过大
            break;
        }
        return res;
    }
};

#endif // COMMON_H
