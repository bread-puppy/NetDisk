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

    bool openAll(const char* ip, int port);
    void closeAll();
    TcpClientMediator* connection(int index);
    int count() const;
    bool isAllOpen() const;

private:
    QVector<TcpClientMediator*> m_connections;
    int m_count;
};

#endif // TCPCLIENTPOOL_H
