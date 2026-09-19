#include "playerdialog.h"
#include "ui_playerdialog.h"
#include<QDebug>
//#define _DEF_PATH "C:/Users/张项飞/Music/夏吉ゆうこ _ 早見沙織 - ray (超かぐや姫！ Version).flac"

PlayerDialog::PlayerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PlayerDialog)
{
    ui->setupUi(this);
    m_player=new VideoPlayer;
    m_currentPath="";  //初始没有播放文件

    //去掉按钮的焦点框：点击后按钮会一直带着一圈颜色边框，
    //只有按住时才有按压效果、松开后不留任何边框    const QList<QPushButton*> btns = this->findChildren<QPushButton*>();
    for (QPushButton* b : btns)
        b->setFocusPolicy(Qt::NoFocus);

    connect(m_player,SIGNAL(SIG_getOneImage(QImage)),this,SLOT(slot_setImage(QImage)));
    slot_PlayerStateChanged(PlayerState::Stop);
    //测试路径
    //m_player->setFileName(_DEF_PATH);
    //connect(&m_timer,SIGNAL(timeout()),this,SLOT())
    connect(m_player,SIGNAL(SIG_PlayerStateChanged(int)),this,SLOT(slot_PlayerStateChanged(int)));
    connect( m_player, SIGNAL( SIG_TotalTime(qint64))  ,   this ,SLOT( slot_getTotalTime(qint64)) );
    connect(&m_timer,SIGNAL(timeout()),this,SLOT(slot_TimerTimeOut()));
     m_timer.setInterval(500); //超时时间500毫秒
     //安装时间过滤器，让该对象成为被观察对象  //this去执行函数
     ui->slider_progress->installEventFilter(this);
}

PlayerDialog::~PlayerDialog()
{
    m_player->stop(true);

    delete ui;
    delete m_player;
}


//Qt线程
//QThread 定义子类 start()->run();
#include<QFileDialog>
//打开文件
void PlayerDialog::on_pb_start_clicked()
{
    //开始播放-> 一段时间内 获取图片
    //m_player->start();

    //首先 要先关闭  判断当前的状态 stop
    if(m_player->playerstate()!=PlayerState::Stop){
        m_player->stop(true);
        //清掉上一个视频残留的最后一帧并复位进度条，
        slot_PlayerStateChanged(PlayerState::Stop);
    }
    //打开浏览选择文件
    QString path=QFileDialog::getOpenFileName(this,"打开文件","./","视频文件 (*.flv *.rmvb *.avi *.MP4 *.mkv);; 所有文件(*.*);;");
    //判断
    if(path.isEmpty()) return;

    m_currentPath=path;  //记录当前播放路径，播完再点"播放"时从头重播
    //设置 m_play filename
    m_player->setFileName(path);

    m_player->start();
    //状态切换
    slot_PlayerStateChanged(PlayerState::Playing);
}
//由外部直接指定路径播放（不弹文件选择框）void PlayerDialog::on_pb_start_clicked_with_path(QString path)
{
    if(path.isEmpty()) return;
    //先停止当前播放    if(m_player->playerstate()!=PlayerState::Stop){
        m_player->stop(true);
        //清掉上一个视频残留的最后一帧并复位进度条，
        slot_PlayerStateChanged(PlayerState::Stop);
    }
    m_currentPath=path;  //记录当前播放路径，播完再点"播放"时从头重播
    //设置文件名并启动    m_player->setFileName(path);
    m_player->start();
    //更新界面状态    slot_PlayerStateChanged(PlayerState::Playing);
}

void PlayerDialog::slot_setImage(QImage img)
{
    //视频帧都交给OpenGL控件画
    ui->wdg_show->slot_setImage(img);
}

void PlayerDialog::on_pb_resume_clicked()
{
    //暂停状态：恢复播放
    if(m_player->playerstate()==PlayerState::Pause){
        m_player->play();
        //切换
        ui->pb_resume->hide();
        ui->pb_pause->show();
        return;
    }
    //自然播完后是Stop状态，点"播放"应从头重新播放
    startPlayCurrent();
}

//Stop状态从头播放
//自然播完时Stop信号先于播放线程退出发出，线程还没完全结束时QThread::start会失败，//所以先判断isRunning，还在运行就延迟200毫秒重试。void PlayerDialog::startPlayCurrent()
{
    if(m_currentPath.isEmpty()) return;
    if(m_player->playerstate()!=PlayerState::Stop) return;
    if(m_player->isRunning()){
        QTimer::singleShot(200, this, [this](){ startPlayCurrent(); });
        return;
    }
    m_player->setFileName(m_currentPath);
    m_player->start();
    slot_PlayerStateChanged(PlayerState::Playing);
}


void PlayerDialog::on_pb_pause_clicked()
{
    if(m_player->playerstate()!=PlayerState::Playing )return;
    m_player->pause();
    //切换
    ui->pb_resume->show();
    ui->pb_pause->hide();
}


//【点×停止】关闭播放窗口即停止播放：点×、按Esc都会走reject()，
//stop(true)等待播放线程退出。析构函数里的stop(true)作为兜底保留。void PlayerDialog::reject()
{
    m_player->stop(true);
    QDialog::reject();
}
void PlayerDialog ::slot_PlayerStateChanged(int state)
{
    switch( state )
    {
    case PlayerState::Stop:
        //旧播放线程退出时发出的 Stop 信号是排队投递的，
        //清成黑屏、进度条停走、按钮状态错乱。检查播放器真实状态：        //已经在播新视频就忽略这条过期信号。        if (m_player && m_player->playerstate() == PlayerState::Playing)
            break;
        qDebug()<< "VideoPlayer::Stop";
        m_timer.stop();
        ui->slider_progress->setValue(0);
        ui->lb_totalTime->setText("00:00:00");
        ui->lb_curTime->setText("00:00:00");
        ui->pb_pause->hide();
        ui->pb_resume->show();
    {
        QImage img;
        img.fill( Qt::black);
        slot_setImage( img );
    }
        this->update();
        isStop = true;
        break;
    case PlayerState::Playing:
        qDebug()<< "VideoPlayer::Playing";
        ui->pb_resume->hide();
        ui->pb_pause->show();
        m_timer.start();
        this->update();
        isStop = false;
        break;
    }
}

void PlayerDialog::slot_getTotalTime(qint64 uSec)
{
    //总时长未知(<=0)时把进度条范围置 0 并禁用：
    //原代码 setRange(0,-1) 会让进度条完全不可用    if (uSec <= 0) {
        ui->slider_progress->setRange(0, 0);
        ui->slider_progress->setEnabled(false);
        ui->lb_totalTime->setText("00:00:00");
        return;
    }
    ui->slider_progress->setEnabled(true);
    qint64 Sec = uSec/1000000;
    ui->slider_progress->setRange(0,Sec);//精确到秒
    QString hStr = QString("00%1").arg(Sec/3600);
    QString mStr = QString("00%1").arg(Sec/60);
    QString sStr = QString("00%1").arg(Sec%60);
    QString str =
    QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
    ui->lb_totalTime->setText(str);
}
//获取当前视频时间定时器
void PlayerDialog::slot_TimerTimeOut()
{
    if (QObject::sender() == &m_timer)
    {
        qint64 Sec = m_player->getCurrentTime()/1000000;
        ui->slider_progress->setValue(Sec);
        QString hStr = QString("00%1").arg(Sec/3600);
        QString mStr = QString("00%1").arg(Sec/60%60);
        QString sStr = QString("00%1").arg(Sec%60);
        QString str =
                QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
        ui->lb_curTime->setText(str);
        if(ui->slider_progress->value() == ui->slider_progress->maximum()
                && m_player->playerstate() == PlayerState::Stop)
        {
//slot_PlayerStateChanged( PlayerState::Stop );            m_player->stop(true);
        }else if(ui->slider_progress->value() + 1  ==
                 ui->slider_progress->maximum()
                 && m_player->playerstate() == PlayerState::Stop)
        {
//slot_PlayerStateChanged( PlayerState::Stop );            m_player->stop(true);
        }
    }
}
#include<QStyle>
#include<QMouseEvent>
bool PlayerDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->slider_progress) {
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                int min=ui->slider_progress->minimum();
                int max=ui->slider_progress->maximum();
                int value = QStyle::sliderValueFromPosition(
                min, max, mouseEvent->pos().x(), ui->slider_progress->width());

                m_timer.stop();
                ui->slider_progress->setValue(value);
                m_player->seek((qint64)value*1000000);  //value 秒
                m_timer.start();
                return true;
            } else {
                return false;
            }
        } else {
            //将事件继续传递给父类            return QDialog::eventFilter(obj, event);
    }
}
