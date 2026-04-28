#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QImage>
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#define VIDEO_RECV_PORT 8000
#define RK_IP "192.168.3.95"
#define CMD_SEND_PORT 8001

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

    m_hasSPS = false;
    m_hasPPS = false;
    m_canShow = false;

    // ========== UI ==========
    m_videoLabel = new QLabel;
    m_videoLabel->setStyleSheet("background:#000;color:#fff;");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setText("等待视频流...");

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

  // ========== UDP ==========
m_udpRecv = new QUdpSocket(this);
if (!m_udpRecv->bind(VIDEO_RECV_PORT, QUdpSocket::ShareAddress)) {
    QMessageBox::warning(this, "警告", "视频端口绑定失败！");
}
connect(m_udpRecv, &QUdpSocket::readyRead, this, &MainWindow::onUdpRecv);

    // ========== FFmpeg ==========
    initFFmpeg();

    qDebug("===== 地面站已启动 =====");
    qDebug("UDP端口：%d", VIDEO_RECV_PORT);
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
        m_udpRecv->readDatagram(data.data(), data.size());

        qDebug("\n=====================================");
        qDebug("【UDP】收到数据包：%d 字节", (int)data.size());

        processH264Data(data);
    }
}

void MainWindow::processH264Data(const QByteArray& data)
{
    if (!m_codecCtx) return;

    m_h264Buffer.append(data);

    // 防溢出
    if (m_h264Buffer.size() > 2 * 1024 * 1024) {
        m_h264Buffer.clear();
    }

    // --------------------------
    // 【终极算法】同时查找 00 00 01 和 00 00 00 01
    // --------------------------
    QList<int> starts;
    int pos = 0;
    while (pos < m_h264Buffer.size() - 3) {
        if (m_h264Buffer[pos] == 0 && m_h264Buffer[pos+1] == 0 && m_h264Buffer[pos+2] == 1) {
            starts << pos;
            pos += 3;
        }
        else if (m_h264Buffer[pos] == 0 && m_h264Buffer[pos+1] == 0 && m_h264Buffer[pos+2] == 0 && m_h264Buffer[pos+3] == 1) {
            starts << pos;
            pos += 4;
        }
        else {
            pos++;
        }
    }

    if (starts.size() < 2) return;

    int start = starts[0];
    int next = starts[1];

    QByteArray nalu = m_h264Buffer.mid(start, next - start);
    m_h264Buffer = m_h264Buffer.mid(next);

    if (nalu.size() > 0) {
        qDebug() << "【✅ 切割NALU成功】长度：" << nalu.size();
        decodeH264(nalu);

        if (m_isSaving && m_saveFile.isOpen()) {
            m_saveFile.write(nalu);
        }
    }
}

void MainWindow::decodeH264(const QByteArray& data)
{
    if (!m_codecCtx || !m_pkt || !m_frame) return;
    if (data.size() < 5) return;

    qDebug("=====================================");
    qDebug("【NALU】长度：%d 字节", (int)data.size());

    // --------------------------
    // 正确查找起始码位置
    // --------------------------
    int pos = 0;
    int naluTypePos = -1;

    while (pos < data.size() - 3) {
        // 找到 00 00 01
        if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) {
            naluTypePos = pos + 3;
            break;
        }
        // 找到 00 00 00 01
        if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) {
            naluTypePos = pos + 4;
            break;
        }
        pos++;
    }

    if (naluTypePos <= 0 || naluTypePos >= data.size()) {
        qDebug("❌ 找不到有效起始码");
        return;
    }

    // --------------------------
    // 正确取 NALU 类型
    // --------------------------
    uint8_t naluType = data[naluTypePos] & 0x1F;
    qDebug("✅ 真正NALU类型 = %d", naluType);

    if (naluType == 7) {
        qDebug("===================== SPS 已收到 =====================");
        m_hasSPS = true;
    }
    if (naluType == 8) {
        qDebug("===================== PPS 已收到 =====================");
        m_hasPPS = true;
    }
    if (naluType == 5) {
        qDebug("===================== I 帧已收到 =====================");
        m_canShow = true;
    }

    qDebug("状态：SPS=%d | PPS=%d | I帧=%d | 可显示=%d",
           m_hasSPS, m_hasPPS, m_canShow,
           (m_hasSPS && m_hasPPS && m_canShow));

    if (!m_hasSPS || !m_hasPPS || !m_canShow) {
        m_videoLabel->setText("等待关键帧(SPS+PPS+I)...");
        return;
    }

    m_videoLabel->setText("");

    // --------------------------
    // 正常解码
    // --------------------------
    av_packet_unref(m_pkt);
    m_pkt->data = (uint8_t*)data.constData();
    m_pkt->size = data.size();

    int ret = avcodec_send_packet(m_codecCtx, m_pkt);
    if (ret < 0) {
        qDebug("解码失败：send packet");
        return;
    }

    while (avcodec_receive_frame(m_codecCtx, m_frame) == 0) {
        int w = m_frame->width;
        int h = m_frame->height;
        qDebug("===================== 解码成功：%dx%d =====================", w, h);

        if (!m_sws || m_lastW != w || m_lastH != h) {
            sws_freeContext(m_sws);
            m_sws = sws_getContext(w, h, m_codecCtx->pix_fmt,
                                   w, h, AV_PIX_FMT_BGR24,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
            m_lastW = w;
            m_lastH = h;
        }

        QImage rgbImg(w, h, QImage::Format_BGR888);
        uint8_t* dst[] = { rgbImg.bits() };
        int lines[] = { (int)rgbImg.bytesPerLine() };

        sws_scale(m_sws,
                  m_frame->data,
                  m_frame->linesize,
                  0, h,
                  dst, lines);

        QMetaObject::invokeMethod(m_videoLabel, [=]() {
            m_videoLabel->setPixmap(
                QPixmap::fromImage(rgbImg).scaled(
                    m_videoLabel->size(),
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation));
        });

        av_frame_unref(m_frame);
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
    m_btnSave->setText("停止保存");

    disconnect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStartSave);
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStopSave);
}

void MainWindow::btnStopSave()
{
    if (!m_isSaving) return;

    m_saveFile.close();
    m_isSaving = false;
    m_btnSave->setText("开始保存视频");

    disconnect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStopSave);
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::btnStartSave);
}