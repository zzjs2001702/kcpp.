#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <random>
#include <string>

#ifndef _WIN32 
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <sys/time.h>
	#include <unistd.h>
#else
	#include <WinSock2.h>
	#include <WS2tcpip.h>
	#include <time.h>
#endif

#include "../kcpp.h"


using kcpp::KcpSession;

#define SERVER_PORT 6666

#define SND_BUFF_LEN 200
#define RCV_BUFF_LEN 1500

// if u modify this `PRACTICAL_CONDITION`,
// u have to update this var of the client side to have the same value.
#define PRACTICAL_CONDITION 1



#ifdef WIN32
inline int
gettimeofday(struct timeval *tp, void *tzp)
{
	time_t clock;
	struct tm tm;
	SYSTEMTIME wtm;
	GetLocalTime(&wtm);
	tm.tm_year = wtm.wYear - 1900;
	tm.tm_mon = wtm.wMonth - 1;
	tm.tm_mday = wtm.wDay;
	tm.tm_hour = wtm.wHour;
	tm.tm_min = wtm.wMinute;
	tm.tm_sec = wtm.wSecond;
	tm.tm_isdst = -1;
	clock = mktime(&tm);
	tp->tv_sec = static_cast<long>(clock);
	tp->tv_usec = wtm.wMilliseconds * 1000;
	return (0);
}
#endif

IUINT32 iclock()
{
	long s, u;
	IUINT64 value;

	struct timeval time;
	gettimeofday(&time, NULL);
	s = time.tv_sec;
	u = time.tv_usec;

	value = ((IUINT64)s) * 1000 + (u / 1000);
	return (IUINT32)(value & 0xfffffffful);
}

float GetRandomFloatFromZeroToOne()
{
	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_real_distribution< float > dis(0.f, 1.f);
	return dis(gen);
}

void udp_output(const void *buf, int len, int fd, struct sockaddr_in* dst)
{
	sendto(fd, (const char*)buf, len, 0, (struct sockaddr*)dst, sizeof(*(struct sockaddr*)dst));
}

bool isSimulatingPackageLoss = false;
float simulatePackageLossRate = 0.3f; // simulate package loss rate 30%
kcpp::UserInputData udp_input(char* buf, int len, int fd, struct sockaddr_in* from)
{
	socklen_t fromAddrLen = sizeof(*from);
	int recvLen = ::recvfrom(fd, buf, len, 0,
		(struct sockaddr*)from, &fromAddrLen);
	if (recvLen > 0)
	{
		isSimulatingPackageLoss = 
			GetRandomFloatFromZeroToOne() < simulatePackageLossRate ? true : false;
		if (isSimulatingPackageLoss)
		{
			// printf("server: simulate package loss!!\n");
			buf = nullptr;
			recvLen = 0;
		}
	}
	return kcpp::UserInputData(buf, recvLen);
}

void error_pause()
{
	printf("press any key to quit ...\n");
	char ch; scanf("%c", &ch);
}

void handle_udp_msg(int fd)
{
	char sndBuf[SND_BUFF_LEN];
	char rcvBuf[RCV_BUFF_LEN];

	// we can't use char array, cause we don't know how big the recv_data is
	kcpp::Buf kcppRcvBuf;

	struct sockaddr_in clientAddr;  //clent_addr????????????????????????????????????
	static const uint32_t kInitIndex = 11;
	uint32_t nextRcvIndex = kInitIndex;
	uint32_t curRcvIndex = kInitIndex;
	int len = 0;
	uint32_t rcvedIndex = 0;
	IUINT32 startTs = 0;
	int64_t nextKcppUpdateTs = 0;
	int64_t nextSendTs = 0;

	KcpSession kcppServer(
		kcpp::RoleTypeE::kSrv,
		std::bind(udp_output, std::placeholders::_1, std::placeholders::_2, fd, &clientAddr),
		std::bind(udp_input, rcvBuf, RCV_BUFF_LEN, fd, &clientAddr),
		std::bind(iclock));


#if !PRACTICAL_CONDITION

	static const int64_t kSendInterval = 0;
	const uint32_t testPassIndex = 66666;
	kcppServer.SetConfig(666, 1024, 1024, 4096, 1, 1, 1, 1, 0, 5);

#else

	//kcppServer.SetConfig(1500, 32, 128, 128, 0, 100, 2, 0, 0, 100);
	static const int64_t kSendInterval = 50; // 20fps
	const uint32_t testPassIndex = 666;

#endif // PRACTICAL_CONDITION


	while (1)
	{
		int64_t now = static_cast<int64_t>(iclock());
		if (now >= nextKcppUpdateTs)
			nextKcppUpdateTs = kcppServer.Update();

		while (kcppServer.Recv(&kcppRcvBuf, len))
		{
			if (len < 0 && !isSimulatingPackageLoss)
			{
				printf("kcpSession Recv failed, Recv() = %d \n", len);
				error_pause();
				return;
			}
			else if (len > 0)
			{
				rcvedIndex = *(uint32_t*)(kcppRcvBuf.peek() + 0);
				kcppRcvBuf.retrieveAll();

				if (rcvedIndex <= testPassIndex)
					printf("msg from client: %d\n", (int)rcvedIndex);

				if (rcvedIndex == kInitIndex)
					startTs = iclock();

				if (rcvedIndex == testPassIndex)
					printf("\n test passes, yay! \n simulate package loss rate %f %% \n"
						" avg rtt %d ms \n cost %f secs \n"
						" now u can close me ...\n",
						(simulatePackageLossRate * 100.f), kcppServer.GetKcpInstance()->rx_srtt,
						(1.0 * (iclock() - startTs) / 1000));

				if (kcppServer.IsConnected() && rcvedIndex != nextRcvIndex)
				{
					// ???????????????????????????
					printf("ERROR index != nextRcvIndex : %d != %d, kcpServer.IsKcpConnected() = %d\n",
						(int)rcvedIndex, (int)nextRcvIndex, (kcppServer.IsConnected() ? 1 : 0));
					error_pause();
					return;
				}
				++nextRcvIndex;
			}
		}

		if (now >= nextSendTs)
		{
			nextSendTs = now + kSendInterval;
			while (curRcvIndex <= nextRcvIndex - 1)
			{
				memset(sndBuf, 0, SND_BUFF_LEN);
				((uint32_t*)sndBuf)[0] = curRcvIndex++;
				//int result = kcppServer.Send(sndBuf, SND_BUFF_LEN, kcpp::TransmitModeE::kUnreliable);
				int result = kcppServer.Send(sndBuf, SND_BUFF_LEN);
				if (result < 0)
				{
					printf("kcpSession Send failed\n");
					error_pause();
					return;
				}
			}
		}

	}
}


int main(int argc, char* argv[])
{
#ifdef _WIN32
	WSADATA  Ws;
	//Init Windows Socket
	if (WSAStartup(MAKEWORD(2, 2), &Ws) != 0)
	{
		printf("Init Windows Socket Failed");
		return -1;
	}
#endif
	srand(static_cast<uint32_t>(time(nullptr)));

	int server_fd, ret;
	struct sockaddr_in ser_addr;

	server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (server_fd < 0)
	{
		printf("create socket fail!\n");
		return -1;
	}

	memset(&ser_addr, 0, sizeof(ser_addr));
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ser_addr.sin_port = htons(SERVER_PORT);

	ret = ::bind(server_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr));
	if (ret < 0)
	{
		printf("socket bind fail!\n");
		return -1;
	}

	handle_udp_msg(server_fd);

#ifndef _WIN32
	close(server_fd);
#else
	closesocket(server_fd);
#endif // !_WIN32

	return 0;
}