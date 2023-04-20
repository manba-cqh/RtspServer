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
	void startPlay() { m_sendRtpFrame = true; }
	void stopPlay() { m_sendRtpFrame = false; }

private:
	int handleOptionReq(char* result, int cseq);
	int handleDescribeReq(char* result, int cseq, char *url);
	int handleSetupReq(char* result, int cseq, int clientRtpPort);
	int handlePlayReq(char* result, int cseq);
	int sendRtpFrame(void *obj);

private:
	bool m_sendRtpFrame;
};
