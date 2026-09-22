#include "stm32f4xx_hal.h"
#include <sys/stat.h>

extern UART_HandleTypeDef huart2;

// 나머지 stub 함수들 (경고 제거용)
int _close(int file) { return -1; }
int _fstat(int file, struct stat *st) { st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { return 1; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { return -1; }
