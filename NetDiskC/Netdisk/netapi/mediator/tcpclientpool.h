#ifndef TCPCLIENTPOOL_H
#define TCPCLIENTPOOL_H

#include <QObject>
#include <QVector>
#include "TcpClientMediator.h"

//TCP 连接池：为超大文件（>100MB）分片并行传输提供多路连接//默认 4 条连接，每条独立收发，互不阻塞
class TcpClientPool : public QObject
{
    Q_OBJECT
public:
    explicit TcpClientPool(int count = 4, QObject *parent = nullptr);
    ~TcpClientPool();

    //打开所有连接到服务器    bool openAll(const char* ip, int port);
    //关闭所有连接    void closeAll();
    //获取第 index 条连接（0-based）    TcpClientMediator* connection(int index);
    //连接总数    int count() const;
    //所有连接是否均已打开    bool isAllOpen() const;

private:
    QVector<TcpClientMediator*> m_connections;
    int m_count;
};

#endif // TCPCLIENTPOOL_H
