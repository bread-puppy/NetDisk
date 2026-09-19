#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QThread>
#include<QImage>
#include <QMutex>
#include <QWaitCondition>
#include"PacketQueue.h"
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 //1 second of 48khz 32bit audio
#define SDL_AUDIO_BUFFER_SIZE 1024 //

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include"libavutil/time.h"
#include "SDL.h"
}
enum PlayerState
{
    Playing = 0,
    Pause,
    Stop
};
class VideoPlayer ;
typedef struct VideoState {
    AVFormatContext *pFormatCtx;//相当于视频”文件指针”
    ///////////////音频///////////////////////////////////
    AVStream
    *audio_st; //音频流
    PacketQueue *audioq;//音频缓冲队列
    AVCodecContext *pAudioCodecCtx ;//音频解码器信息指针
    int audioStream;//视频音频流索引
    double audio_clock; ///<pts of last decoded frame 音频时钟
    SDL_AudioDeviceID audioID; //音频 ID
    AVFrame out_frame; //设置参数，供音频解码后的swr_alloc_set_opts使用。
    /// 音频回调函数使用的量
    uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf_index = 0;
    AVFrame *audioFrame;
    //////////////////////////////////////////////////////
    ///////////////视频///////////////////////////////////
    AVStream
    *video_st; //视频流
    PacketQueue *videoq;//视频队列
    AVCodecContext *pCodecCtx ;//音频解码器信息指针
    int videoStream;//视频音频流索引
    double video_clock; ///<pts of last decoded frame 视频时钟
    SDL_Thread *video_tid;  //视频线程id
    //////////////////////////////////////////////////////
    /// 播放控制的变量
    bool isPause;//暂停标志
    bool quit; //停止
    bool readFinished; //读线程文件读取完毕
    bool readThreadFinished; //读取线程是否结束
    bool videoThreadFinished; // 视频线程是否结束
    /////////////////////////////////////////////////////
    //// 跳转相关的变量
    int   seek_req; //跳转标志 -- 读线程
    int64_t    seek_pos; //跳转的位置 -- 微秒
    int        seek_flag_audio;//跳转标志 -- 用于音频线程中
    int        seek_flag_video;//跳转标志 -- 用于视频线程中
    double     seek_time; //跳转的时间(秒)  值和seek_pos是一样的
    //////////////////////////////////////////////////////
    int64_t start_time; //单位 微秒
    //视频帧节拍器（秒）：音频未就绪/无PTS时按视频自身时间轴
    //匀速播放。原先只靠音频时钟同步，音频时钟不走时视频线程会永久
    //等待（黑屏）或全速狂奔（一闪而过）。
    double frame_timer;
    VideoState()
    {
        audio_clock = video_clock  = start_time = 0;
        pauseMutex = nullptr;
        pauseCond  = nullptr;
    }
    //暂停/恢复 条件变量（由 VideoPlayer::run() 初始化）
    QMutex*       pauseMutex;
    QWaitCondition* pauseCond;
    VideoPlayer* m_player;//用于调用函数
} VideoState;
class VideoPlayer : public QThread
{
    Q_OBJECT
//qt通过信号槽 实现qt 多线程通信
public:

signals:
    void SIG_getOneImage(QImage img); //这里不能用引用，在run()中对象可能会被回收
    void SIG_PlayerStateChanged(int flag);   //切换当前的播放状态
    void SIG_TotalTime(qint64 uSec);
public:
    VideoPlayer();
    void run();
public slots:
    void SendGetOneImage(QImage &img);
private:
    QString m_fileName;
    VideoState m_videoState;
    PlayerState m_playerState;
public:
    ///播放控制
    void play();
    void pause();
    void stop( bool isWait);
    void seek(int64_t pos);
    void setFileName(const QString &fileName);
    double getCurrentTime();
    int64_t getTotalTime();
    PlayerState playerstate() const; //为了完成播放的控制, 需要在VideoPlayer中添加成员PlayerState m_playerState;播放状态是一个枚举,代表着播放状态.

};

#endif // VIDEOPLAYER_H
