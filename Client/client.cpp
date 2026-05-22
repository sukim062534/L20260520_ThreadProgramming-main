#define _WINSOCK_DEPRECATED_NO_WARNINGS


#include "ChatPacket.h"
#include "NetUtil.h"

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <process.h>
#include <conio.h>




#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")


using namespace std;

char SendBuffer[1024] = { 0, };
char RecvBuffer[1024] = { 0, };

bool IsRecvThreadRunning = true;
bool IsSendThreadRunning = true;

SessionManager MySessionManager;
SOCKET MyClientID;

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer, const Header& InHeader)
{
	switch ((EPacketType)InHeader.PacketType)
	{
	case EPacketType::S2C_Login:
		{
			S2C_Login LoginPacket;
			LoginPacket.Parse(InBuffer);
			cout << LoginPacket.ToString() << endl;
			MyClientID = LoginPacket.ClientSocketID;
		}
		break;
	case EPacketType::S2C_Spawn:
		{
			S2C_Spawn SpawnData;
			SpawnData.Parse(InBuffer);
			cout << SpawnData.ToString() << endl;

			Session InSession;
			InSession.ClientSocket = SpawnData.ClientSocket;
			InSession.Shape = SpawnData.Shape;
			InSession.X = SpawnData.X;
			InSession.Y = SpawnData.Y;

			MySessionManager.Add(InSession);
		}
		break;
	case EPacketType::S2C_Move:
		{
			S2C_Move MoveData;
			MoveData.Parse(InBuffer);
			Session* FindSession = MySessionManager.GetSession(MoveData.ClientSocket);
			FindSession->X = MoveData.X;
			FindSession->Y = MoveData.Y;

			std::cout << MoveData.ToString() << endl;
		}
		break;
	case EPacketType::S2C_Destroy:
		{
			S2C_Destroy DestroyPacket;
			DestroyPacket.Parse(InBuffer);

			Session* FindSession = MySessionManager.GetSession(DestroyPacket.ClientSocket);

			std::cout << "Quit : " << FindSession->ClientSocket << endl;

			MySessionManager.Delete(*FindSession);

		}
		break;
	}


}

unsigned WINAPI RecvThread(void* Argument)
{
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsRecvThreadRunning)
	{
		unsigned short PacketSize = 0;

		//header
		Header DataHeader;
		int RecvBytes = RecvAll(ServerSocket, (char*)&DataHeader, HeaderSize);
		if (RecvBytes <= 0)
		{
			cout << "header recv fail " << endl;
			break;
		}

		DataHeader.NetworkToHost();

		memset(RecvBuffer, 0, sizeof(RecvBuffer));
		//data JSON
		RecvBytes = RecvAll(ServerSocket, RecvBuffer, DataHeader.PacketSize);
		if (RecvBytes <= 0)
		{
			cout << "Data recv fail " << endl;
			break;
		}

		ProcessPacket(ServerSocket, RecvBuffer, DataHeader);
	}


	return 0;
}

unsigned WINAPI SendThread(void* Argument)
{
	//책임은 사용하는 놈이 진다.
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsSendThreadRunning)
	{
		int KeyCode = _getch();

		if (!(KeyCode == 'w' ||
			KeyCode == 'W' ||
			KeyCode == 'a' ||
			KeyCode == 'A' ||
			KeyCode == 's' ||
			KeyCode == 'S' ||
			KeyCode == 'd' ||
			KeyCode == 'D'))
		{
			continue;
		}


		C2S_Move MoveData;
		MoveData.ClientSocket = MyClientID;
		MoveData.Direction = KeyCode;


		//header
		Header DataHeader;
		DataHeader.MakeHeader((int)(MoveData.ToString().length()), EPacketType::C2S_Move);
		int SentBytes = SendAll(ServerSocket, (char*)&DataHeader, HeaderSize);
		if (SentBytes <= 0)
		{
			cout << "header send fail." << endl;
		}

		//Data
		SentBytes = SendAll(ServerSocket, MoveData.ToString().c_str(), (int)(MoveData.ToString().length()));
		if (SentBytes <= 0)
		{
			cout << "Data send fail." << endl;
		}
	

	}

	return 0;
}

int main()
{
	cout << "client " << endl;

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ServerSockAddr;
	memset(&ServerSockAddr, 0, sizeof(ServerSockAddr));
	ServerSockAddr.sin_family = AF_INET;
	ServerSockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	ServerSockAddr.sin_port = htons(35000);

	connect(ServerSocket, (SOCKADDR*)&ServerSockAddr, sizeof(ServerSockAddr));

	cout << "client connect" << endl;

	C2S_Login LoginData;
	LoginData.UserID = "aaa";
	LoginData.HashKey = "1asdfegsefd";

	Header LoginHeader;
	LoginHeader.MakeHeader(static_cast<unsigned short>(LoginData.ToString().length()), EPacketType::C2S_Login);

	//Login 요청
	if (SendAll(ServerSocket, (char*)&LoginHeader, HeaderSize) <= 0)
	{
		cout << "login header Error" << endl;
	}

	if ( SendAll(ServerSocket, LoginData.ToString().c_str(), (int)LoginData.ToString().length()) <= 0)
	{
		cout << "login data Error" << endl;
	}

	HANDLE ThreadHandles[2] = { 0, };

	//nonblocking, asynchrous
	ThreadHandles[0] = (HANDLE)_beginthreadex(0, 0, RecvThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	ThreadHandles[1] = (HANDLE)_beginthreadex(0, 0, SendThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	//ResumeThread(ThreadHandles[0]);
	//ResumeThread(ThreadHandles[1]);
	//SuspendThread(ThreadHandles[0]);
	//SuspendThread(ThreadHandles[1]);


	//blocking
	WaitForMultipleObjects(2, ThreadHandles, FALSE, INFINITE);

	closesocket(ServerSocket);

	cout << "End Thread" << endl;

	//TerminateThread(ThreadHandles[0], 0);
	//TerminateThread(ThreadHandles[1], 0);
	IsSendThreadRunning = false;
	IsRecvThreadRunning = false;


	CloseHandle(ThreadHandles[0]);
	CloseHandle(ThreadHandles[1]);

	WSACleanup();

	return 0;
}