#include "screenrecord.h"

#include <QVariantMap>
#include <QtDebug>
#include <QFileInfo>
#include <QMutex>
#include <QAudioDeviceInfo>

#include <thread>
#include <string>
#include <fstream>
#include <dshow.h>

static int g_vInCnt = 0;	//采集视频帧数
static int g_vOutCnt = 0;	//编码视频帧数
static int g_aInCnt = 0;	//采集音频帧数
static int g_aOutCnt = 0;	//编码音频帧数

ScreenRecord::ScreenRecord(QObject *parent) : QObject(parent)
{
    m_state = RecordState::None;
    m_vInFmtCtx = nullptr;
    m_vOutFmtCtx = nullptr;
    m_aInFmtCtx = nullptr;
    m_dict = nullptr;
    m_vInIndex = -1;
    m_vOutIndex = -1;
    m_vFifoBuf = nullptr;
    m_vInDecodeCtx = nullptr;
    m_vOutEncodeCtx = nullptr;
    m_aInDecodeCtx = nullptr;
    m_aOutEncodeCtx = nullptr;
    m_swsCtx = nullptr;
    m_swrCtx = nullptr;
    m_vFifoBuf = nullptr;
    m_aFifoBuf = nullptr;
	m_fps = 30;
    m_vCurPts = 0;
    m_aCurPts = 0;
    m_outFrameSize = 0;
}

void ScreenRecord::initData(const QVariantMap &map)
{
    m_filePath = map["filePath"].toString();
    m_width = map["width"].toInt();
    m_height = map["height"].toInt();
    m_fps = map["fps"].toInt();
    m_audioBit = map["audioBitrate"].toInt();

}

void ScreenRecord::recordThread()
{
    qDebug() << "recordThread";
// ffmpeg 第一步 注册设备
    avdevice_register_all();

    if (grabScreen() < 0) {
        return;
    }

    if (openAudio() < 0) {
        return;
    }

    if (openOutputFile() < 0) {
        return;
    }

    initBuffer();
    initAudioBuffer();

    // 采集
    std::thread cThread(&ScreenRecord::collectionThread, this);
    std::thread caThread(&ScreenRecord::collectionAThread, this);
    cThread.detach();
    caThread.detach();

    // 编码
    bool done = false;
    int errorId = -1;
    int frameIndex = 0;
    int aFrameIndex = 0;
    while (1) {
        if (RecordState::Stopped == m_state && !done) {
            done = true;
        }

        if (done) {
            std::unique_lock<std::mutex> mutex(m_mtxVideo, std::defer_lock);
            std::unique_lock<std::mutex> amutex(m_mtxAudio, std::defer_lock);
            std::lock(mutex, amutex);

            qDebug() << av_fifo_size(m_vFifoBuf) << m_outFrameSize;
            qDebug() << av_audio_fifo_size(m_aFifoBuf) << m_nbSamples;
            if (av_fifo_size(m_vFifoBuf) <= m_outFrameSize
                && av_audio_fifo_size(m_aFifoBuf) < m_nbSamples) {
                qDebug() << "video and audio buffer are empty";
                break;
            }
        }

        if (av_compare_ts(m_vCurPts, m_vOutFmtCtx->streams[m_vOutIndex]->time_base,
                          m_aCurPts, m_vOutFmtCtx->streams[m_aOutIndex]->time_base) <= 0) {
            // 处理视频
            qDebug() << "------ video";
            if (done) {
                std::lock_guard<std::mutex> mutex(m_mtxVideo);
                if (av_fifo_size(m_vFifoBuf) < m_outFrameSize) {
                    m_vCurPts = INT_MAX;
                    continue;
                }
            } else {
                std::unique_lock<std::mutex> mutex(m_mtxVideo);
                m_vOutNotEmpty.wait(mutex, [this] {
                    return av_fifo_size(m_vFifoBuf) >= m_outFrameSize;
                });
            }
            av_fifo_generic_read(m_vFifoBuf, m_outFrameBuf, m_outFrameSize, nullptr);
            m_vInNotFull.notify_one();

            // 设置视频帧参数
            m_outFrame->pts = frameIndex++;
            m_outFrame->format = m_vOutEncodeCtx->pix_fmt;
            m_outFrame->width = m_vOutEncodeCtx->width;
            m_outFrame->height = m_vOutEncodeCtx->height;
            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);

            errorId = avcodec_send_frame(m_vOutEncodeCtx, m_outFrame);
            if (errorId != 0) {
                qDebug() << "video avcodec_send_frame failed,  errorId = " << errorId;
                av_packet_unref(pkt);
                continue;
            }

            errorId = avcodec_receive_packet(m_vOutEncodeCtx, pkt);
            if (errorId != 0) {
                qDebug() << "video avcodec_receive_packet failed, errorId: " << errorId;
                av_packet_unref(pkt);
                continue;
            }

            pkt->stream_index = m_vOutIndex;
            // 时间转换 timebase从编码层转换成复用层
            av_packet_rescale_ts(pkt, m_vOutEncodeCtx->time_base,
                                 m_vOutFmtCtx->streams[m_vOutIndex]->time_base);
            m_vCurPts = pkt->pts;
            qDebug() << "m_vCurPts: " << m_vCurPts;

            // 将编码帧写入到输出文件
            errorId = av_interleaved_write_frame(m_vOutFmtCtx, pkt);
            if (errorId != 0) {
                qDebug() << "video av_interleaved_write_frame failed, errorId: " << errorId;
            } else {
                qDebug() << "Write video packet id: " << ++g_vOutCnt;
            }

            av_packet_unref(pkt);
        } else {
            // 处理音频
            qDebug() << "------- audio";
            if (done) {
                std::lock_guard<std::mutex> mutex(m_mtxAudio);
                if (av_audio_fifo_size(m_aFifoBuf) < m_nbSamples) {
                    m_aCurPts = INT_MAX;
                    continue;
                }
            } else {
                std::unique_lock<std::mutex> mutex(m_mtxAudio);
                m_aOutNotEmpty.wait(mutex, [this] {
                    return av_audio_fifo_size(m_aFifoBuf) >= m_nbSamples;
                });
            }
            AVFrame* aFrame = av_frame_alloc();
            aFrame->nb_samples = m_nbSamples;
            aFrame->channel_layout = m_aOutEncodeCtx->channel_layout;
            aFrame->format = m_aOutEncodeCtx->sample_fmt;
            aFrame->sample_rate = m_aOutEncodeCtx->sample_rate;
            aFrame->pts = m_nbSamples * aFrameIndex++;

            int errorId = av_frame_get_buffer(aFrame, 0);
            av_audio_fifo_read(m_aFifoBuf, reinterpret_cast<void **>(aFrame->data), m_nbSamples);
            m_aInNotFull.notify_one();

            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);
            errorId = avcodec_send_frame(m_aOutEncodeCtx, aFrame);
            if (errorId != 0) {
                qDebug() << "audio avcodec_send_frame failed, errorId: " << errorId;
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }

            errorId = avcodec_receive_packet(m_aOutEncodeCtx, pkt);
            if (errorId != 0) {
                qDebug() << "audio avcodec_receive_packet failed!, errorId: " << errorId;
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = m_aOutIndex;

            av_packet_rescale_ts(pkt, m_aOutEncodeCtx->time_base,
                                 m_vOutFmtCtx->streams[m_aOutIndex]->time_base);
            m_aCurPts = pkt->pts;
            qDebug() << "m_aCurPts: " << m_aCurPts;

            errorId = av_interleaved_write_frame(m_vOutFmtCtx, pkt);
            if (errorId != 0) {
                qDebug() << "audio av_interleaved_write_frame failed, errorId: " << errorId;
            } else {
                qDebug() << "Write audio packet id: " << ++g_aOutCnt;
            }

            qDebug() <<"~~~~~~~ test bug1" << aFrame;
            av_frame_free(&aFrame);
            qDebug() <<"~~~~~~~ test bug2";
            av_packet_unref(pkt);
            qDebug() <<"~~~~~~~ test bug3";
        }


    }

    flushEncoders();
    av_write_trailer(m_vOutFmtCtx);
    release();
    qDebug() << "parent thread exit";
}

void ScreenRecord::collectionThread()
{
    //AVPacket是FFmpeg中很重要的一个数据结构，
    //它保存了解复用（demuxer)之后，解码（decode）之前的数据（仍然是压缩后的数据）和关于这些数据的一些附加的信息，
    //如显示时间戳（pts），解码时间戳（dts）,数据时长（duration），所在流媒体的索引（stream_index）等等。
    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);
    int y_size = m_width * m_height;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = av_frame_alloc();

    // 通过指定像素格式、图像宽、图像高来计算所需的内存大小
    int frameBuffSize = av_image_get_buffer_size(m_vOutEncodeCtx->pix_fmt, m_width, m_height, 1);
    // 根据大小申请内存
    uint8_t* frameBuf = static_cast<uint8_t*>(av_malloc(static_cast<size_t>(frameBuffSize)));
    // 对申请的内存进行格式化
    av_image_fill_arrays(newFrame->data, newFrame->linesize, frameBuf,
                         m_vOutEncodeCtx->pix_fmt, m_width, m_height, 1);

    int errorId = -1;
    while (RecordState::Stopped != m_state) {
        if (RecordState::Paused == m_state) {
            std::unique_lock<std::mutex> mutex(m_mtxPause);
            m_cvNotPause.wait(mutex, [this] {
                return m_state != RecordState::Paused;
            });
        }

        if (av_read_frame(m_vInFmtCtx, pkt) < 0) {
            qDebug() << "video av_read_frame failed!";
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index != m_vInIndex) {
            qDebug() << "not a video packet from video input";
            av_packet_unref(pkt);
            continue;
        }

        // 发送数据到ffmepg，放到解码队列中
        errorId = avcodec_send_packet(m_vInDecodeCtx, pkt);
        if (errorId != 0) {
            qDebug() << "avcodec_send_packet failed: errorId: " << errorId;
            av_packet_unref(pkt);
            continue;
        }

        // 将成功的解码队列中取出1个frame
        errorId = avcodec_receive_frame(m_vInDecodeCtx, oldFrame);
        if (errorId != 0) {
            qDebug() << "avcodec_receive_frame failed, errorId: "<< errorId;
            av_packet_unref(pkt);
            continue;
        }
        ++g_vInCnt;

        // sws_scale函数 实现：1.图像色彩空间转换；2.分辨率缩放；3.前后图像滤波处理。
        sws_scale(m_swsCtx, oldFrame->data, oldFrame->linesize, 0,
                  m_vOutEncodeCtx->height, newFrame->data, newFrame->linesize);

        std::unique_lock<std::mutex> mutex(m_mtxVideo);
        m_vInNotFull.wait(mutex, [this] {
            return av_fifo_space(m_vFifoBuf) >= m_outFrameSize;
        });

        // 将数据写入fifobuf
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, nullptr);
        m_vOutNotEmpty.notify_one();

        av_packet_unref(pkt);
    }
    flushDecoder();

    av_free(frameBuf);
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
    qDebug() << "screen collection record thread exit";
}

void ScreenRecord::collectionAThread()
{
    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);
    int nbSamples = m_nbSamples;
    int dstNbSamples, maxDstNbSamples;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = allocAudioFrame(m_aOutEncodeCtx, nbSamples);

    dstNbSamples = static_cast<int>(av_rescale_rnd(nbSamples, m_aOutEncodeCtx->sample_rate,
                                                   m_aInDecodeCtx->sample_rate, AV_ROUND_UP));
    maxDstNbSamples = dstNbSamples;

    int errorId = -1;
    while (RecordState::Stopped != m_state) {
        if (RecordState::Paused == m_state) {
            std::unique_lock<std::mutex> mutex(m_mtxPause);
            m_cvNotPause.wait(mutex, [this] {
                return m_state != RecordState::Paused;
            });
        }

        if (av_read_frame(m_aInFmtCtx, pkt) < 0) {
            qDebug() << "av_read_frame audio read failed!";
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index != m_aInIndex) {
            qDebug() << "not a audio packet";
            av_packet_unref(pkt);
            continue;
        }

        errorId = avcodec_send_packet(m_aInDecodeCtx, pkt);
        if (errorId != 0) {
            qDebug() << "audio_send_packet failed, errorId: " << errorId;
            av_packet_unref(pkt);
            continue;
        }

        errorId = avcodec_receive_frame(m_aInDecodeCtx, oldFrame);
        if (errorId != 0) {
            qDebug() << "avcodec_receive_frame failed!, errorId: " << errorId;
            av_packet_unref(pkt);
            continue;
        }
        g_aInCnt++;

        // static_cast<int>
        dstNbSamples = static_cast<int>(av_rescale_rnd(swr_get_delay(m_swrCtx, m_aInDecodeCtx->sample_rate) + oldFrame->nb_samples,
                                                                     m_aOutEncodeCtx->sample_rate, m_aInDecodeCtx->sample_rate,
                                                                     AV_ROUND_UP));

        if (dstNbSamples > maxDstNbSamples) {
            qDebug() << "audio newFrame relloc";
            av_freep(&newFrame->data[0]);

            errorId = av_samples_alloc(newFrame->data, newFrame->linesize, m_aOutEncodeCtx->channels,
                                       dstNbSamples, m_aOutEncodeCtx->sample_fmt, 1);

            if (errorId < 0) {
                qDebug() << "av_sample_alloc failed!";
                return;
            }

            maxDstNbSamples = dstNbSamples;
            m_aOutEncodeCtx->frame_size = dstNbSamples;
            m_nbSamples = newFrame->nb_samples;
        }

        newFrame->nb_samples = swr_convert(m_swrCtx, newFrame->data, dstNbSamples,
                                           const_cast<const uint8_t **>(oldFrame->data), oldFrame->nb_samples);
        if (newFrame->nb_samples < 0) {
            qDebug() << "swr_convert failed!";
            return;
        }

        std::unique_lock<std::mutex> mutex(m_mtxAudio);
        m_aInNotFull.wait(mutex, [newFrame, this] {
            return av_audio_fifo_space(m_aFifoBuf) >= newFrame->nb_samples;
        });

        if (av_audio_fifo_write(m_aFifoBuf, reinterpret_cast<void **>(newFrame->data), newFrame->nb_samples) < newFrame->nb_samples) {
            qDebug() << "av_audio_fifo_write failed!";
            return;
        }

        m_aOutNotEmpty.notify_one();
    }
    flushAudioDecoder();
    av_frame_free(&oldFrame);
   // av_frame_free(&newFrame);
    qDebug() << "audio record thread exit";
}

// 抓取屏幕
int ScreenRecord::grabScreen()
{
    //  "gdigrab"  windows基于gdi的抓屏软件
    //Windows采集设备的主要方式是dshow、vfwcap、gdigrab，
    //其中dshow可以用来抓取摄像头、采集卡、麦克风等，
    //vfwcap主要用来采集摄像头类设备，
    //gdigrab则是抓取Windows窗口程序
    AVInputFormat* ifmt = av_find_input_format("gdigrab");

    // AVDictionary 健值对存储工具  类似于 map,set 用于参数传递
    AVDictionary* options = nullptr;
    AVCodec* decoder = nullptr;

    // 设置帧数键值对   options "fps" ---->  m_fps的值(30)  framerate
    av_dict_set(&options, "fps", QString::number(m_fps).toStdString().c_str(), 0);

    // 打开输入  m_vOutFmtCtx 赋值
    // "desktop" 抓取整个屏幕
    if (avformat_open_input(&m_vInFmtCtx, "desktop", ifmt, &options) != 0) {
        qDebug() << "can't open input stream";
        return -1;
    }

    if (avformat_find_stream_info(m_vInFmtCtx, nullptr) < 0) {
        qDebug() << "can't find stream information";
        return -1;
    }

    int errorId = -1;
    for (unsigned i = 0; i < m_vInFmtCtx->nb_streams; ++i) {
        AVStream* stream = m_vInFmtCtx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            // 获取解码器
            decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!decoder) {
                qDebug() << "avcodec_find_decoder find decode failed!";
                return -1;
            }

            // 拷贝解码器参数到 m_vInDecodeCtx
            m_vInDecodeCtx = avcodec_alloc_context3(decoder);
            errorId = avcodec_parameters_to_context(m_vInDecodeCtx, stream->codecpar);
            if (errorId < 0) {
                qDebug() << "avcodec_parameters_to_context  failed";
                return -1;
            }
            m_vInIndex = static_cast<int>(i);
            break;
        }
    }

    // 初始化视频解码器
    if (avcodec_open2(m_vInDecodeCtx, decoder, nullptr) < 0) {  // &m_dict
        qDebug() << "avcodec_open2 failed";
        return -1;
    }

    // 像素转换  源图转换成需要的图
    m_swsCtx = sws_getContext(m_vInDecodeCtx->width, m_vInDecodeCtx->height,
                              m_vInDecodeCtx->pix_fmt,
                              m_width, m_height,
                              AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR,
                              nullptr, nullptr, nullptr);

    return 0;
}

int ScreenRecord::openAudio()
{
    //音频采集
    AVInputFormat *ifmt = av_find_input_format("dshow");
    QString audioDeviceName = "audio=";

    // 获取音频输入设备 一般都是麦克风
    QList<QAudioDeviceInfo> audioDeviceList = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    if (audioDeviceList.isEmpty()) {
        return -1;
    }

    audioDeviceName += audioDeviceList.first().deviceName();
    qDebug() << "audioDeviceName: " << audioDeviceName;

    if(avformat_open_input(&m_aInFmtCtx, audioDeviceName.toStdString().c_str(), ifmt, nullptr) < 0) {
        qDebug() << "can't open audio input stream";
        return -1;
    }

    if (avformat_find_stream_info(m_aInFmtCtx, nullptr) < 0) {
        qDebug() << "can't find audio input stream";
        return -1;
    }

    AVCodec *decoder = nullptr;
    int errorId = -1;
    for (unsigned i = 0; i < m_aInFmtCtx->nb_streams; ++i) {
        AVStream* stream = m_aInFmtCtx->streams[i];
        if (AVMEDIA_TYPE_AUDIO == stream->codecpar->codec_type) {
            decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!decoder) {
                qDebug() << "not find audio decoder";
                return -1;
            }

            // 拷贝解码器参数到 m_aInDecodeCtx
            m_aInDecodeCtx = avcodec_alloc_context3(decoder);
            errorId = avcodec_parameters_to_context(m_aInDecodeCtx, stream->codecpar);
            if (errorId < 0) {
                qDebug() << "audio avcodec_parameters_to_context failed, errorId: " << errorId;
                return -1;
            }

            m_aInIndex = static_cast<int>(i);
            break;
        }
    }

    // 初始化音频解码器
    if (avcodec_open2(m_aInDecodeCtx, decoder, nullptr) < 0) {
        qDebug() << "can't find or open audio decoder!";
        return -1;
    }

    return 0;
}

static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

// 打开输出文件 设置好格式信息
int ScreenRecord::openOutputFile()
{
    AVStream* oStream = nullptr;
	AVStream* aStream = nullptr;
    std::string outFilePath = m_filePath.toStdString();
    //QFileInfo fileInfo(m_filePath);
    int errorId = -1;

    qDebug() << outFilePath.c_str();

    // 初始化输出结构体 m_vOutFmtCtx 参数中的设置为nullptr 会自动猜测视频格式
    errorId = avformat_alloc_output_context2(&m_vOutFmtCtx, nullptr, nullptr, outFilePath.c_str());
    if (errorId < 0) {
        qDebug() << "avformat_alloc_output_context2 failed";
        return -1;
    }

    if (m_vInFmtCtx->streams[m_vInIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        oStream = avformat_new_stream(m_vOutFmtCtx, nullptr);
        if (!oStream) {
            qDebug() << "New stream for output failed!";
            return -1;
        }

        m_vOutIndex = oStream->index;
        oStream->time_base = AVRational{1, m_fps};

        // 初始化解码器
        m_vOutEncodeCtx = avcodec_alloc_context3(nullptr);
        if (!m_vOutEncodeCtx) {
            qDebug() << "avcodec_alloc_context3  failed!";
            return -1;
        }

        // setEncodeParm();
        m_vOutEncodeCtx->width = m_width;
        m_vOutEncodeCtx->height = m_height;
        m_vOutEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        m_vOutEncodeCtx->time_base.num = 1;
        m_vOutEncodeCtx->time_base.den = m_fps;
        m_vOutEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        m_vOutEncodeCtx->codec_id = AV_CODEC_ID_H264;
        m_vOutEncodeCtx->bit_rate = 800 * 1000;
        m_vOutEncodeCtx->rc_max_rate = 800 * 1000;
        m_vOutEncodeCtx->rc_buffer_size = 500 * 1000;
        //设置图像组层的大小, gop_size越大，文件越小
        m_vOutEncodeCtx->gop_size = 30;
        m_vOutEncodeCtx->max_b_frames = 3;
         //设置h264中相关的参数,不设置avcodec_open2会失败
        m_vOutEncodeCtx->qmin = 10;	//2
        m_vOutEncodeCtx->qmax = 31;	//31
        m_vOutEncodeCtx->max_qdiff = 4;
        m_vOutEncodeCtx->me_range = 16;	//0
        m_vOutEncodeCtx->max_qdiff = 4;	//3
        m_vOutEncodeCtx->qcompress = 0.6;	//0.5
		

        // 设置编码器
        AVCodec* encoder;
        encoder = avcodec_find_encoder(m_vOutEncodeCtx->codec_id);
        if (!encoder) {
            qDebug() << "can't find the encoder, id: " << m_vOutEncodeCtx->codec_id;
            return -1;
        }
        m_vOutEncodeCtx->codec_tag = 0;

        // 设置sps/pps
        m_vOutEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // 打开编码器
        errorId = avcodec_open2(m_vOutEncodeCtx, encoder, nullptr);
        if (errorId < 0) {
            qDebug() << "can't open encoder id: " << encoder->id << "error code: " << errorId;
            return -1;
        }

        // 编码器参数传给输出流
        errorId = avcodec_parameters_from_context(oStream->codecpar, m_vOutEncodeCtx);
        if (errorId < 0) {
            qDebug() << "avcodec_parameters_from_context, error id: " << errorId;
            return -1;
        }
    }

    if (m_aInFmtCtx->streams[m_aInIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        aStream = avformat_new_stream(m_vOutFmtCtx, nullptr);
        if (!aStream)
        {
            printf("can not new audio stream for output!\n");
            return -1;
        }
        m_aOutIndex = aStream->index;

        AVCodec *encoder = avcodec_find_encoder(m_vOutFmtCtx->oformat->audio_codec);
        if (!encoder)
        {
            qDebug() << "Can not find audio encoder, id: " << m_vOutFmtCtx->oformat->audio_codec;
            return -1;
        }
        m_aOutEncodeCtx = avcodec_alloc_context3(encoder);
        if (nullptr == m_vOutEncodeCtx)
        {
            qDebug() << "audio avcodec_alloc_context3 failed";
            return -1;
        }
        m_aOutEncodeCtx->sample_fmt = encoder->sample_fmts ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        m_aOutEncodeCtx->bit_rate = m_audioBit;
        m_aOutEncodeCtx->sample_rate = 44100;
        if (encoder->supported_samplerates)
        {
            m_aOutEncodeCtx->sample_rate = encoder->supported_samplerates[0];
            for (int i = 0; encoder->supported_samplerates[i]; ++i)
            {
                if (encoder->supported_samplerates[i] == 44100)
                    m_aOutEncodeCtx->sample_rate = 44100;
            }
        }
        m_aOutEncodeCtx->channels = av_get_channel_layout_nb_channels(m_aOutEncodeCtx->channel_layout);
        m_aOutEncodeCtx->channel_layout = AV_CH_LAYOUT_STEREO;
        if (encoder->channel_layouts)
        {
            m_aOutEncodeCtx->channel_layout = encoder->channel_layouts[0];
            for (int i = 0; encoder->channel_layouts[i]; ++i)
            {
                if (encoder->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    m_aOutEncodeCtx->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        m_aOutEncodeCtx->channels = av_get_channel_layout_nb_channels(m_aOutEncodeCtx->channel_layout);
        aStream->time_base = AVRational{ 1, m_aOutEncodeCtx->sample_rate };

        m_aOutEncodeCtx->codec_tag = 0;
        m_aOutEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (!check_sample_fmt(encoder, m_aOutEncodeCtx->sample_fmt))
        {
            qDebug() << "Encoder does not support sample format " << av_get_sample_fmt_name(m_aOutEncodeCtx->sample_fmt);
            return -1;
        }

        //打开音频编码器，打开后frame_size被设置
        errorId = avcodec_open2(m_aOutEncodeCtx, encoder, nullptr);
        if (errorId < 0)
        {
            qDebug() << "Can not open the audio encoder, id: " << encoder->id << "error code: " << errorId;
            return -1;
        }
        //将codecCtx中的参数传给音频输出流
        errorId = avcodec_parameters_from_context(aStream->codecpar, m_aOutEncodeCtx);
        if (errorId < 0)
        {
            qDebug() << "Output audio avcodec_parameters_from_context,error code:" << errorId;
            return -1;
        }

        m_swrCtx = swr_alloc();
        if (!m_swrCtx)
        {
            qDebug() << "swr_alloc failed";
            return -1;
        }
        av_opt_set_int(m_swrCtx, "in_channel_count", m_aInDecodeCtx->channels, 0);	//2
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_aInDecodeCtx->sample_rate, 0);	//44100
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_aInDecodeCtx->sample_fmt, 0);	//AV_SAMPLE_FMT_S16
        av_opt_set_int(m_swrCtx, "out_channel_count", m_aOutEncodeCtx->channels, 0);	//2
        av_opt_set_int(m_swrCtx, "out_sample_rate", m_aOutEncodeCtx->sample_rate, 0);	//44100
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", m_aOutEncodeCtx->sample_fmt, 0);	//AV_SAMPLE_FMT_FLTP

        if ((errorId = swr_init(m_swrCtx)) < 0)
        {
            qDebug() << "swr_init failed";
            return -1;
        }
    }
	
    // 打开输出文件
    if (!(m_vOutFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_vOutFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0) {
            qDebug() << "avio_open failed!";
            return -1;
        }
    }

    //写文件
    if (avformat_write_header(m_vOutFmtCtx, nullptr) < 0) {  //&m_dict
        qDebug() << "avformat_write_header failed!";
        return -1;
    }
    return 0;
}

// 设置输出视频的编码器格式
void ScreenRecord::setEncodeParm()
{
    // 参数参考 https://www.cnblogs.com/soief/archive/2013/12/12/3471465.html
    m_vOutEncodeCtx->width = m_width;
    m_vOutEncodeCtx->height = m_height;
    m_vOutEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    m_vOutEncodeCtx->time_base.num = 1;
    m_vOutEncodeCtx->time_base.den = m_fps;
    m_vOutEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    QString suffix = QFileInfo(m_filePath).suffix();
    if (!QString::compare("mp4", suffix, Qt::CaseInsensitive)
        || !QString::compare("mkv", suffix, Qt::CaseInsensitive)
        || !QString::compare("mov", suffix, Qt::CaseInsensitive)) {
        m_vOutEncodeCtx->codec_id = AV_CODEC_ID_H264;
        m_vOutEncodeCtx->bit_rate = 800 * 1000;
        m_vOutEncodeCtx->rc_max_rate = 800 * 1000;
        m_vOutEncodeCtx->rc_buffer_size = 500 * 1000;
        m_vOutEncodeCtx->gop_size = 30;
        m_vOutEncodeCtx->max_b_frames = 3;

        // h246 参数
        m_vOutEncodeCtx->qmin = 10;  //最小的量化因子。取值范围1-51。建议在10-30之间。
        m_vOutEncodeCtx->qmax = 31;  //最大的量化因子。取值范围1-51。建议在10-30之间。
        m_vOutEncodeCtx->max_qdiff = 4; //最大的在帧与帧之间进行切变的量化因子的变化量。
        m_vOutEncodeCtx->me_range = 16;
        m_vOutEncodeCtx->max_qdiff = 4;
        m_vOutEncodeCtx->qcompress = static_cast<float>(0.6);
        av_dict_set(&m_dict, "profile", "high", 0);

        // 调节编码速度和质量
        av_dict_set(&m_dict, "preset", "superfast", 0);
        av_dict_set(&m_dict, "threads", "0", 0);
        av_dict_set(&m_dict, "crf", "26", 0);
        av_dict_set(&m_dict, "tune", "zerolatency", 0);
    } else {
        m_vOutEncodeCtx->bit_rate = 4096 * 1000;
        if (!QString::compare("avi", suffix, Qt::CaseInsensitive)) {
            m_vOutEncodeCtx->codec_id = AV_CODEC_ID_MPEG4;
        } else if (!QString::compare("wmv", suffix, Qt::CaseInsensitive)) {
            m_vOutEncodeCtx->codec_id = AV_CODEC_ID_MSMPEG4V3;
        } else if (!QString::compare("flv", suffix, Qt::CaseInsensitive)) {
            m_vOutEncodeCtx->codec_id = AV_CODEC_ID_FLV1;
        } else {
            m_vOutEncodeCtx->codec_id = AV_CODEC_ID_MPEG4;
        }
    }
}

void ScreenRecord::initBuffer()
{
    m_outFrameSize = av_image_get_buffer_size(m_vOutEncodeCtx->pix_fmt, m_width, m_height, 1);
    m_outFrameBuf = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(m_outFrameSize)));
    m_outFrame = av_frame_alloc();

    // AVFrame 写入数据
    av_image_fill_arrays(m_outFrame->data, m_outFrame->linesize, m_outFrameBuf, m_vOutEncodeCtx->pix_fmt,
                         m_width, m_height, 1);

    // 申请帧缓存
    if (!(m_vFifoBuf = av_fifo_alloc_array(30,
                                          static_cast<size_t>(m_outFrameSize)))) { //static_cast<size_t>(m_fps),
        qDebug() << "av_fifo_alloc_array failed!";
        return;
    }
}

void ScreenRecord::initAudioBuffer()
{
    m_nbSamples = m_aOutEncodeCtx->frame_size;
    if (!m_nbSamples) {
        m_nbSamples = 1024;
    }

    m_aFifoBuf = av_audio_fifo_alloc(m_aOutEncodeCtx->sample_fmt, m_aOutEncodeCtx->channels,
                                     30 * m_nbSamples);

    if (!m_aFifoBuf) {
        qDebug() << "av_audio_fifo_alloc  failed!";
        return;
    }

}

// 将最后几帧输出
void ScreenRecord::flushDecoder()
{
    int y_size = m_width * m_height;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = av_frame_alloc();

    int errorId = avcodec_send_packet(m_vInDecodeCtx, nullptr);
    if (errorId != 0) {
        qDebug() << "flush video avcodec_send_packet failed, errorId: " << errorId;
        return;
    }
    while (errorId >= 0) {
        errorId = avcodec_receive_frame(m_vInDecodeCtx, oldFrame);
        if (errorId < 0) {
            if (AVERROR(EAGAIN) == errorId) {
                qDebug() << "flush EAGAIN avcodec_receive_frame";
                errorId = 1;
                continue;
            } else if (AVERROR_EOF == errorId) {
                qDebug() << "flush video decoder finished";
                break;
            }
            return;
        }
        ++g_vInCnt;

        sws_scale(m_swsCtx, oldFrame->data, oldFrame->linesize, 0,
                  m_vOutEncodeCtx->height, newFrame->data, newFrame->linesize);

        std::unique_lock<std::mutex> mutex(m_mtxVideo);
        m_vInNotFull.wait(mutex, [this] {
            return av_fifo_space(m_vFifoBuf) >= m_outFrameSize;
        });

        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, nullptr);
        m_vOutNotEmpty.notify_one();
    }
}

void ScreenRecord::flushEncoder()
{
    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);

    int errorId = avcodec_send_frame(m_vOutEncodeCtx, nullptr);

    while (errorId >= 0) {
        errorId = avcodec_receive_packet(m_vOutEncodeCtx, pkt);
        if (errorId < 0) {
            av_packet_unref(pkt);
            if (AVERROR(EAGAIN) == errorId) {
                qDebug() << "flush EAGAIN video avcodec_receive_packet";
                errorId = 1;
                continue;
            } else if (AVERROR_EOF == errorId) {
                qDebug() << "flush video encoder finished!";
                break;
            }
            qDebug() << "video avcodec_receive_packet  failed, errorId: " << errorId;
            return;
        }

        qDebug() << "flush succeed";

        pkt->stream_index = m_vOutIndex;
        av_packet_rescale_ts(pkt, m_vOutEncodeCtx->time_base,
                             m_vOutFmtCtx->streams[m_vOutIndex]->time_base);

        errorId = av_interleaved_write_frame(m_vOutFmtCtx, pkt);
        if (errorId != 0) {
            qDebug() << "video av_interleaved_write_frame failed!, errorId: " << errorId;
        } else {
            qDebug() << "Write video packet id: " << ++g_vOutCnt;
        }

        av_packet_unref(pkt);
    }

}

void ScreenRecord::flushAudioDecoder()
{
//    AVPacket* pkt = av_packet_alloc();
//    av_init_packet(pkt);

    int dstNbSamples, maxDstNbSamples;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = allocAudioFrame(m_aOutEncodeCtx, m_nbSamples);
    maxDstNbSamples = static_cast<int>(av_rescale_rnd(m_nbSamples, m_aOutEncodeCtx->sample_rate,
                                       m_aInDecodeCtx->sample_rate, AV_ROUND_UP));
    dstNbSamples = maxDstNbSamples;

    int errorId = avcodec_send_packet(m_aInDecodeCtx, nullptr);
    if (errorId != 0) {
        qDebug() << "flush audio avcodec_send_packet failed, errorId: " << errorId;
        return;
    }

    while (errorId > 0) {
        errorId = avcodec_receive_frame(m_aInDecodeCtx, oldFrame);
        if (errorId < 0) {
            if (AVERROR(EAGAIN) == errorId) {
                qDebug() << "flush audio EAGAIN avcodec_receive_frame";
                errorId = 1;
                continue;
            } else if (errorId == AVERROR_EOF) {
                qDebug() << "flush audio decoder finished!";
                break;
            }
            qDebug() << "flush audio avcodec_receive_frame error, errorId: " << errorId;
            return;
        }
        g_aInCnt++;

        dstNbSamples = static_cast<int>(av_rescale_rnd(swr_get_delay(m_swrCtx, m_aInDecodeCtx->sample_rate) + oldFrame->nb_samples,
                                                                     m_aOutEncodeCtx->sample_rate, m_aInDecodeCtx->sample_rate,
                                                                     AV_ROUND_UP));

        if (dstNbSamples > maxDstNbSamples) {
            qDebug() << "flush audio newFrame relloc";
            av_freep(&newFrame->data[0]);

            errorId = av_samples_alloc(newFrame->data, newFrame->linesize, m_aOutEncodeCtx->channels,
                                       dstNbSamples, m_aOutEncodeCtx->sample_fmt, 1);

            if (errorId < 0) {
                qDebug() << "flush av_sample_alloc failed!";
                return;
            }

            maxDstNbSamples = dstNbSamples;
            m_aOutEncodeCtx->frame_size = dstNbSamples;
            m_nbSamples = newFrame->nb_samples;
        }

        newFrame->nb_samples = swr_convert(m_swrCtx, newFrame->data, dstNbSamples,
                                           const_cast<const uint8_t **>(oldFrame->data), oldFrame->nb_samples);

        if (newFrame->nb_samples < 0) {
            qDebug() << "flush swr_convert failed!";
            return;
        }

        std::unique_lock<std::mutex> mutex(m_mtxAudio);
        m_aInNotFull.wait(mutex, [newFrame, this] {
            return av_audio_fifo_space(m_aFifoBuf) >= newFrame->nb_samples;
        });

        if (av_audio_fifo_write(m_aFifoBuf, reinterpret_cast<void **>(newFrame->data), newFrame->nb_samples) < newFrame->nb_samples) {
            qDebug() << "flush av_audio_fifo_write failed!";
            return;
        }
        m_aOutNotEmpty.notify_one();
    }
    qDebug() << "audio collect frame count: " << g_aInCnt;
}

void ScreenRecord::flushEncoders()
{
    bool aFlush = false;
    bool vFlush = false;

    m_vCurPts = m_aCurPts = 0;
    int errorId = -1;
    int nFlush = 2;
    while (1) {
        AVPacket* pkt = av_packet_alloc();
        av_init_packet(pkt);

        if (av_compare_ts(m_vCurPts, m_vOutFmtCtx->streams[m_vOutIndex]->time_base,
                          m_aCurPts, m_vOutFmtCtx->streams[m_aOutIndex]->time_base) < 0) {
            if (!vFlush) {
                vFlush = true;
                errorId = avcodec_send_frame(m_vOutEncodeCtx, nullptr);
                if (errorId != 0) {
                    qDebug() << "flush viedo avcode_send_frame failed!, errorId: " << errorId;
                    return;
                }
            }
            errorId = avcodec_receive_packet(m_vOutEncodeCtx, pkt);
            if (errorId < 0) {
                av_packet_unref(pkt);
                if (AVERROR(EAGAIN) == errorId) {
                    qDebug() << "flush video EAGAIN avcodec_receive_packet";
                    errorId = 1;
                    continue;
                } else if(AVERROR_EOF == errorId) {
                    qDebug() << "flush video encoder finished!";
                    if (!(--nFlush)) {
                        break;
                    }
                    m_vCurPts = INT_MAX;
                    continue;
                }
                return;
            }

            pkt->stream_index = m_vOutIndex;
            av_packet_rescale_ts(pkt, m_vOutEncodeCtx->time_base,
                                 m_vOutFmtCtx->streams[m_vOutIndex]->time_base);
            m_vCurPts = pkt->pts;
            qDebug() << "m_vCurPts: " << m_vCurPts;

            errorId = av_interleaved_write_frame(m_vOutFmtCtx, pkt);
            if (errorId != 0) {
                qDebug() << "video av_interleaved_write_frame failed!, errorId: " << errorId;
            } else {
                qDebug() << "Write video packet id: " << ++g_vOutCnt;
            }

            av_packet_unref(pkt);
        } else {
            if (!aFlush) {
                aFlush = true;
                errorId = avcodec_send_frame(m_aOutEncodeCtx, nullptr);
                if (errorId != 0) {
                    qDebug() << "flush audio avcodec_send_frame failed, errorId: " << errorId;
                    return;
                }
            }

            errorId = avcodec_receive_packet(m_aOutEncodeCtx, pkt);
            if (errorId < 0) {
                av_packet_unref(pkt);
                if (AVERROR(EAGAIN) == errorId) {
                    qDebug() << "flush EAGAIN avcodec_receive_packet";
                    errorId = 1;
                    continue;
                } else if (AVERROR_EOF == errorId) {
                    qDebug() << "flush audio encoder finished";
                    if (!(--nFlush)) {
                        break;
                    }
                    m_aCurPts = INT_MAX;
                    continue;
                }
                return;
            }

            pkt->stream_index = m_aOutIndex;
            av_packet_rescale_ts(pkt, m_aOutEncodeCtx->time_base,
                                 m_vOutFmtCtx->streams[m_aOutIndex]->time_base);
            m_aCurPts = pkt->pts;
			qDebug() << "m_aCurPts: " << m_aCurPts;

            errorId = av_interleaved_write_frame(m_vOutFmtCtx, pkt);
            if (errorId != 0) {
                qDebug() << "video av_interleaved_write_frame failed!, errorId: " << errorId;
            } else {
                qDebug() << "Write video packet id: " << ++g_vOutCnt;
            }

            av_packet_unref(pkt);
        }
    }

}

// 清除数据
void ScreenRecord::release()
{
    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~0" << __FILE__ << __func__;
    av_frame_free(&m_outFrame);
    av_free(m_outFrameBuf);

    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~1" << __FILE__ << __func__;
    if (m_vInDecodeCtx) {
        avcodec_free_context(&m_vInDecodeCtx);
        m_vInDecodeCtx = nullptr;
    }

    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~2" << __FILE__ << __func__;
    if (m_vOutEncodeCtx) {
        avcodec_free_context(&m_vOutEncodeCtx);
        m_vOutEncodeCtx = nullptr;
    }
    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~3" << __FILE__ << __func__;

    if (m_aInDecodeCtx) {
        avcodec_free_context(&m_aInDecodeCtx);
        m_aInDecodeCtx = nullptr;
    }
    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~4" << __FILE__ << __func__;

    if (m_aOutEncodeCtx) {
        avcodec_free_context(&m_aOutEncodeCtx);
        m_aOutEncodeCtx = nullptr;
    }

    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~5" << __FILE__ << __func__;
    if (m_vFifoBuf) {
        av_fifo_freep(&m_vFifoBuf);
        m_vFifoBuf = nullptr;
    }

    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~6" << __FILE__ << __func__;
    if (m_aFifoBuf) {
        av_audio_fifo_free(m_aFifoBuf);
        m_aFifoBuf = nullptr;
    }

    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~7" << __FILE__ << __func__;
    if (m_vInFmtCtx) {
        avformat_close_input(&m_vInFmtCtx);
        m_vInFmtCtx = nullptr;
    }

    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~8" << __FILE__ << __func__;
    if (m_aInFmtCtx) {
        avformat_close_input(&m_aInFmtCtx);
        m_aInFmtCtx = nullptr;
    }

    avio_close(m_vOutFmtCtx->pb);
    avformat_free_context(m_vOutFmtCtx);

    m_vCurPts = 0;
    m_aCurPts = 0;
    m_outFrameSize = 0;
    m_state = RecordState::None;
    qDebug() << "!!!!!!!!!!!!!!!!!!!!!~~~~~~~~~~~~~~~  end release" << __func__;
}

// 初始化音频frame属性
AVFrame *ScreenRecord::allocAudioFrame(AVCodecContext* cContext, int nbSamples)
{
    AVFrame* frame = av_frame_alloc();

    frame->format = cContext->sample_fmt;
    frame->channel_layout = cContext->channel_layout ? cContext->channel_layout : AV_CH_LAYOUT_STEREO;
    frame->sample_rate = cContext->sample_rate;
    frame->nb_samples = nbSamples;

    if (nbSamples) {
        int errorId = av_frame_get_buffer(frame, 0);
        if (errorId < 0) {
            qDebug() << "av_frame_get_buffer failed!";
            return nullptr;
        }
    }
    return frame;
}

void ScreenRecord::setFilePath(const QString str)
{
    m_filePath = str;
}


void ScreenRecord::start()
{
    qDebug() << "------------------------- start";
    if (RecordState::None == m_state) {
        // 开始录制
        m_state = RecordState::Started;
        std::thread rThread(&ScreenRecord::recordThread, this);
        rThread.detach();
    } else if (m_state == RecordState::Paused) {
        qDebug() << "continue record";
        m_state = RecordState::Started;
        m_cvNotPause.notify_one();
    }
    qDebug() << "------------------------- start  end" << m_state;
}

void ScreenRecord::stop()
{
    RecordState state = m_state;
    m_state = RecordState::Stopped;
    if (state == RecordState::Paused)
        m_cvNotPause.notify_one();
    else if (state == RecordState::Paused)
        m_state = RecordState::None;

}


