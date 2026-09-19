#ifndef _MYSQL_H
#define _MYSQL_H
#include "packdef.h"
#include <mysql/mysql.h>
#include<list>
#include<string>
#include<vector>

using namespace  std;


class CMysql{
public:
    int ConnectMysql(const char *server, const char *user, const char *password, const char *database);
    int SelectMysql(char* szSql,int nColumn,list<string>& lst);
    int UpdataMysql(char *szsql);
    //在同一个事务中原子执行多条 SQL，失败时自动回滚    bool ExecuteTransaction(const vector<string>& sqlList);
    void DisConnect();
private:
    MYSQL *conn;

    pthread_mutex_t m_lock;
};




#endif
