#pragma once

// 全局变量声明（用于main.c访问）
extern int uart_fd;

void uart_init(void);
void uart_send_rtf_mode(void);