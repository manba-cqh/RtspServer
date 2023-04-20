#pragma once

#define BUF_MAX_SIZE (1024*1024)
#define SERVER_RTP_PORT 55532
#define SEND_BUFFER_SIZE 1024

enum RTSP_OPTIONS
{
	RTSP_NONE = -1,
	RESP_OPTION,
	RTSP_DESCRIBE,
	RTSP_SETUP,
	RTSP_PLAY,
};