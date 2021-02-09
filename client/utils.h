#ifndef _UTILS_H_
#define _UTILS_H_

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

// #define SHOW_DEBUG_MSGS 1

#define LOG_DEBUG(x) prinft(x)

#define FLOAT_TO_INT(x) ((x)>=0?(int)((x)+0.5):(int)((x)-0.5))


char* uint8_to_binary(uint8_t bitmap, char* buffer, size_t buffer_length);
void delay(int ms);
bool lp_startThreadDetached(void* (daemon)(void*), void* arg, char* daemon_name);
void Log_Debug(char* fmt, ...);
void Log_Debug_Time_Init(size_t log_debug_buffer_size);
void Log_Debug_Time_Free(void);
int64_t lp_getNowMilliseconds(void);

typedef void(*callback)(void);

#endif
