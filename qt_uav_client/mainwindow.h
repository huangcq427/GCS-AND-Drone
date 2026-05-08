#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QUdpSocket>
#include <QMap>
#include <QFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#define VIDEO_RECV_PORT 8000
#define RK_IP           "192.168.3.95"
#define CMD_SEND_PORT   8001

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onUdpRecv();
    void btnSendRtf();
    void btnStartSave();
    void btnStopSave();

private:
    void initFFmpeg();
    void processVideoPacket(const QByteArray& packet);
    void decodeH264(const QByteArray& data);
    void showCustomMessageBox(const QString &title, const QString &text);

private:
    QLabel         *m_videoLabel;
    QPushButton    *m_btnRtf;
    QPushButton    *m_btnSave;

    QUdpSocket     *m_udpRecv;
    QUdpSocket     *m_udpSend;

    bool            m_isSaving;
    QFile           m_h264File;

    AVCodec        *m_codec;
    AVCodecContext *m_codecCtx;
    AVPacket       *m_pkt;
    AVFrame        *m_frame;
    SwsContext     *m_sws;
    int             m_lastW;
    int             m_lastH;

    struct FrameBuf {
        QByteArray data;
        int totalSlice = 0;
        int recvSlice  = 0;
    };
    QMap<uint32_t, FrameBuf> m_frameBufMap;
};

#endif