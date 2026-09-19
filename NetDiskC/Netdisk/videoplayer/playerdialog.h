#ifndef PLAYERDIALOG_H
#define PLAYERDIALOG_H

#include <QDialog>
#include"videoplayer.h"
#include<QTimer>
QT_BEGIN_NAMESPACE
namespace Ui { class PlayerDialog; }
QT_END_NAMESPACE

class PlayerDialog : public QDialog
{
    Q_OBJECT

public:
    PlayerDialog(QWidget *parent = nullptr);
    ~PlayerDialog();


public:
    void on_pb_start_clicked_with_path(QString path);
    void reject();  //【点×停止】关闭播放窗口即停止播放（点×/Esc都会走这里）

private slots:
    void on_pb_start_clicked();

    void slot_setImage(QImage img);

    void on_pb_resume_clicked();

    void on_pb_pause_clicked();

    void slot_PlayerStateChanged(int state);

    void slot_getTotalTime(qint64 uSec);

    void slot_TimerTimeOut();

    //事件过滤器
    bool eventFilter(QObject *obj,QEvent *event);
    //自然播完(Stop状态)后从头重新播放
    void startPlayCurrent();
private:
    Ui::PlayerDialog *ui;
    VideoPlayer *m_player;
    QTimer m_timer;
    bool isStop;//停止的状态
    QString m_currentPath;  //当前播放的文件路径，用于播完再点"播放"时从头重播
};
#endif // PLAYERDIALOG_H
