#pragma once

#include <string>
#include <WinSock2.h>

class RtspSession
{
public:
	RtspSession();
	~RtspSession();

	void doConversation(SOCKET s, std::string data, std::string clntIp);

private:
	static void conversationFunc(SOCKET s, std::string data);
	static int handleOptionReq(char* result, int cseq);
	static int handleDescribeReq(char* result, int cseq, char *url);
	static int handleSetupReq(char* result, int cseq, int clientRtpPort);
	static int handlePlayReq(char* result, int cseq);
	static int sendRtpFrame();

private:

};
