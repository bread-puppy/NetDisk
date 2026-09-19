#include "tcpclientpool.h"
#include <QDebug>

TcpClientPool::TcpClientPool(int count, QObject *parent)
    : QObject(parent), m_count(count)
{
    for (int i = 0; i < m_count; ++i) {
        m_connections.append(new TcpClientMediator);
    }
}

TcpClientPool::~TcpClientPool()
{
    closeAll();
    for (auto* conn : m_connections) {
        delete conn;
    }
    m_connections.clear();
}

bool TcpClientPool::openAll(const char* ip, int port)
{
    for (int i = 0; i < m_count; ++i) {
        if (!m_connections[i]->OpenNet(ip, port)) {
            qDebug() << "TcpClientPool: connection" << i << "failed to open";
            return false;
        }
        qDebug() << "TcpClientPool: connection" << i << "opened";
    }
    return true;
}

void TcpClientPool::closeAll()
{
    for (int i = 0; i < m_count; ++i) {
        m_connections[i]->CloseNet();
    }
}

TcpClientMediator* TcpClientPool::connection(int index)
{
    if (index < 0 || index >= m_count) return nullptr;
    return m_connections[index];
}

int TcpClientPool::count() const
{
    return m_count;
}

bool TcpClientPool::isAllOpen() const
{
    for (auto* conn : m_connections) {
        if (!conn || !conn->IsConnected())
            return false;
    }
    return true;
}
