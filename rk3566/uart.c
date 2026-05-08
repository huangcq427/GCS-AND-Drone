#include "uart.h"
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>

// 改为全局变量，供main.c访问
int uart_fd = -1;

void uart_init(void) {
    uart_fd = open(UART_DEV, O_RDWR | O_NOCTTY | O_NONBLOCK);  // 添加非阻塞模式
    if (uart_fd < 0) {
        perror("UART open failed");
        return;
    }

    struct termios opt;
    tcgetattr(uart_fd, &opt);

    cfsetispeed(&opt, UART_BAUD);
    cfsetospeed(&opt, UART_BAUD);

    opt.c_cflag = CS8 | CLOCAL | CREAD;
    opt.c_iflag = 0;
    opt.c_oflag = 0;
    opt.c_lflag = 0;

    tcsetattr(uart_fd, TCSANOW, &opt);
    printf("UART initialized\n");
}

void uart_send_rtf_mode(void) {
    const char *cmd = "MODE 4\n";
    (void)write(uart_fd, cmd, strlen(cmd));  
    printf("Sent RTF mode to flight controller\n");
}