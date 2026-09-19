#include "maindialog.h"
#include "ui_maindialog.h"
#include<QMessageBox>
#include <QDateTime>
#include<QDebug>
#include<QFileDialog>
#include<QProgressBar>
#include"videoplayer/playerdialog.h"
#include<QFileInfo>
#include<QSet>
#include <QInputDialog>
#include <QTextCodec>
#include <algorithm>
#include <functional>
#include "packdef.h"

//视频缩略图用到的头文件
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}

MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MainDialog)
{
    ui->setupUi(this);
    ui->sw_page->setCurrentIndex(0);  //默认文件分页
    ui->tw_transmit->setCurrentIndex(2);  //传输默认分页
    this->setWindowTitle("我的网盘");  //设置标题栏
    this->setWindowFlags(Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint);  //设置最小最大化

    //去掉按钮的焦点框：点击后按钮会一直带着一圈颜色边框，
    //只有按住时才有按压效果、松开后不留任何边框才符合预期    const QList<QPushButton*> btns = this->findChildren<QPushButton*>();
    for (QPushButton* b : btns)
        b->setFocusPolicy(Qt::NoFocus);

    //定义菜单项，资源路径 :/images/folder.png
    QAction* action_addFolder=new QAction(QIcon(":/images/folder.png"),"新建文件夹");
    QAction* action_uploadFile=new QAction("上传文件");
    QAction* action_uploadFolder=new QAction("上传文件夹");
    //添加菜单项
    m_menuAddFile.addAction(action_addFolder);
    m_menuAddFile.addSeparator();  //加入分隔符
    m_menuAddFile.addAction(action_uploadFile);
    m_menuAddFile.addAction(action_uploadFolder);
    connect(action_addFolder,SIGNAL(triggered(bool)),this,SLOT(slot_addFolder(bool)));
    connect(action_uploadFile,SIGNAL(triggered(bool)),this,SLOT(slot_uploadFile(bool)));
    connect(action_uploadFolder,SIGNAL(triggered(bool)),this,SLOT(slot_uploadFolder(bool)));

    QAction *action_downloadFile=new QAction("下载文件");
    QAction *action_shareFile=new QAction("分享文件");
    QAction *action_getShare=new QAction("获取分享");  //获取分享到本地目录
    QAction *action_playVideo=new QAction("播放视频");
    QAction *action_searchFile = new QAction("搜索文件");
    QAction *action_favoriteFile = new QAction("收藏");
    QAction *action_recycleFile = new QAction("加入回收站");

    m_menuFileInfo.addAction(action_addFolder);
    m_menuFileInfo.addSeparator();  //加入分隔符
    m_menuFileInfo.addAction(action_downloadFile);
    m_menuFileInfo.addAction(action_shareFile);
    m_menuFileInfo.addAction(action_favoriteFile);
    m_menuFileInfo.addAction(action_recycleFile);
    m_menuFileInfo.addAction(action_playVideo);
    m_menuFileInfo.addSeparator();  //加入分隔符
    m_menuFileInfo.addAction(action_getShare);
    m_menuFileInfo.addAction(action_searchFile);

    connect(action_downloadFile,SIGNAL(triggered(bool)),this,SLOT(slot_downloadFile(bool)));
    connect(action_shareFile,SIGNAL(triggered(bool)),this,SLOT(slot_shareFile(bool)));
    connect(action_getShare,SIGNAL(triggered(bool)),this,SLOT(slot_getShare(bool)));
    connect(action_playVideo,SIGNAL(triggered(bool)),this,SLOT(slot_playVideo(bool)));
    connect(action_searchFile, SIGNAL(triggered(bool)), this,SLOT(slot_searchFile(bool)));
    connect(action_favoriteFile, SIGNAL(triggered(bool)), this, SLOT(slot_favoriteFile(bool)));
    connect(action_recycleFile, SIGNAL(triggered(bool)), this, SLOT(slot_recycleFile(bool)));

    //【已完成列表】右键菜单删除选中记录 + 支持多选
    QAction* action_deleteComplete=new QAction("删除选中记录");
    m_menuComplete.addAction(action_deleteComplete);
    connect(action_deleteComplete,SIGNAL(triggered(bool)),this,SLOT(slot_deleteCompleteSelected()));
    ui->table_complete->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->table_complete->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(ui->table_complete,&QTableWidget::customContextMenuRequested,this,[this](QPoint){this->m_menuComplete.exec(QCursor::pos());});
    //初始化视频播放器    m_playerDialog = new PlayerDialog;
    m_sysPath = "";
    m_browseShareCode = 0;
    m_browseSharePwd  = "";
    m_browseShareCurDir = "/";
    m_currentLoadingCode = 0;
    m_isLoadingShares = false;

    //查看分享：双击进入文件夹或预览    connect(ui->table_viewShare, &QTableWidget::cellDoubleClicked,
            this, &MainDialog::slot_browseShareEnterFolder);
    //查看分享：单击显示预览    connect(ui->table_viewShare, &QTableWidget::cellClicked,
            this, &MainDialog::on_table_viewShare_cellClicked);

    //添加右键显示菜单 lambda表达式--匿名函数
    connect(ui->table_download,&QTableWidget::customContextMenuRequested,this,[this](QPoint){this->m_menuDownload.exec(QCursor::pos());});
    connect(ui->table_upload,&QTableWidget::customContextMenuRequested,this,[this](QPoint){this->m_menuUpload.exec(QCursor::pos());});
    //添加菜单项
    QAction* actionUploadPause=new QAction("暂停");
    QAction* actionUploadResume=new QAction("开始");
    QAction* actionDownloadPause=new QAction("暂停");
    QAction* actionDownloadResume=new QAction("开始");

    m_menuUpload.addAction(actionUploadPause);
    m_menuUpload.addAction(actionUploadResume);
    QAction* actionUploadPauseAll = m_menuUpload.addAction("全部暂停");
    QAction* actionUploadResumeAll = m_menuUpload.addAction("全部开始");
    m_menuDownload.addAction(actionDownloadPause);
    m_menuDownload.addAction(actionDownloadResume);
    QAction* actionDownloadPauseAll = m_menuDownload.addAction("全部暂停");
    QAction* actionDownloadResumeAll = m_menuDownload.addAction("全部开始");

    connect(actionUploadPause,SIGNAL(triggered(bool)),this,SLOT(slot_uploadPause(bool)));
    connect(actionUploadResume,SIGNAL(triggered(bool)),this,SLOT(slot_uploadResume(bool)));
    connect(actionDownloadPause,SIGNAL(triggered(bool)),this,SLOT(slot_downloadPause(bool)));
    connect(actionDownloadResume,SIGNAL(triggered(bool)),this,SLOT(slot_downloadResume(bool)));

    //"全部暂停"/"全部开始" — 遍历所有行执行操作    connect(actionUploadPauseAll, &QAction::triggered, this, [this](){
        for(int i=0; i<ui->table_upload->rowCount(); ++i){
            MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_upload->item(i,0);
            QPushButton* btn=(QPushButton*)ui->table_upload->cellWidget(i,5);
            if(item0 && btn && btn->text()=="暂停"){
                btn->setText("开始");
                Q_EMIT SIG_setUploadPause(item0->m_info.timestamp,1);
            }
        }
    });
    connect(actionUploadResumeAll, &QAction::triggered, this, [this](){
        for(int i=0; i<ui->table_upload->rowCount(); ++i){
            MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_upload->item(i,0);
            QPushButton* btn=(QPushButton*)ui->table_upload->cellWidget(i,5);
            if(item0 && btn && btn->text()=="开始"){
                btn->setText("暂停");
                Q_EMIT SIG_setUploadPause(item0->m_info.timestamp,0);
            }
        }
    });
    connect(actionDownloadPauseAll, &QAction::triggered, this, [this](){
        for(int i=0; i<ui->table_download->rowCount(); ++i){
            MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_download->item(i,0);
            QPushButton* btn=(QPushButton*)ui->table_download->cellWidget(i,5);
            if(item0 && btn && btn->text()=="暂停"){
                btn->setText("开始");
                Q_EMIT SIG_setDownloadPause(item0->m_info.timestamp,1);
            }
        }
    });
    connect(actionDownloadResumeAll, &QAction::triggered, this, [this](){
        for(int i=0; i<ui->table_download->rowCount(); ++i){
            MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_download->item(i,0);
            QPushButton* btn=(QPushButton*)ui->table_download->cellWidget(i,5);
            if(item0 && btn && btn->text()=="开始"){
                btn->setText("暂停");
                Q_EMIT SIG_setDownloadPause(item0->m_info.timestamp,0);
            }
        }
    });

    connect(ui->pb_search, SIGNAL(clicked(bool)), this, SLOT(slot_searchFile(bool)));
    connect(ui->pb_searchBack, SIGNAL(clicked(bool)), this, SLOT(on_pb_searchBack_clicked()));

    //左侧按钮：收藏和回收站页面切换    connect(ui->pb_store, SIGNAL(clicked(bool)), this, SLOT(on_pb_store_clicked()));
    connect(ui->pb_bin, SIGNAL(clicked(bool)), this, SLOT(on_pb_bin_clicked()));

    //回收站页面按钮    connect(ui->pb_restoreFile, SIGNAL(clicked(bool)), this, SLOT(on_pb_restoreFile_clicked()));
    connect(ui->pb_deleteForever, SIGNAL(clicked(bool)), this, SLOT(on_pb_deleteForever_clicked()));

    //收藏页右键菜单    QAction* action_cancelFavorite = new QAction("取消收藏");
    m_menuFavorite.addAction(action_cancelFavorite);
    connect(action_cancelFavorite, SIGNAL(triggered(bool)), this, SLOT(slot_cancelFavorite(bool)));
    connect(ui->table_favorite, &QTableWidget::customContextMenuRequested, this, [this](QPoint){
        m_menuFavorite.exec(QCursor::pos());
    });

    //回收站页右键菜单    QAction* action_restoreFile = new QAction("恢复");
    QAction* action_deleteForever = new QAction("彻底删除");
    m_menuRecycle.addAction(action_restoreFile);
    m_menuRecycle.addAction(action_deleteForever);
    connect(action_restoreFile, SIGNAL(triggered(bool)), this, SLOT(on_pb_restoreFile_clicked()));
    connect(action_deleteForever, SIGNAL(triggered(bool)), this, SLOT(on_pb_deleteForever_clicked()));
    connect(ui->table_recycle, &QTableWidget::customContextMenuRequested, this, [this](QPoint){
        m_menuRecycle.exec(QCursor::pos());
    });
}

MainDialog::~MainDialog()
{
    delete ui;
}

void MainDialog::closeEvent(QCloseEvent *event)
{
    //关闭信号没有参数
    if(QMessageBox::question(this,"退出提示","是否退出?")==QMessageBox::Yes){
        event->accept();  //同意退出，关闭
        Q_EMIT SIG_close();  //回收
    }else{
        event->ignore();
    }
}

void MainDialog::slot_setInfo(QString name)
{
    ui->pb_name->setText(name);
}


void MainDialog::on_pb_file_clicked()
{
    ui->sw_page->setCurrentIndex(0);
}


void MainDialog::on_pb_transmit_clicked()
{
    ui->sw_page->setCurrentIndex(1);
}


void MainDialog::on_pb_share_clicked()
{
    ui->sw_page->setCurrentIndex(2);
    ui->tw_share->setCurrentIndex(0);  // 显示"我的分享"标签页
}


void MainDialog::on_pb_addFile_clicked()  //点击添加文件
{
    //弹出菜单
    m_menuAddFile.exec(QCursor::pos());  //鼠标的坐标 在该点显示菜单
}

#include<QInputDialog>
void MainDialog::slot_addFolder(bool flag)  //新建文件夹
{
    qDebug()<<__func__;
    //弹出输入窗口
    QString name=QInputDialog::getText(this,"新建文件夹","输入名称");
    QString tmp=name;
    //空白符的处理
    if(name.isEmpty()||tmp.remove(" ").isEmpty()||name.length()>100){
        QMessageBox::about(this,"提示","名字非法");
        return;
    }
    //不可以用的名字 。。。
    //一些非法的符号
    if(name.contains("\\")||name.contains("/")||name.contains(":")||name.contains("?")||name.contains("*")
            ||name.contains("<")||name.contains(">")||name.contains("|")||name.contains("\"")){
        QMessageBox::about(this,"提示","名字非法");
        return;
    }
    //判断是否现在已经存在 todo

    QString dir=ui->lb_path->text();
    Q_EMIT SIG_addFolder(name,dir);
}

void MainDialog::slot_uploadFile(bool flag)  //上传文件
{
    qDebug()<<__func__;
    QString path=QFileDialog::getOpenFileName(this,"选择文件","./");  //弹出窗口，选择文件
    if(path.isEmpty()) return;
    //目前上传的有没有一样的文件，如果是取消 todo
    //发送信号 核心处理类 传递的信息：上传XX文件到XX目录
    QString dir=ui->lb_path->text();
    Q_EMIT SIG_uploadFile(path,dir);
}

void MainDialog::slot_uploadFolder(bool flag)
{
    qDebug()<<__func__;
    //点击 弹出文件选择对话框 选择路径
    QString path=QFileDialog::getExistingDirectory(this,"选择文件夹","./");
    //判断非空
    if(path.isEmpty()) return;
    //过滤 是否正在传 todo
    //发信号 上传XX路径的文件夹到XX目录下
    Q_EMIT SIG_uploadFolder(path,ui->lb_path->text());
}

void MainDialog::slot_downloadFile(bool flag)
{
    qDebug()<<__func__;
    //遍历列表
    int rows=ui->table_file->rowCount();
    QString dir=ui->lb_path->text();  //获取目录
    for(int i=0;i<rows;++i){
        //看选中的
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_file->item(i,0);
        if(item0->checkState()==Qt::Checked){

            qDebug()<<"Selected item type:"<<item0->m_info.type<<"name:"<<item0->m_info.name;


            //列表中有这个下载，不能开始 todo 过滤
            //获取类型
            if(item0->m_info.type=="file"){ //发信号 下载文件 下载文件夹
                Q_EMIT SIG_downloadFile(item0->m_info.fileid,dir);
            }
            else{
                Q_EMIT SIG_downloadFolder(item0->m_info.fileid,dir);
            }
        }
    }
}

void MainDialog::slot_shareFile(bool flag)
{
    qDebug()<<__func__;
    //申请数组
    QVector<int> array;
    int count=ui->table_file->rowCount();
    //遍历所有项
    for(int i=0;i<count;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_file->item(i,0);
        //看是否是打钩的
        if(item0->checkState()==Qt::Checked)
            //添加到数组里
            array.push_back(item0->m_info.fileid);
    }
    if(array.isEmpty()){
        QMessageBox::about(this,"提示","请选择要分享的文件");
        return;
    }
    //去掉"设置分享密码"询问：服务端的分享协议与数据库都不支持    //分享密码，弹窗会让用户以为设置了密码而实际没有生效。    //密码参数保留在信号签名中，此处传空字符串。    Q_EMIT SIG_shareFile(array, ui->lb_path->text(), QString());
}

void MainDialog::slot_deleteFile(bool flag)
{
    qDebug()<<__func__;
    QVector<int> array;
    int count=ui->table_file->rowCount();
    //遍历所有项
    for(int i=0;i<count;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_file->item(i,0);
        //看是否是打钩的
        if(item0->checkState()==Qt::Checked)
            //添加到数组里
            array.push_back(item0->m_info.fileid);
    }
    //发送信号
    Q_EMIT SIG_deleteFile(array,ui->lb_path->text());
}

void MainDialog::slot_getShare(bool flag)
{
    qDebug()<<__func__;
    //弹窗 输入分享码
    QString txt=QInputDialog::getText(this,"获取分享","输入分享码");
    //过滤
    int code=txt.toInt();
    if(txt.length()!=9||code<100000000||code>=1000000000){
        QMessageBox::about(this,"提示","分享码非法");
        return;
    }
    //输入密码    QString password = QInputDialog::getText(this,"密码验证","请输入分享密码（可为空）",
                                             QLineEdit::Password);
    //发送信号 什么目录下添加什么分享码的文件
    Q_EMIT SIG_getshareByLink(code,ui->lb_path->text(),password);
}

void MainDialog::slot_uploadPause(bool flag)
{
    qDebug()<<"上传暂停";
    int rows=ui->table_upload->rowCount();
    //遍历表单
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_upload->item(i,0);
        //看是否打钩
        if(item0->checkState()==Qt::Checked){
            QPushButton* button=(QPushButton*)ui->table_upload->cellWidget(i,5);

            //看按钮的状态 切换文字 发送信号
            if(button->text()=="暂停"){
                //信号 设置文件信息结构体暂停标志位
                button->setText("开始");
                Q_EMIT SIG_setUploadPause(item0->m_info.timestamp,1);
            }
        }
    }
}

void MainDialog::slot_uploadResume(bool flag)
{
    qDebug()<<"上传开始";
    int rows=ui->table_upload->rowCount();
    //遍历表单
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_upload->item(i,0);
        //看是否打钩
        if(item0->checkState()==Qt::Checked){
            QPushButton* button=(QPushButton*)ui->table_upload->cellWidget(i,5);

            //看按钮的状态 切换文字 发送信号
            if(button->text()=="开始"){
                //信号 设置文件信息结构体暂停标志位
                button->setText("暂停");
                Q_EMIT SIG_setUploadPause(item0->m_info.timestamp,0);
            }
        }
    }
}

void MainDialog::slot_downloadPause(bool flag)
{
    qDebug()<<"下载暂停";
    int rows=ui->table_download->rowCount();
    //遍历表单
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_download->item(i,0);
        //看是否打钩
        if(item0->checkState()==Qt::Checked){
            QPushButton* button=(QPushButton*)ui->table_download->cellWidget(i,5);
            //看按钮的状态 切换文字 发送信号
            if(button->text()=="暂停"){
                //信号 设置文件信息结构体暂停标志位
                button->setText("开始");
                Q_EMIT SIG_setDownloadPause(item0->m_info.timestamp,1);
            }
        }
    }
}

void MainDialog::slot_downloadResume(bool flag)
{
    qDebug()<<"下载开始";
    int rows=ui->table_download->rowCount();
    //遍历表单
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_download->item(i,0);
        //看是否打钩
        if(item0->checkState()==Qt::Checked){
            QPushButton* button=(QPushButton*)ui->table_download->cellWidget(i,5);
            //看按钮的状态 切换文字 发送信号
            if(button->text()=="开始"){
                //信号 设置文件信息结构体暂停标志位
                button->setText("暂停");
                Q_EMIT SIG_setDownloadPause(item0->m_info.timestamp,0);
            }
        }
    }
}

void MainDialog::slot_insertUploadFile(FileInfo &info)  //插入到上传中
{
    //表格插入信息
    //列：文件 大小 时间 网速 进度 按钮
    //1、新增一列 获取当前行+1 设置行数
    int rows=ui->table_upload->rowCount();
    ui->table_upload->setRowCount(rows+1);
    //2、设置这一行的每一列控件（添加对象）
    MyTableWidgetItem* item0=new MyTableWidgetItem;
    item0->slot_setInfo(info);
    ui->table_upload->setItem(rows,0,item0);
    QTableWidgetItem* item1=new QTableWidgetItem(FileInfo::getSize(info.size));
    ui->table_upload->setItem(rows,1,item1);
    QTableWidgetItem* item2=new QTableWidgetItem(info.time);
    ui->table_upload->setItem(rows,2,item2);
    QTableWidgetItem* item3=new QTableWidgetItem("0KB/s");
    ui->table_upload->setItem(rows,3,item3);
    //进度条 — 占满单元格
    QProgressBar* progress=new QProgressBar;
    progress->setMaximum(info.size);
    progress->setMinimum(0);
    progress->setValue(0);
    progress->setTextVisible(false);
    progress->setStyleSheet(
        "QProgressBar{ border:1px solid #ccc; border-radius:3px; background:#f0f0f0; min-height:18px; }"
        "QProgressBar::chunk{ background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #4CAF50, stop:1 #66BB6A); border-radius:2px; }");
    progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    progress->setMinimumHeight(20);
    ui->table_upload->setCellWidget(rows,4,progress);
    //按钮
    QPushButton*button=new QPushButton;
    if(info.isPause==0)
        button->setText("暂停");
    else
        button->setText("开始");
    button->setMinimumWidth(50);
    //去掉焦点框：点击后不留颜色边框，只有按住时有按压效果
    button->setFocusPolicy(Qt::NoFocus);
    //连接按钮点击：切换暂停/恢复    int timestamp = info.timestamp;
    connect(button, &QPushButton::clicked, this, [this, timestamp, button](){
        if(button->text()=="暂停"){
            button->setText("开始");
            Q_EMIT SIG_setUploadPause(timestamp, 1);
        }else{
            button->setText("暂停");
            Q_EMIT SIG_setUploadPause(timestamp, 0);
        }
    });
    ui->table_upload->setCellWidget(rows,5,button);
}

void MainDialog::slot_insertUploadComplete(FileInfo &info)
{
    //列：文件 大小 时间 上传完成
    //1、新增一行 获取当前行+1 设置行数
    int rows=ui->table_complete->rowCount();
    ui->table_complete->setRowCount(rows+1);
    //2、设置这一行的每一列控件（添加对象）
    MyTableWidgetItem* item0=new MyTableWidgetItem;
    item0->slot_setInfo(info);
    ui->table_complete->setItem(rows,0,item0);
    QTableWidgetItem* item1=new QTableWidgetItem(FileInfo::getSize(info.size));
    ui->table_complete->setItem(rows,1,item1);
    QTableWidgetItem* item2=new QTableWidgetItem(info.time);
    ui->table_complete->setItem(rows,2,item2);
    QTableWidgetItem* item3=new QTableWidgetItem("上传完成");
    ui->table_complete->setItem(rows,3,item3);
}

void MainDialog::slot_insertDownloadFile(FileInfo &info)
{
    //表格插入信息
    //列：文件 大小 时间 网速 进度 按钮
    //1、新增一列 获取当前行+1 设置行数
    int rows=ui->table_download->rowCount();
    ui->table_download->setRowCount(rows+1);
    //2、设置这一行的每一列控件（添加对象）
    MyTableWidgetItem* item0=new MyTableWidgetItem;
    item0->slot_setInfo(info);
    ui->table_download->setItem(rows,0,item0);
    QTableWidgetItem* item1=new QTableWidgetItem(FileInfo::getSize(info.size));
    ui->table_download->setItem(rows,1,item1);
    QTableWidgetItem* item2=new QTableWidgetItem(info.time);
    ui->table_download->setItem(rows,2,item2);
    QTableWidgetItem* item3=new QTableWidgetItem("0KB/s");
    ui->table_download->setItem(rows,3,item3);
    //进度条 — 占满单元格
    QProgressBar* progress=new QProgressBar;
    progress->setMaximum(info.size);
    progress->setMinimum(0);
    progress->setValue(0);
    progress->setTextVisible(false);
    progress->setStyleSheet(
        "QProgressBar{ border:1px solid #ccc; border-radius:3px; background:#f0f0f0; min-height:18px; }"
        "QProgressBar::chunk{ background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #2196F3, stop:1 #42A5F5); border-radius:2px; }");
    progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    progress->setMinimumHeight(20);
    ui->table_download->setCellWidget(rows,4,progress);
    //按钮
    QPushButton*button=new QPushButton;
    if(info.isPause==0)
        button->setText("暂停");
    else
        button->setText("开始");
    button->setMinimumWidth(50);
    //去掉焦点框：点击后不留颜色边框，只有按住时有按压效果
    button->setFocusPolicy(Qt::NoFocus);
    //连接按钮点击：切换暂停/恢复    int timestamp = info.timestamp;
    connect(button, &QPushButton::clicked, this, [this, timestamp, button](){
        if(button->text()=="暂停"){
            button->setText("开始");
            Q_EMIT SIG_setDownloadPause(timestamp, 1);
        }else{
            button->setText("暂停");
            Q_EMIT SIG_setDownloadPause(timestamp, 0);
        }
    });
    ui->table_download->setCellWidget(rows,5,button);
}

void MainDialog::slot_insertDownloadComplete(FileInfo &info)
{
    //列：文件 大小 时间 按钮
    //1、新增一列 获取当前行+1 设置行数
    int rows=ui->table_complete->rowCount();
    ui->table_complete->setRowCount(rows+1);
    //2、设置这一行的每一列控件（添加对象）
    MyTableWidgetItem* item0=new MyTableWidgetItem;
    item0->slot_setInfo(info);
    ui->table_complete->setItem(rows,0,item0);
    QTableWidgetItem* item1=new QTableWidgetItem(FileInfo::getSize(info.size));
    ui->table_complete->setItem(rows,1,item1);
    QTableWidgetItem* item2=new QTableWidgetItem(info.time);
    ui->table_complete->setItem(rows,2,item2);

    QPushButton* button=new QPushButton;
    connect(button,SIGNAL(clicked(bool)),this,SLOT(slot_openPath(bool)));
    button->setIcon(QIcon(":/images/folder.png"));
    //设置扁平
    button->setFlat(true);
    //去掉焦点框：点击后不留颜色边框，只有按住时有按压效果
    button->setFocusPolicy(Qt::NoFocus);
    //tooltip提示
    button->setToolTip(info.absolutePath);
    connect(button,SIGNAL(clicked(bool)),this,SLOT(slot_openPath(bool)));
    ui->table_complete->setCellWidget(rows,3,button);
}

void MainDialog::slot_insertShareFileInfo(QString name, int size, QString time, int shareLink, QString password)
{
    //列：文件 大小 时间 分享码 密码
    int rows=ui->table_share->rowCount();
    ui->table_share->setRowCount(rows+1);
    ui->table_share->setItem(rows, 0, new QTableWidgetItem(name));
    ui->table_share->setItem(rows, 1, new QTableWidgetItem(FileInfo::getSize(size)));
    ui->table_share->setItem(rows, 2, new QTableWidgetItem(time));
    ui->table_share->setItem(rows, 3, new QTableWidgetItem(QString::number(shareLink)));
    ui->table_share->setItem(rows, 4, new QTableWidgetItem(password));
}

void MainDialog::slot_insertViewShareInfo(QString name, int size, QString time, int shareLink)
{
    int rows = ui->table_viewShare->rowCount();
    ui->table_viewShare->setRowCount(rows + 1);
    ui->table_viewShare->setItem(rows, 0, new QTableWidgetItem(name));
    ui->table_viewShare->setItem(rows, 1, new QTableWidgetItem(FileInfo::getSize(size)));
    ui->table_viewShare->setItem(rows, 2, new QTableWidgetItem(time));
    ui->table_viewShare->setItem(rows, 3, new QTableWidgetItem(QString::number(shareLink)));
}

#include<QProcess>
void MainDialog::slot_openPath(bool flag){
    QPushButton* button=(QPushButton*)QObject::sender();
    QString path=button->toolTip();
    ///转化成\\    path.replace('/','\\');
    qDebug()<<path;
    //如何打开文件夹
    //通过qt打开进程
    QProcess process;
    QStringList lst;
    lst<<QString("/select,")<<path;
    process.startDetached("explorer",lst);
}

void MainDialog::slot_deleteAllFileInfo()
{
    int rows=ui->table_file->rowCount();
    for(int i=rows-1;i>=0;i--)
        ui->table_file->removeRow(i);
}

void MainDialog::slot_deleteAllShareInfo()
{
    int rows=ui->table_share->rowCount();
    for(int i=rows-1;i>=0;i--)
        ui->table_share->removeRow(i);
}

void MainDialog::slot_updateUploadFileProgress(int timestamp, int pos)
{
    //遍历所有项 第0列
    int row=ui->table_upload->rowCount();
    for(int i=0;i<row;++i){
        //取到的每一个文件信息的时间戳 看是否一致
        MyTableWidgetItem*item0=(MyTableWidgetItem*)ui->table_upload->item(i,0);
        if(item0->m_info.timestamp==timestamp){
            //一致 更新进度
            QProgressBar* item4=(QProgressBar*)ui->table_upload->cellWidget(i,4);
            item0->m_info.pos=pos;
            item4->setValue(pos);//
            //看是否结束
            if(item4->value()>=item4->maximum()){
                //先拷贝文件信息，再删除该行：
                //removeRow 会销毁 item0 对象，原先删除后再访问 item0->m_info                FileInfo tmpInfo = item0->m_info;
                //是 删除这一项 添加到完成
                slot_deleteUploadFileByRow(i);
                slot_insertUploadComplete(tmpInfo);

                return;
            }
        }
    }
}

void MainDialog::slot_updateDownloadFileProgress(int timestamp, int pos)
{
    //遍历所有项 第0列
    int row=ui->table_download->rowCount();
    for(int i=0;i<row;++i){
        //取到的每一个文件信息的时间戳 看是否一致
        MyTableWidgetItem*item0=(MyTableWidgetItem*)ui->table_download->item(i,0);
        if(item0->m_info.timestamp==timestamp){
            //一致 更新进度
            QProgressBar* item4=(QProgressBar*)ui->table_download->cellWidget(i,4);
            pos = pos > item4->maximum() ? item4->maximum(): pos;
            item0->m_info.pos=pos;
            item4->setValue(pos);

            if (!m_pendingSharePreviewFiles.isEmpty()) {
                QString dlName = item0->m_info.name;
                int dlSize = item0->m_info.size;
                //遍历待预览文件，按文件名匹配                for (auto it = m_pendingSharePreviewFiles.begin();
                     it != m_pendingSharePreviewFiles.end(); ++it) {
                    if (it.value() == dlName) {
                        int pct = (dlSize > 0) ? (int)((qint64)pos * 100 / dlSize) : 100;
                        QString sizeStr = FileInfo::getSize(dlSize);
                        ui->lb_sharePreview->setPixmap(QPixmap());
                        ui->lb_sharePreview->setText(
                            QString("正在下载预览... %1%\n\n%2\n%3")
                                .arg(pct).arg(dlName).arg(sizeStr));
                        break;
                    }
                }
            }

            //看是否结束
            if(item4->value()>=item4->maximum()){
                //检查是否是待播放的视频文件                if(m_pendingVideoPaths.contains(item0->m_info.absolutePath)){
                    m_pendingVideoPaths.remove(item0->m_info.absolutePath);
                    //先拷贝文件信息，再删除该行：
                    //removeRow 会销毁 item0 对象，原先删除行后仍访问 item0->m_info                    FileInfo tmpInfo = item0->m_info;
                    //是 删除这一项 添加到完成                    slot_insertDownloadComplete(tmpInfo);
                    slot_deleteDownloadFileByRow(i);
                    //播放视频                    playVideoFile(tmpInfo.absolutePath);
                    return;
                }
                //是 删除这一项 添加到完成
                slot_insertDownloadComplete(item0->m_info);
                slot_deleteDownloadFileByRow(i);
                return;
            }
        }
    }
}

void MainDialog::slot_deleteUploadFileByRow(int row)
{
    ui->table_upload->removeRow(row);
}

void MainDialog::slot_deleteDownloadFileByRow(int row)
{
    ui->table_download->removeRow(row);
}

void MainDialog::slot_insertFileInfo(FileInfo &info)
{
    //列：文件 大小 时间
    //1、新增一行 获取当前行+1 设置行数
    int rows=ui->table_file->rowCount();
    ui->table_file->setRowCount(rows+1);
    //2、设置这一行的每一列控件（添加对象）
    MyTableWidgetItem* item0=new MyTableWidgetItem;
    item0->slot_setInfo(info);
    ui->table_file->setItem(rows,0,item0);

    QString strSize;
    if(info.type=="file")
        strSize=FileInfo::getSize(info.size);
    else
        strSize="";

    QTableWidgetItem* item1=new QTableWidgetItem(strSize);
    ui->table_file->setItem(rows,1,item1);
    QTableWidgetItem* item2=new QTableWidgetItem(info.time);
    ui->table_file->setItem(rows,2,item2);

}

void MainDialog::on_table_file_cellClicked(int row, int column)   //选中某一行
{
    //切换勾选和未勾选状态
    MyTableWidgetItem * item0=(MyTableWidgetItem*)ui->table_file->item(row,0);
    if(item0->checkState()==Qt::Checked){
        item0->setCheckState(Qt::Unchecked);
    }
    else{
        item0->setCheckState(Qt::Checked);
    }
}


void MainDialog::on_table_file_customContextMenuRequested(const QPoint &pos) //表格位置鼠标右键
{
    //弹出菜单
    m_menuFileInfo.exec(QCursor::pos());
}


void MainDialog::on_table_file_cellDoubleClicked(int row, int column)
{
    MyTableWidgetItem *item0=(MyTableWidgetItem*)ui->table_file->item(row,0); //先拿到双击的哪行的文件名字
    //判断是不是文件夹，是文件夹可以跳转，是文件考虑打开文件
    if(item0->m_info.type!="file"){
        QString dir=ui->lb_path->text()+item0->m_info.name+"/";  //是文件夹 路径拼接
        ui->lb_path->setText(dir);  //设置路径 lb_path->text
        Q_EMIT SIG_changeDir(dir);  //发送信号->更新当前的目录->刷新文件列表
    }else if(isVideoFile(item0->m_info.name)){
        QString dir2 = ui->lb_path->text();
        QString localPath = m_sysPath + dir2 + item0->m_info.name;
        QFileInfo fi(localPath);
        if(fi.exists() && fi.size() == item0->m_info.size && checkVideoPlayable(localPath)){
            playVideoFile(localPath);
        }else{
            if (fi.exists()) QFile::remove(localPath);
            //先下载再播放            m_pendingVideoPaths.insert(localPath);
            Q_EMIT SIG_downloadFile(item0->m_info.fileid, dir2);
        }
    }
}


void MainDialog::on_pb_prev_clicked()
{
    //获取目录
    QString path=ui->lb_path->text();
    //判断"/"结束
    if(path=="/") return;
    //先找到最右边的"/"，从它左边开始向右找"/"
    //left取多少个长度
    path=path.left(path.lastIndexOf("/"));
    //新的目录就是找到的"/"，以及左边的所有字符
    path=path.left(path.lastIndexOf("/")+1);
    qDebug()<<path;
    ui->lb_path->setText(path);
    //跳转路径
    Q_EMIT SIG_changeDir(path);
}


void MainDialog::on_table_upload_cellClicked(int row, int column)
{
    //切换勾选和未勾选状态
    MyTableWidgetItem * item0=(MyTableWidgetItem*)ui->table_upload->item(row,0);
    if(item0->checkState()==Qt::Checked){
        item0->setCheckState(Qt::Unchecked);
    }
    else{
        item0->setCheckState(Qt::Checked);
    }
}

void MainDialog::on_table_download_cellClicked(int row, int column)
{
    //切换勾选和未勾选状态
    MyTableWidgetItem * item0=(MyTableWidgetItem*)ui->table_download->item(row,0);
    if(item0->checkState()==Qt::Checked){
        item0->setCheckState(Qt::Unchecked);
    }
    else{
        item0->setCheckState(Qt::Checked);
    }
}

bool MainDialog::slot_getDownloadFileInfoByTimestamp(int timestamp,FileInfo& info)
{
    //遍历所有第0列
    int rows=ui->table_download->rowCount();
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_download->item(i,0);
        if(item0->m_info.timestamp==timestamp){
            info=item0->m_info;
            return true;
        }
    }
    return false;
}

bool MainDialog::slot_getUploadFileInfoByTimestamp(int timestamp,FileInfo& info)
{
    //遍历所有第0列
    int rows=ui->table_upload->rowCount();
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_upload->item(i,0);
        if(item0->m_info.timestamp==timestamp){
            info=item0->m_info;
            return true;
        }
    }
    return false;
}

//---- 视频播放相关 ----
bool MainDialog::isVideoFile(const QString& name)
{
    QString lower = name.toLower();
    return lower.endsWith(".mp4") || lower.endsWith(".flv") ||
           lower.endsWith(".rmvb") || lower.endsWith(".avi") ||
           lower.endsWith(".mkv") || lower.endsWith(".mov") ||
           lower.endsWith(".wmv") || lower.endsWith(".webm") ||
           lower.endsWith(".ts") || lower.endsWith(".m4v") ||
           lower.endsWith(".3gp");
}

void MainDialog::slot_playVideo(bool flag)
{
    qDebug()<<__func__;
    int rows=ui->table_file->rowCount();
    for(int i=0;i<rows;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_file->item(i,0);
        if(item0->checkState()==Qt::Checked && item0->m_info.type=="file"){
            if(isVideoFile(item0->m_info.name)){
                QString dir = ui->lb_path->text();
                QString localPath = m_sysPath + dir + item0->m_info.name;
                QFileInfo fi(localPath);
                //解不出就删掉重新下载                if(fi.exists() && fi.size() == item0->m_info.size && checkVideoPlayable(localPath)){
                    playVideoFile(localPath);
                }else{
                    if (fi.exists()) QFile::remove(localPath);
                    m_pendingVideoPaths.insert(localPath);
                    Q_EMIT SIG_downloadFile(item0->m_info.fileid, dir);
                }
            }
        }
    }
}

void MainDialog::slot_onVideoReady(QString localPath)
{
    playVideoFile(localPath);
}

//校验本地视频能否解出第一帧：
//只做 avformat_open_input 校验无法发现内容损坏，播放会黑屏。//这里用解码器实际解出一帧来确认文件可用。bool MainDialog::checkVideoPlayable(const QString& localPath)
{
    AVFormatContext *fmt = nullptr;
    QByteArray path = QFile::encodeName(localPath);
    if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    int vIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            vIdx = (int)i;
            break;
        }
    }
    if (vIdx < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecContext *ctx = fmt->streams[vIdx]->codec;
    AVCodec *codec = avcodec_find_decoder(ctx->codec_id);
    if (!codec || avcodec_open2(ctx, codec, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    AVFrame *frame = av_frame_alloc();
    AVPacket pkt;
    av_init_packet(&pkt);
    bool ok = false;
    int tries = 300;  // 最多尝试 300 个包，防止在坏文件上无限循环
    while (!ok && tries-- > 0 && av_read_frame(fmt, &pkt) >= 0) {
        if (pkt.stream_index == vIdx) {
            int got = 0;
            avcodec_decode_video2(ctx, frame, &got, &pkt);
            if (got)
                ok = true;
        }
        av_packet_unref(&pkt);
    }
    av_frame_free(&frame);
    avcodec_close(ctx);
    avformat_close_input(&fmt);
    return ok;
}

void MainDialog::playVideoFile(const QString& localPath)
{
    QFileInfo fi(localPath);
    if(!fi.exists() || fi.size() == 0){
        qWarning() << "Video file is missing or empty:" << localPath;
        QMessageBox::warning(this, "播放失败", "视频文件不完整，请重新下载");
        return;
    }

    AVFormatContext *formatCtx = nullptr;
    QByteArray encodedPath = QFile::encodeName(localPath);
    if (avformat_open_input(&formatCtx, encodedPath.constData(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(formatCtx, nullptr) < 0) {
        if (formatCtx) avformat_close_input(&formatCtx);
        qWarning() << "Invalid video container:" << localPath;
        QMessageBox::warning(this, "播放失败", "视频文件损坏或下载不完整，请删除后重新下载");
        return;
    }
    avformat_close_input(&formatCtx);

    if(m_playerDialog->isVisible()){
        m_playerDialog->hide();
    }
    m_playerDialog->show();
    m_playerDialog->on_pb_start_clicked_with_path(localPath);
}

void MainDialog::slot_searchFile(bool flag)
{
    QString keyword = QInputDialog::getText(this,"搜索文件","输入关键词");
    if (keyword.trimmed().isEmpty()) return;
    Q_EMIT SIG_searchFile(keyword.trimmed());
}



void MainDialog::on_pb_searchBack_clicked()
{
    ui->sw_page->setCurrentIndex(0);  // 切回文件列表
}

void MainDialog::slot_showSearchResults(const char *buf, int nlen)
{
    STRU_SEARCH_FILE_RS* rs = (STRU_SEARCH_FILE_RS*)buf;
    int count = rs->count;

    //切到搜索分页    ui->sw_page->setCurrentIndex(3);

    QTableWidget* table = ui->table_search;
    int oldRows = table->rowCount();
    for (int i = oldRows - 1; i >= 0; i--)
        table->removeRow(i);

    //填充搜索结果    for (int i = 0; i < count; ++i) {
        int row = table->rowCount();
        table->setRowCount(row + 1);

        QTableWidgetItem* item0 = new QTableWidgetItem(rs->items[i].name);
        item0->setData(Qt::UserRole, rs->items[i].fileid);
        table->setItem(row, 0, item0);

        QString sizeStr = (strcmp(rs->items[i].fileType, "file") == 0)
                ? FileInfo::getSize(rs->items[i].size) : "";
        QTableWidgetItem* item1 = new QTableWidgetItem(sizeStr);
        table->setItem(row, 1, item1);

        QTableWidgetItem* item2 = new QTableWidgetItem(rs->items[i].time);
        table->setItem(row, 2, item2);

        QTableWidgetItem* item3 = new QTableWidgetItem(rs->items[i].dir);
        table->setItem(row, 3, item3);
    }
}

//========== 收藏功能 ==========
void MainDialog::on_pb_store_clicked()
{
    //切换到收藏页面    ui->sw_page->setCurrentIndex(4);
    //请求刷新收藏列表    Q_EMIT SIG_getFavorites();
}

void MainDialog::slot_favoriteFile(bool flag)
{
    qDebug()<<__func__;
    QVector<int> array;
    int count=ui->table_file->rowCount();
    //遍历所有项    for(int i=0;i<count;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_file->item(i,0);
        //看是否打钩        if(item0->checkState()==Qt::Checked)
            array.push_back(item0->m_info.fileid);
    }
    if(array.isEmpty()){
        QMessageBox::about(this,"提示","请选择要收藏的文件");
        return;
    }
    //发送信号（添加收藏）    Q_EMIT SIG_favoriteFile(array, ui->lb_path->text(), true);
}

void MainDialog::slot_cancelFavorite(bool flag)
{
    qDebug()<<__func__;
    QVector<int> array;
    QString dir = "/";
    int count = ui->table_favorite->rowCount();
    //遍历收藏列表中被勾选的项    for (int i = count - 1; i >= 0; --i) {
        QTableWidgetItem* item0 = ui->table_favorite->item(i, 0);
        if (item0 && item0->checkState() == Qt::Checked) {
            int fileid = item0->data(Qt::UserRole).toInt();
            array.push_back(fileid);
            dir = item0->data(Qt::UserRole + 1).toString();
            //乐观更新：立即从UI移除            ui->table_favorite->removeRow(i);
        }
    }
    if (array.isEmpty()) {
        QMessageBox::about(this, "提示", "请勾选要取消收藏的文件");
        return;
    }
    //发送信号（取消收藏）    Q_EMIT SIG_favoriteFile(array, dir, false);
}

void MainDialog::slot_insertFavoriteInfo(int fileid, QString name, QString dir, int size, QString time, QString type)
{
    int rows = ui->table_favorite->rowCount();
    ui->table_favorite->setRowCount(rows + 1);

    QTableWidgetItem* item0 = new QTableWidgetItem(name);
    item0->setData(Qt::UserRole, fileid);
    item0->setData(Qt::UserRole + 1, dir);
    item0->setCheckState(Qt::Unchecked);
    if (type == "file")
        item0->setIcon(QIcon(":/NetDisk/ui_assets/images/file.png"));
    else
        item0->setIcon(QIcon(":/NetDisk/ui_assets/images/folder.png"));
    ui->table_favorite->setItem(rows, 0, item0);

    QString strSize = (type == "file") ? FileInfo::getSize(size) : "";
    QTableWidgetItem* item1 = new QTableWidgetItem(strSize);
    ui->table_favorite->setItem(rows, 1, item1);

    QTableWidgetItem* item2 = new QTableWidgetItem(time);
    ui->table_favorite->setItem(rows, 2, item2);

    QTableWidgetItem* item3 = new QTableWidgetItem(dir);
    ui->table_favorite->setItem(rows, 3, item3);
}

void MainDialog::slot_deleteAllFavoriteInfo()
{
    int rows = ui->table_favorite->rowCount();
    for (int i = rows - 1; i >= 0; i--)
        ui->table_favorite->removeRow(i);
}

void MainDialog::slot_showFavorites(const char *buf, int nlen)
{
    STRU_GET_FAVORITES_RS* rs = (STRU_GET_FAVORITES_RS*)buf;
    int count = rs->count;

    slot_deleteAllFavoriteInfo();

    for (int i = 0; i < count; ++i) {
        slot_insertFavoriteInfo(
            rs->items[i].fileid,
            QString::fromStdString(rs->items[i].name),
            QString::fromStdString(rs->items[i].dir),
            rs->items[i].size,
            QString::fromStdString(rs->items[i].time),
            QString::fromStdString(rs->items[i].fileType)
        );
    }
}

//========== 回收站功能 ==========
void MainDialog::on_pb_bin_clicked()
{
    //切换到回收站页面    ui->sw_page->setCurrentIndex(5);
    //请求刷新回收站列表    Q_EMIT SIG_getRecycle();
}

void MainDialog::slot_recycleFile(bool flag)
{
    qDebug()<<__func__;
    QVector<int> array;
    int count=ui->table_file->rowCount();
    //遍历所有项    for(int i=0;i<count;++i){
        MyTableWidgetItem* item0=(MyTableWidgetItem*)ui->table_file->item(i,0);
        //看是否打钩        if(item0->checkState()==Qt::Checked)
            array.push_back(item0->m_info.fileid);
    }
    if(array.isEmpty()){
        QMessageBox::about(this,"提示","请选择要加入回收站的文件");
        return;
    }
    if(QMessageBox::question(this,"确认","确定将选中文件移入回收站？") != QMessageBox::Yes)
        return;
    //发送信号（UI刷新由服务端回复驱动，不做乐观删除）    Q_EMIT SIG_recycleFile(array, ui->lb_path->text());
}

void MainDialog::on_pb_restoreFile_clicked()
{
    QVector<int> array;
    int count = ui->table_recycle->rowCount();
    for (int i = 0; i < count; ++i) {
        QTableWidgetItem* item0 = ui->table_recycle->item(i, 0);
        if (item0 && item0->checkState() == Qt::Checked) {
            int fileid = item0->data(Qt::UserRole).toInt();
            array.push_back(fileid);
        }
    }
    if (array.isEmpty()) {
        QMessageBox::about(this, "提示", "请选择要恢复的文件");
        return;
    }
    Q_EMIT SIG_restoreFile(array);
}

void MainDialog::on_pb_deleteForever_clicked()
{
    QVector<int> array;
    int count = ui->table_recycle->rowCount();
    for (int i = 0; i < count; ++i) {
        QTableWidgetItem* item0 = ui->table_recycle->item(i, 0);
        if (item0 && item0->checkState() == Qt::Checked) {
            int fileid = item0->data(Qt::UserRole).toInt();
            array.push_back(fileid);
        }
    }
    if (array.isEmpty()) {
        QMessageBox::about(this, "提示", "请选择要彻底删除的文件");
        return;
    }
    if (QMessageBox::question(this, "确认", "彻底删除后无法恢复，确定删除？") != QMessageBox::Yes)
        return;
    Q_EMIT SIG_deleteForever(array);
}

void MainDialog::slot_insertRecycleInfo(int fileid, QString name, QString dir, int size, QString time, QString type)
{
    int rows = ui->table_recycle->rowCount();
    ui->table_recycle->setRowCount(rows + 1);

    QTableWidgetItem* item0 = new QTableWidgetItem(name);
    item0->setData(Qt::UserRole, fileid);
    item0->setData(Qt::UserRole + 1, dir);
    item0->setCheckState(Qt::Unchecked);
    if (type == "file")
        item0->setIcon(QIcon(":/NetDisk/ui_assets/images/file.png"));
    else
        item0->setIcon(QIcon(":/NetDisk/ui_assets/images/folder.png"));
    ui->table_recycle->setItem(rows, 0, item0);

    QString strSize = (type == "file") ? FileInfo::getSize(size) : "";
    QTableWidgetItem* item1 = new QTableWidgetItem(strSize);
    ui->table_recycle->setItem(rows, 1, item1);

    //删除时间 + 剩余天数（回收站保留30天）    QString strTime = time;
    QDateTime delTime = QDateTime::fromString(time, "yyyy-MM-dd hh:mm:ss");
    if (delTime.isValid()) {
        int remain = 30 - delTime.daysTo(QDateTime::currentDateTime());
        if (remain < 0) remain = 0;
        strTime = time + QString("（剩余 %1 天）").arg(remain);
    }
    QTableWidgetItem* item2 = new QTableWidgetItem(strTime);
    ui->table_recycle->setItem(rows, 2, item2);

    QTableWidgetItem* item3 = new QTableWidgetItem(dir);
    ui->table_recycle->setItem(rows, 3, item3);
}

void MainDialog::slot_deleteAllRecycleInfo()
{
    int rows = ui->table_recycle->rowCount();
    for (int i = rows - 1; i >= 0; i--)
        ui->table_recycle->removeRow(i);
}

void MainDialog::slot_showRecycle(const char *buf, int nlen)
{
    STRU_GET_RECYCLE_RS* rs = (STRU_GET_RECYCLE_RS*)buf;
    int count = rs->count;

    slot_deleteAllRecycleInfo();

    for (int i = 0; i < count; ++i) {
        slot_insertRecycleInfo(
            rs->items[i].fileid,
            QString::fromStdString(rs->items[i].name),
            QString::fromStdString(rs->items[i].dir),
            rs->items[i].size,
            QString::fromStdString(rs->items[i].deleteTime),
            QString::fromStdString(rs->items[i].fileType)
        );
    }
}

void MainDialog::on_table_favorite_cellClicked(int row, int column)
{
    QTableWidgetItem* item0 = ui->table_favorite->item(row, 0);
    if (!item0) return;
    if (item0->checkState() == Qt::Checked)
        item0->setCheckState(Qt::Unchecked);
    else
        item0->setCheckState(Qt::Checked);
}

void MainDialog::on_table_recycle_cellClicked(int row, int column)
{
    QTableWidgetItem* item0 = ui->table_recycle->item(row, 0);
    if (!item0) return;
    if (item0->checkState() == Qt::Checked)
        item0->setCheckState(Qt::Unchecked);
    else
        item0->setCheckState(Qt::Checked);
}

void MainDialog::on_pb_viewShare_clicked()
{
    ui->sw_page->setCurrentIndex(2);
    ui->tw_share->setCurrentIndex(1);
    slot_refreshObtainedShares();  // 自动加载已获取的分享列表
//============================================================//查看分享 — 浏览目录//============================================================
//返回上级按钮}
void MainDialog::on_pb_shareBack_clicked()
{
    //如果正在浏览某个分享的根目录 → 回到多分享合并视图    if (m_browseShareCurDir == "/" || m_browseShareCurDir.isEmpty()) {
        slot_refreshObtainedShares();
        return;
    }
    if (m_browseShareCode == 0) return;
    //去掉最后一级目录    QString dir = m_browseShareCurDir;
    if (dir.endsWith('/')) dir.chop(1);
    int pos = dir.lastIndexOf('/');
    if (pos < 0) dir = "/";
    else         dir = dir.left(pos + 1);
    Q_EMIT SIG_browseShare(m_browseShareCode, dir, m_browseSharePwd);
}

//清空上传列表void MainDialog::on_pb_clearUpload_clicked()
{
    //确认对话框    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认", "确定要清空所有上传记录吗？\n（正在上传的文件不会被取消）",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        //清空上传列表UI        int rows = ui->table_upload->rowCount();
        for (int i = rows - 1; i >= 0; --i) {
            ui->table_upload->removeRow(i);
        }
        //同步清空SQLite中的上传任务        Q_EMIT SIG_clearUploadTasks();
    }
}

//清空"已完成"列表（只清界面，不影响任何任务）void MainDialog::on_pb_clearComplete_clicked()
{
    if(ui->table_complete->rowCount()==0) return;
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认", "确定要清空所有已完成记录吗？",
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        ui->table_complete->setRowCount(0);
    }
}

//删除"已完成"列表选中的行（右键菜单触发，支持多选）void MainDialog::slot_deleteCompleteSelected()
{
    //收集选中的行号并去重    QSet<int> rowSet;
    for(QTableWidgetItem* item : ui->table_complete->selectedItems()){
        rowSet.insert(item->row());
    }
    QList<int> rows = rowSet.toList();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for(int r : rows){
        ui->table_complete->removeRow(r);
    }
}

//===== 已获取分享列表管理 =====
void MainDialog::slot_refreshObtainedShares()
{
    ui->table_viewShare->setRowCount(0);
    ui->lb_sharePreview->setPixmap(QPixmap());
    ui->lb_sharePreview->setText("正在加载...");
    ui->lb_sharePath->setText("已获取的分享文件");
    m_browseQueue.clear();
    m_isLoadingShares = true;

    //从SQLite加载所有已保存的分享码    Q_EMIT SIG_loadObtainedShares();
}

void MainDialog::slot_onObtainedSharesLoaded(QList<QPair<int,QString>> list)
{
    if (list.isEmpty()) {
        m_isLoadingShares = false;
        ui->lb_sharePath->setText("已获取的分享文件（空）");
        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText("暂无已获取的分享\n\n在主页面右键文件列表 → 获取分享");
        return;
    }

    ui->table_viewShare->setRowCount(0);
    m_sharePasswords.clear();
    //所有分享码入队，逐个发送 BrowseShareRq    for (auto& p : list) {
        m_browseQueue.enqueue(p);
        m_sharePasswords[p.first] = p.second;  // 持久保存密码
    }

    auto first = m_browseQueue.dequeue();
    m_currentLoadingCode = first.first;
    m_currentLoadingPwd  = first.second;
    Q_EMIT SIG_browseShare(first.first, "/", first.second);
}

//服务端回包 → 填充 table_viewShare（双模式：多分享合并 / 单分享浏览）void MainDialog::slot_showBrowseShareResult(const char* buf, int nlen)
{
    STRU_BROWSE_SHARE_RS* rs = (STRU_BROWSE_SHARE_RS*)buf;

    //=== 多分享加载模式：静默处理 ===    if (m_isLoadingShares) {
        if (rs->result != browse_share_success) {
            //某个分享失效，跳过，继续加载下一个            goto nextInQueue;
        }

        //追加 items 到表格（不清空），每个 item 记住归属的 shareCode        for (int i = 0; i < rs->count; ++i) {
            int row = ui->table_viewShare->rowCount();
            ui->table_viewShare->setRowCount(row + 1);

            QString name = QString::fromUtf8(rs->items[i].name);
            QString type = QString::fromUtf8(rs->items[i].fileType);
            QString size = (type == "file") ? FileInfo::getSize(rs->items[i].size) : "--";

            QTableWidgetItem* it0 = new QTableWidgetItem(name);
            it0->setData(Qt::UserRole, type);
            it0->setData(Qt::UserRole + 1, rs->items[i].fileid);
            it0->setData(Qt::UserRole + 2, m_currentLoadingCode);  // 归属分享码
            ui->table_viewShare->setItem(row, 0, it0);
            ui->table_viewShare->setItem(row, 1, new QTableWidgetItem(size));
            ui->table_viewShare->setItem(row, 2, new QTableWidgetItem(type == "file" ? "文件" : "文件夹"));
        }

nextInQueue:
        //出队下一个分享，继续加载        if (!m_browseQueue.isEmpty()) {
            auto next = m_browseQueue.dequeue();
            m_currentLoadingCode = next.first;
            m_currentLoadingPwd  = next.second;
            Q_EMIT SIG_browseShare(next.first, "/", next.second);
        } else {
            m_isLoadingShares = false;
            int total = ui->table_viewShare->rowCount();
            ui->lb_sharePath->setText(QString("已获取的分享文件（共 %1 项）").arg(total));
            ui->lb_sharePreview->setText("👈 点击左侧文件名查看预览\n\n支持图片、视频、文本文件");
            if (total == 0)
                ui->lb_sharePreview->setText("暂无文件\n\n请确认分享码是否仍然有效");
        }
        return;
    }

    //=== 单分享浏览模式（文件夹导航） ===    if (rs->result == browse_share_invalid_link) {
        QMessageBox::about(this, "提示", "分享链接无效");
        return;
    }
    if (rs->result == browse_share_wrong_password) {
        QMessageBox::about(this, "提示", "分享密码错误");
        return;
    }

    m_browseShareCurDir = QString::fromStdString(rs->curDir);
    QString displayPath = m_browseShareCurDir.isEmpty() ? "/" : m_browseShareCurDir;
    ui->lb_sharePath->setText("当前路径: " + displayPath);

    ui->table_viewShare->setRowCount(0);
    ui->lb_sharePreview->setText("👈 点击左侧文件名查看预览\n\n支持图片、视频、文本文件");
    ui->lb_sharePreview->setPixmap(QPixmap());

    for (int i = 0; i < rs->count; ++i) {
        int row = ui->table_viewShare->rowCount();
        ui->table_viewShare->setRowCount(row + 1);

        QString name = QString::fromUtf8(rs->items[i].name);
        QString type = QString::fromUtf8(rs->items[i].fileType);
        QString size = (type == "file") ? FileInfo::getSize(rs->items[i].size) : "--";

        QTableWidgetItem* it0 = new QTableWidgetItem(name);
        it0->setData(Qt::UserRole, type);
        it0->setData(Qt::UserRole + 1, rs->items[i].fileid);
        it0->setData(Qt::UserRole + 2, m_browseShareCode);  // 当前浏览的分享码
        ui->table_viewShare->setItem(row, 0, it0);
        ui->table_viewShare->setItem(row, 1, new QTableWidgetItem(size));
        ui->table_viewShare->setItem(row, 2, new QTableWidgetItem(type == "file" ? "文件" : "文件夹"));
    }
}

//双击 table_viewShare：文件夹进入下级，文件显示预览void MainDialog::slot_browseShareEnterFolder(int row, int col)
{
    QTableWidgetItem* it = ui->table_viewShare->item(row, 0);
    if (!it) return;
    QString name = it->text();
    QString type = it->data(Qt::UserRole).toString();

    if (type != "file") {
        //文件夹：使用 item 自己的 shareCode 导航        int shareCode = it->data(Qt::UserRole + 2).toInt();
        if (shareCode == 0) shareCode = m_browseShareCode;  // 回退方案
        m_browseShareCode = shareCode;
        m_isLoadingShares = false;  // 退出多分享模式

        QString newDir = m_browseShareCurDir;
        if (newDir.isEmpty()) newDir = "/";
        if (!newDir.endsWith('/')) newDir += '/';
        newDir += name + '/';
        //从密码映射中查找密码        QString pwd = m_sharePasswords.value(shareCode, m_browseSharePwd);
        m_browseSharePwd = pwd;
        Q_EMIT SIG_browseShare(shareCode, newDir, pwd);
    } else {
        showSharePreview(name, type, "");
    }
}

//单击 table_viewShare：文件显示预览，文件夹显示文件夹图标void MainDialog::on_table_viewShare_cellClicked(int row, int col)
{
    QTableWidgetItem* it = ui->table_viewShare->item(row, 0);
    if (!it) return;
    QString name = it->text();
    QString type = it->data(Qt::UserRole).toString();
    int fileid = it->data(Qt::UserRole + 1).toInt();

    //如果是图片、视频或文本文件，触发下载    if (type == "file" && (isImageFile(name) || isVideoFile(name) || isTextFile(name))) {
        //检查文件大小：0字节文件跳过下载        QTableWidgetItem* sizeItem = ui->table_viewShare->item(row, 1);
        QString sizeStr = sizeItem ? sizeItem->text() : "";
        if (sizeStr == "0.00KB") {
            ui->lb_sharePreview->setPixmap(QPixmap());
            ui->lb_sharePreview->setText(QString("(空文件)\n%1\n\n文件大小为 0 字节，无法预览").arg(name));
            ui->lb_sharePreview->setAlignment(Qt::AlignCenter);
            return;
        }

        int shareCode = it->data(Qt::UserRole + 2).toInt();
        if (shareCode == 0) shareCode = m_browseShareCode;
        QString localPath = m_sysPath + "/" + name;
        QFileInfo localFi(localPath);
        if (localFi.exists() && localFi.size() > 0) {
            //本地已有完整文件，直接预览，不走下载流程            qDebug() << "【预览】本地已有文件，直接预览:" << localPath;
            ui->lb_sharePreview->setAlignment(Qt::AlignCenter);
            if (isImageFile(name)) {
                QPixmap pixmap(localPath);
                QSize previewSize = ui->lb_sharePreview->size();
                if (previewSize.width() < 10) previewSize = QSize(200, 300);
                ui->lb_sharePreview->setPixmap(
                    pixmap.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else if (isVideoFile(name)) {
                showVideoThumbnail(localPath, name);
            } else if (isTextFile(name)) {
                showTextPreview(localPath, name);
            }
            return;
        }
        if (m_pendingSharePreviewFiles.contains(fileid)) {
            return;
        }
        QString pwd = m_sharePasswords.value(shareCode, m_browseSharePwd);
        m_pendingSharePreviewFiles[fileid] = name;
        Q_EMIT SIG_downloadShareFile(shareCode, fileid, pwd);
        //显示加载提示（下载进度由 slot_updateDownloadFileProgress 实时更新）        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText(QString("正在下载预览... 0%%\n\n%1\n%2").arg(name).arg(sizeStr));
    } else {
        showSharePreview(name, type, "");
    }
}

//预览核心：根据类型渲染右侧 lb_sharePreviewvoid MainDialog::showSharePreview(const QString& name, const QString& type,
                                   const QString& /*localPath*/)
{
    QSize previewSize = ui->lb_sharePreview->size();
    if (previewSize.width() < 10) previewSize = QSize(200, 300);

    if (type != "file") {
        //文件夹：显示文件夹图标和名称        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText("📁\n\n" + name + "\n\n双击进入文件夹");
        return;
    }

    QString ext = QFileInfo(name).suffix().toUpper();

    if (isImageFile(name)) {
        //图片：显示图片图标        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText("🖼️\n\n" + name + "\n\n图片文件 (" + ext + ")\n\n点击文件名查看图片");
        return;
    }

    if (isTextFile(name)) {
        //文本文件：显示文本图标        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText("📝\n\n" + name + "\n\n文本文件 (" + ext + ")\n\n点击文件名可下载并查看内容");
        return;
    }

    if (isVideoFile(name)) {
        //视频：显示视频图标        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText("🎬\n\n" + name + "\n\n视频文件 (" + ext + ")\n\n点击查看视频缩略图");
        return;
    }

    //其他文件类型：显示扩展名图标    ui->lb_sharePreview->setPixmap(QPixmap());
    QString displayText = "📄\n\n" + name + "\n\n";
    if (!ext.isEmpty()) {
        displayText += ext + " 文件";
    } else {
        displayText += "未知类型文件";
    }
    ui->lb_sharePreview->setText(displayText);
}

bool MainDialog::isTextFile(const QString& name)
{
    QString lower = name.toLower();
    return lower.endsWith(".txt")  || lower.endsWith(".log")  ||
           lower.endsWith(".md")   || lower.endsWith(".csv")  ||
           lower.endsWith(".xml")  || lower.endsWith(".json") ||
           lower.endsWith(".html") || lower.endsWith(".htm")  ||
           lower.endsWith(".css")  || lower.endsWith(".js")   ||
           lower.endsWith(".c")    || lower.endsWith(".cpp")  ||
           lower.endsWith(".h")    || lower.endsWith(".hpp")  ||
           lower.endsWith(".py")   || lower.endsWith(".java") ||
           lower.endsWith(".ini")  || lower.endsWith(".cfg")  ||
           lower.endsWith(".yaml") || lower.endsWith(".yml")  ||
           lower.endsWith(".sh")   || lower.endsWith(".bat")  ||
           lower.endsWith(".sql")  || lower.endsWith(".conf") ||
           lower.endsWith(".properties") || lower.endsWith(".toml") ||
           lower.endsWith(".tex")  || lower.endsWith(".rst")  ||
           lower.endsWith(".php")  || lower.endsWith(".rb")   ||
           lower.endsWith(".go")   || lower.endsWith(".rs")   ||
           lower.endsWith(".swift");
}

bool MainDialog::isImageFile(const QString& name)
{
    QString lower = name.toLower();
    return lower.endsWith(".jpg")  || lower.endsWith(".jpeg") ||
           lower.endsWith(".png")  || lower.endsWith(".bmp")  ||
           lower.endsWith(".gif")  || lower.endsWith(".webp") ||
           lower.endsWith(".tiff") || lower.endsWith(".ico");
}

//文本文件预览：读取前4KB内容显示void MainDialog::showTextPreview(const QString& localPath, const QString& name)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText(QString("预览失败\n无法打开文件\n%1").arg(name));
        ui->lb_sharePreview->setAlignment(Qt::AlignCenter);
        return;
    }

    //读取前4KB    QByteArray data = file.read(4096);
    file.close();

    if (data.isEmpty()) {
        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText(QString("(空文件)\n%1").arg(name));
        ui->lb_sharePreview->setAlignment(Qt::AlignCenter);
        return;
    }

    //尝试UTF-8解码，失败则尝试GBK/GB18030    QString text = QString::fromUtf8(data);
    if (text.toUtf8() != data) {
        //不是有效UTF-8，尝试GB18030（兼容GBK/GB2312）        QTextCodec *codec = QTextCodec::codecForName("GB18030");
        if (codec) {
            text = codec->toUnicode(data);
        } else {
            text = QString::fromLocal8Bit(data);
        }
    }

    //截断过长内容    if (text.length() > 2000) {
        text = text.left(2000) + "\n\n... (内容过长，已截断)";
    }

    ui->lb_sharePreview->setPixmap(QPixmap());
    ui->lb_sharePreview->setText(text);
    ui->lb_sharePreview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui->lb_sharePreview->setWordWrap(true);
}

//视频缩略图：使用FFmpeg提取第一帧void MainDialog::showVideoThumbnail(const QString& localPath, const QString& name)
{
    //极小文件（<1KB）或空文件：不调用FFmpeg，直接显示提示    QFileInfo fi(localPath);
    if (!fi.exists() || fi.size() < 1024) {
        ui->lb_sharePreview->setPixmap(QPixmap());
        if (fi.size() == 0)
            ui->lb_sharePreview->setText(QString("(空文件)\n%1\n\n文件大小为 0 字节，无法预览").arg(name));
        else
            ui->lb_sharePreview->setText(QString("文件太小，无法预览\n%1\n\n大小: %2\n\n请等待完整下载后重试").arg(name).arg(FileInfo::getSize(fi.size())));
        ui->lb_sharePreview->setAlignment(Qt::AlignCenter);
        return;
    }

    //路径转换为本地8位编码（FFmpeg在Windows上使用ANSI）    QByteArray pathBytes = localPath.toLocal8Bit();
    const char* filePath = pathBytes.constData();

    //注册所有编解码器    av_register_all();

    AVFormatContext* pFormatCtx = avformat_alloc_context();
    if (!pFormatCtx) {
        ui->lb_sharePreview->setText(QString("预览失败\n内存不足\n%1").arg(name));
        return;
    }

    //打开视频文件    if (avformat_open_input(&pFormatCtx, filePath, nullptr, nullptr) != 0) {
        avformat_free_context(pFormatCtx);
        ui->lb_sharePreview->setText(QString("预览失败\n无法打开视频\n%1").arg(name));
        return;
    }

    //获取流信息    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        avformat_close_input(&pFormatCtx);
        ui->lb_sharePreview->setText(QString("预览失败\n无法读取视频信息\n%1").arg(name));
        return;
    }

    //查找视频流    int videoStream = -1;
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1) {
        avformat_close_input(&pFormatCtx);
        ui->lb_sharePreview->setText(QString("预览失败\n未找到视频流\n%1").arg(name));
        return;
    }

    //打开解码器    AVCodecContext* pCodecCtx = pFormatCtx->streams[videoStream]->codec;
    AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (!pCodec || avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        avformat_close_input(&pFormatCtx);
        ui->lb_sharePreview->setText(QString("预览失败\n无法打开解码器\n%1").arg(name));
        return;
    }

    //分配帧    AVFrame* pFrame = av_frame_alloc();
    AVFrame* pFrameRGB = av_frame_alloc();
    if (!pFrame || !pFrameRGB) {
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFormatCtx);
        if (pFrame) av_frame_free(&pFrame);
        if (pFrameRGB) av_frame_free(&pFrameRGB);
        return;
    }

    //计算RGB缓冲区大小并分配    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
                                             pCodecCtx->width, pCodecCtx->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes);
    if (!buffer) {
        av_frame_free(&pFrame);
        av_frame_free(&pFrameRGB);
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFormatCtx);
        return;
    }
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize,
                         buffer, AV_PIX_FMT_RGB32,
                         pCodecCtx->width, pCodecCtx->height, 1);

    //创建SWS缩放上下文（YUV → RGB）    SwsContext* swsCtx = sws_getContext(
        pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB32,
        SWS_BICUBIC, nullptr, nullptr, nullptr);

    if (!swsCtx) {
        av_free(buffer);
        av_frame_free(&pFrame);
        av_frame_free(&pFrameRGB);
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFormatCtx);
        return;
    }

    //读取数据包直到解码出第一帧视频（最多尝试500个包，防止损坏文件无限循环）    AVPacket packet;
    int frameFinished = 0;
    bool gotFrame = false;
    int maxPackets = 500;  // 安全限制：防止在损坏/不完整文件上无限循环

    while (av_read_frame(pFormatCtx, &packet) >= 0 && !gotFrame && maxPackets-- > 0) {
        if (packet.stream_index == videoStream) {
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            if (frameFinished) {
                //YUV → RGB转换                sws_scale(swsCtx,
                          (uint8_t const* const*)pFrame->data,
                          pFrame->linesize, 0,
                          pCodecCtx->height,
                          pFrameRGB->data,
                          pFrameRGB->linesize);

                //创建QImage（深拷贝，防止buffer释放后失效）                QImage img(pFrameRGB->data[0],
                           pCodecCtx->width, pCodecCtx->height,
                           pFrameRGB->linesize[0],
                           QImage::Format_RGB32);
                QImage copy = img.copy();

                QSize previewSize = ui->lb_sharePreview->size();
                if (previewSize.width() < 10) previewSize = QSize(200, 300);

                ui->lb_sharePreview->setPixmap(
                    QPixmap::fromImage(copy).scaled(
                        previewSize, Qt::KeepAspectRatio,
                        Qt::SmoothTransformation));
                gotFrame = true;
            }
        }
        av_packet_unref(&packet);
    }

    if (!gotFrame) {
        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText(QString("预览失败\n无法解码视频帧\n%1").arg(name));
    }

    //清理资源    sws_freeContext(swsCtx);
    av_free(buffer);
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
}

//早期预览（512KB时触发）：尝试用不完整文件显示预览void MainDialog::slot_tryEarlyPreview(int fileid, QString localPath)
{
    if (!m_pendingSharePreviewFiles.contains(fileid))
        return;

    QString fileName = m_pendingSharePreviewFiles[fileid];
    QFileInfo fi(localPath);
    if (!fi.exists() || fi.size() < 1024) return;

    if (isVideoFile(fileName)) {
        if (fi.size() < 1024 * 1024) return;
        //尝试FFmpeg打开不完整视频取第一帧        showVideoThumbnail(localPath, fileName);
        //完整下载后会再次触发 slot_shareFileDownloaded 重新渲染    }
    //图片和文本等完整下载后再预览（它们需要完整数据）}

//分享文件下载完成后的预览处理void MainDialog::slot_shareFileDownloaded(int fileid, QString localPath)
{
    //新增：空路径表示服务器返回错误          if (localPath.isEmpty()) {
              //...找到 pendingSharePreviewFiles 中的文件名              QString fileName;
              if (m_pendingSharePreviewFiles.contains(fileid)) {
                  fileName = m_pendingSharePreviewFiles.take(fileid);
              }
              ui->lb_sharePreview->setPixmap(QPixmap());
              ui->lb_sharePreview->setText(QString("预览失败\n文件不存在或已被删除\n%1").arg(fileName));
              return;
          }
    qDebug() << "【预览】slot_shareFileDownloaded 被调用! fileid=" << fileid << "path=" << localPath;

    //检查是否在待预览列表中（先按fileid查，再按文件名回退匹配）    QString fileName;
    if (m_pendingSharePreviewFiles.contains(fileid)) {
        fileName = m_pendingSharePreviewFiles.take(fileid);
    } else {
        //回退：按本地文件名匹配        QFileInfo fi(localPath);
        for (auto it = m_pendingSharePreviewFiles.begin(); it != m_pendingSharePreviewFiles.end(); ++it) {
            if (it.value() == fi.fileName()) {
                fileName = it.value();
                m_pendingSharePreviewFiles.erase(it);
                break;
            }
        }
        if (fileName.isEmpty()) return;  // 不是分享预览文件
    }

    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists()) {
        ui->lb_sharePreview->setText("预览失败\n文件不存在");
        return;
    }

    //0 字节文件：直接显示空文件提示，不尝试任何编解码    if (fileInfo.size() == 0) {
        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText(QString("(空文件)\n%1\n\n文件大小为 0 字节").arg(fileName));
        ui->lb_sharePreview->setAlignment(Qt::AlignCenter);
        return;
    }

    //根据文件类型显示预览    if (isImageFile(fileName)) {
        //显示图片预览        QPixmap pixmap(localPath);
        if (pixmap.isNull()) {
            ui->lb_sharePreview->setText("预览失败\n无法加载图片");
        } else {
            QSize previewSize = ui->lb_sharePreview->size();
            if (previewSize.width() < 10) previewSize = QSize(200, 300);
            ui->lb_sharePreview->setPixmap(
                pixmap.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else if (isVideoFile(fileName)) {
        //视频：提取第一帧作为缩略图        showVideoThumbnail(localPath, fileName);
    } else if (isTextFile(fileName)) {
        //文本：显示前4KB内容        showTextPreview(localPath, fileName);
    } else {
        //其他类型：显示文件信息        QString ext = QFileInfo(fileName).suffix().toUpper();
        ui->lb_sharePreview->setPixmap(QPixmap());
        ui->lb_sharePreview->setText(
            QString("文件已下载\n\n%1\n\n类型: %2\n大小: %3")
                .arg(fileName)
                .arg(ext.isEmpty() ? "未知" : ext)
                .arg(FileInfo::getSize(fileInfo.size())));
    }
}
