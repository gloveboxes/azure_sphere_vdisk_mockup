#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char* _log_debug_buffer = NULL;
static size_t _log_debug_buffer_size;


char* uint8_to_binary(uint8_t bitmap, char* buffer, size_t buffer_length)
{
	uint16_t mask = 1;
	uint8_t bit_number = 8;

	if (buffer_length < 9) { return NULL; }

	while (bit_number-- > 0)
	{
		buffer[bit_number] = bitmap & mask ? '1' : '0';
		mask <<= 1;
	}

	buffer[8] = 0x00;
	return buffer;
}

void delay(int ms)
{
	//#ifdef SHOW_DEBUG_MSGS
	//	Log_Debug(">>> %s\n", __func__);
	//#endif

	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

static char ascBuff[17];
void DisplayLineOffset(uint16_t offset);

void Log_Debug_Time_Init(size_t log_debug_buffer_size) {
	_log_debug_buffer = (char*)malloc(log_debug_buffer_size);
	_log_debug_buffer_size = log_debug_buffer_size;
}

void Log_Debug_Time_Free(void) {
	if (_log_debug_buffer != NULL) {
		free(_log_debug_buffer);
		_log_debug_buffer = NULL;
	}
}

void Log_Debug(char* fmt, ...) {
	if (_log_debug_buffer == NULL) {
		printf("log_debug_buffer is NULL. Call Log_Debug_Time_Init first");
		return;
	}
	va_list args;
	va_start(args, fmt);
	vsnprintf(_log_debug_buffer, _log_debug_buffer_size, fmt, args);
	va_end(args);
	printf("%s", _log_debug_buffer);
}


bool lp_startThreadDetached(void* (daemon)(void*), void* arg, char* daemon_name) {
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_t thread;


	if (pthread_create(&thread, &attr, daemon, arg)) {
		Log_Debug("ERROR: Failed to start %s daemon.\n", daemon_name);
		return false;
	}
	return true;
}

int64_t lp_getNowMilliseconds(void) {
	struct timespec now = { 0,0 };
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}