#ifndef SCRMIRROR_DECODE
#define SCRMIRROR_DECODE

#include <zephyr/kernel.h>

#define SOCKET_RECV_SIZE KB(256)

enum thread_status {
	THREAD_RUN,
	THREAD_STOP,
};

void decode_start(int *socket);
void decode_stop();
#endif
