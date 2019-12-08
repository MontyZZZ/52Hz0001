#ifndef SCREENRECORD_H
#define SCREENRECORD_H

#include <QObject>

#include <atomic>
#include <condition_variable>
#include <mutex>

extern "C"
{
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
#include <libavdevice\avdevice.h>
#include <libavutil\fifo.h>
#include <libavutil\imgutils.h>
#include <libavutil\audio_fifo.h>
#include <libswresample\swresample.h>
#include <libavutil\avassert.h>
};

class ScreenRecord : public QObject
{
    Q_OBJECT
public:
    enum RecordState {
        None,
        Started,
        Paused,
        Stopped,
        Unknown,
    };
    explicit ScreenRecord(QObject *parent = nullptr);

    void initData(const QVariantMap& map);
    void recordThread();
    void collectionThread();     // 截屏线程
    void collectionAThread();    // 录音线程
    int grabScreen();
    int openAudio();
    int openOutputFile();
    void setEncodeParm();
    void initBuffer();
    void initAudioBuffer();
    void flushDecoder();
    void flushEncoder();
    void flushAudioDecoder();
    void flushEncoders();
    void release();

    AVFrame* allocAudioFrame(AVCodecContext* cContext, int nbSamples);

    void setFilePath(const QString str);

signals:

public slots:
    void start();
    void stop();


private:
    QString            m_filePath;
    int                m_width;
    int                m_height;
    int                m_fps;
    int				   m_audioBit;
    RecordState        m_state;   //录制状态
    int64_t			   m_vCurPts;
    int64_t			   m_aCurPts;

    // AVFormatContext描述了一个媒体文件或媒体流的构成和基本信息，位于avformat.h文件中。
    AVFormatContext*   m_vInFmtCtx;   // 输入
    AVFormatContext*   m_vOutFmtCtx;   // 输出
    AVFormatContext*   m_aInFmtCtx;

    // AVCodecContext编解码器信息
    AVCodecContext*    m_vInDecodeCtx;    // 视频输入解码器
    AVCodecContext*    m_vOutEncodeCtx;    // 视频输出编码器
    AVCodecContext*    m_aInDecodeCtx;   // 音频输入解码器
    AVCodecContext*    m_aOutEncodeCtx;   // 音频输出编码器

    // 保存索引
    int                m_vInIndex;		//输入视频流索引
    int                m_vOutIndex;        //输出视频流索引
    int                m_aInIndex;       //输入音频流索引
    int                m_aOutIndex;       //输出音频流索引


    AVDictionary*      m_dict;

    // 视频相关
    SwsContext*        m_swsCtx;

    AVFifoBuffer*      m_vFifoBuf;
    AVFrame*           m_outFrame;
    uint8_t*           m_outFrameBuf;
    int				   m_outFrameSize;	//一个输出帧的字节

    std::condition_variable m_vInNotFull;	//当fifoBuf满了，采集线程挂起
    std::condition_variable m_vOutNotEmpty;	//当fifoBuf空了，编码线程挂起
    std::mutex				m_mtxVideo;			//m_cvNotFull和m_cvNotEmpty共用这个mutex
    std::condition_variable m_cvNotPause;	//当点击暂停的时候，采集线程挂起
    std::mutex				m_mtxPause;

    // 音频相关
    SwrContext*             m_swrCtx;
    AVAudioFifo*            m_aFifoBuf;

    std::condition_variable m_aInNotFull;
    std::condition_variable m_aOutNotEmpty;
    std::mutex				m_mtxAudio;
    int                     m_nbSamples;
};

#endif // SCREENRECORD_H
