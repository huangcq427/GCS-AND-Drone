#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QImage>
#include <QDateTime>
#include <QDebug>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("无人机地面站");
    resize(900, 600);

    m_isSaving = false;
    m_sws = nullptr;
    m_codec = nullptr;
    m_codecCtx = nullptr;
    m_pkt = nullptr;
    m_frame = nullptr;
    m_lastW = 0;
    m_lastH = 0;

    // UI
    m_videoLabel = new QLabel;
    m_btnRtf  = new QPushButton("发送RTF返航指令");
    m_btnSave = new QPushButton("开始保存视频");

    QHBoxLayout* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_btnRtf);
    btnLayout->addWidget(m_btnSave);

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->addWidget(m_videoLabel);
    mainLayout->addLayout(btnLayout);

    QWidget* central = new QWidget;
    central->setLayout(mainLayout);
    setCentralWidget(central);

    // UDP 接收视频
    m_udpRecv = new QUdpSocket(this);
    if (!m_udpRecv->bind(VIDEO_RECV_PORT, QUdpSocket::ShareAddress)) {
        QMessageBox::warning(this, "警告", "视频端口绑定失败！");
    }
    connect(m_udpRecv, &QUdpSocket::readyRead, this, &MainWindow::onUdpRecv);

    // UDP 发送命令
    m_udpSend = new QUdpSocket(this);

    // 按钮
    connect(m_btnRtf, &QPushButton::clicked, this, &MainWindow::btnSendRtf);
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStartSave);

    // 初始化FFmpeg解码
    initFFmpeg();

    qDebug("===== 地面站已启动 =====");
}

MainWindow::~MainWindow()
{
    if (m_isSaving)
        btnStopSave();

    av_packet_free(&m_pkt);
    av_frame_free(&m_frame);
    avcodec_free_context(&m_codecCtx);
    sws_freeContext(m_sws);
}

void MainWindow::initFFmpeg()
{
    m_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!m_codec) {
        QMessageBox::critical(this, "错误", "未找到H264解码器");
        return;
    }

    m_codecCtx = avcodec_alloc_context3(m_codec);
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        QMessageBox::critical(this, "错误", "解码器打开失败");
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
        return;
    }

    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    qDebug("FFmpeg 初始化成功");
}

void MainWindow::onUdpRecv()
{
    while (m_udpRecv->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(m_udpRecv->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;
        m_udpRecv->readDatagram(data.data(), data.size(), &sender, &senderPort);

        if (data.startsWith("RTF_MODE")) {
            continue;
        }

        processVideoPacket(data);
    }
}

void MainWindow::processVideoPacket(const QByteArray& packet)
{
    const int HEAD_LEN = 6;
    if (packet.size() <= HEAD_LEN) return;

    // 解析包头
    uint32_t frameSeq;
    memcpy(&frameSeq, packet.data(), 4);
    frameSeq = ntohl(frameSeq);

    uint8_t sliceSeq   = (uint8_t)packet[4];
    uint8_t totalSlice = (uint8_t)packet[5];
    QByteArray sliceData = packet.mid(HEAD_LEN);

    // 组包
    FrameBuf &fb = m_frameBufMap[frameSeq];
    if (fb.data.isEmpty()) {
        fb.totalSlice = totalSlice;
        fb.recvSlice  = 0;
        fb.data.clear();
    }

    fb.data.append(sliceData);
    fb.recvSlice++;

    // 收满整帧，开始解码
    if (fb.recvSlice == fb.totalSlice) {
        qDebug() << "重组完整帧 seq:" << frameSeq 
                 << " size:" << fb.data.size();
        decodeH264(fb.data);
        m_frameBufMap.remove(frameSeq);
    }

    // 限制缓存数量，防内存暴涨
    if (m_frameBufMap.size() > 8) {
        m_frameBufMap.clear();
    }
}

void MainWindow::decodeH264(const QByteArray& data)
{
    if (!m_codecCtx || !m_pkt || !m_frame) return;

    av_packet_unref(m_pkt);
    m_pkt->data = (uint8_t*)data.constData();
    m_pkt->size = data.size();

    int ret = avcodec_send_packet(m_codecCtx, m_pkt);
    if (ret < 0) return;

    while (avcodec_receive_frame(m_codecCtx, m_frame) == 0)
    {
        int w = m_frame->width;
        int h = m_frame->height;
        if (w <= 0 || h <= 0) {
            av_frame_unref(m_frame);
            continue;
        }

        if (!m_sws || m_lastW != w || m_lastH != h) {
            sws_freeContext(m_sws);
            m_sws = sws_getContext(w, h, (AVPixelFormat)m_frame->format,
                                   w, h, AV_PIX_FMT_RGB24,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
            m_lastW = w;
            m_lastH = h;
        }

        QImage img(w, h, QImage::Format_RGB888);
        uint8_t* dest[] = { img.bits() };
        int stride[] = { (int)img.bytesPerLine() };
        sws_scale(m_sws, m_frame->data, m_frame->linesize, 0, h, dest, stride);

        QPixmap pix = QPixmap::fromImage(img);
        m_videoLabel->setPixmap(pix.scaled(m_videoLabel->size(), 
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_videoLabel->setAlignment(Qt::AlignCenter);

        av_frame_unref(m_frame);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!m_videoLabel->pixmap().isNull()) {
        QPixmap p = m_videoLabel->pixmap();
        m_videoLabel->setPixmap(p.scaled(m_videoLabel->size(), 
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void MainWindow::btnSendRtf()
{
    QByteArray cmd = "RTF_MODE";
    m_udpSend->writeDatagram(cmd, QHostAddress(RK_IP), CMD_SEND_PORT);
    QMessageBox::information(this, "指令", "已发送返航指令");
}

void MainWindow::btnStartSave()
{
    if (m_isSaving) return;

    QString name = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".h264";
    m_saveFile.setFileName(name);

    if (!m_saveFile.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "错误", "保存失败");
        return;
    }

    m_isSaving = true;
    m_btnSave->setText("停止保存视频");

    disconnect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStartSave);
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStopSave);
}

void MainWindow::btnStopSave()
{
    if (!m_isSaving) return;
    m_saveFile.close();
    m_isSaving = false;
    m_btnSave->setText("开始保存视频");

    disconnect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStartSave);
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStartSave);
}