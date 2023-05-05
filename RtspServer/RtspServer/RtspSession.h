#pragma once

#include <string>
#include <WinSock2.h>

#include "RtspDef.h"

class RtspSession
{
public:
	RtspSession();
	~RtspSession();

	std::string doConversation(std::string data, std::string clntIp, RTSP_OPTIONS &rtspOption);
	void setPlayingStatus(PLAY_STATUS status) { m_playingStatus = status; }

private:
	int handleOptionReq(char* result, int cseq);
	int handleDescribeReq(char* result, int cseq, char *url);
	int handleSetupReq(char* result, int cseq, int clientRtpPort);
	int handlePlayReq(char* result, int cseq);
	int handlePauseReq(char* result, int cseq);
	int handleTeardownReq(char* result, int cseq);

	int sendRtpFrame(void *obj);

private:
	PLAY_STATUS m_playingStatus;

	SOCKET m_rtpUdpSockForVideo;
	SOCKET m_rtpUdpSockForAudio;
	SOCKET m_rtcpUdpSockForVideo;
	SOCKET m_rtcpUdpSockForAudio;

	std::string m_clientIp;

	int m_clientRtpUdpPortForVideo;
	int m_clientRtcpDupPortForVideo;
	int m_clientRtpUdpPortForAudio;
	int m_clientRtcpDupPortForAudio;

	bool m_isAauthorized;
	int m_setupReqSeq;		//SETUP请求序号，第一次0、第二次1...
};
