#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket>
#include <QLabel>
#include <QPushButton>
#include <QFile>
#include <QByteArray>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onUdpRecv();
    void btnSendRtf();
    void btnStartSave();
    void btnStopSave();

private:
    void initFFmpeg();
    void decodeH264(const QByteArray& data);
    void processH264Data(const QByteArray& data);

    // UI
    QLabel* m_videoLabel;
    QPushButton* m_btnRtf;
    QPushButton* m_btnSave;

    // UDP
    QUdpSocket* m_udpRecv;
    QUdpSocket* m_udpSend;

    // FFmpeg
    AVCodec* m_codec;
    AVCodecContext* m_codecCtx;
    AVPacket* m_pkt;
    AVFrame* m_frame;
    SwsContext* m_sws;
    int m_lastW, m_lastH;

    // 视频状态
    QByteArray m_h264Buffer;
    bool m_hasSPS;
    bool m_hasPPS;
    bool m_canShow;

    // 保存
    bool m_isSaving;
    QFile m_saveFile;
};

#endif // MAINWINDOW_H