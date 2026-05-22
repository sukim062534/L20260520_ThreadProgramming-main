#define _WINSOCK_DEPRECATED_NO_WARNINGS


#include "ChatPacket.h"
#include "NetUtil.h"

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <process.h>
#include <conio.h>
#include "SDL.h"
#include <mutex>
#include <queue>

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")
#pragma comment(lib, "SDL2")
#pragma comment(lib, "SDL2main")


using namespace std;

char SendBuffer[1024] = { 0, };
char RecvBuffer[1024] = { 0, };

bool IsRecvThreadRunning = true;
bool IsSendThreadRunning = true;

//ActorList
SessionManager MySessionManager;
SOCKET MyClientID;

SDL_Window* MyWindow;
SDL_Renderer* MyRenderer;


std::mutex SessionLock;


void Render();
void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer, const Header& InHeader);
unsigned WINAPI RecvThread(void* Argument);
unsigned WINAPI SendThread(void* Argument);

//queue
std::queue<int> KeyBuffer;
//KeyBuffer -> PacketBuffer


int SDL_main(int Argc, char* Argv[])
{
	//Object 동기화(Lock, Lockfree)
	//GameThread(Render)
	//NetworkThread

	std::cout << "client " << endl;

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SDL_Init(SDL_INIT_EVERYTHING);
	MyWindow = SDL_CreateWindow("SDL", 100, 100, 640, 480, SDL_WINDOW_OPENGL);
	MyRenderer = SDL_CreateRenderer(MyWindow, -1, SDL_RENDERER_ACCELERATED || SDL_RENDERER_PRESENTVSYNC || SDL_RENDERER_TARGETTEXTURE);


	SOCKET ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ServerSockAddr;
	memset(&ServerSockAddr, 0, sizeof(ServerSockAddr));
	ServerSockAddr.sin_family = AF_INET;
	ServerSockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	ServerSockAddr.sin_port = htons(35000);

	connect(ServerSocket, (SOCKADDR*)&ServerSockAddr, sizeof(ServerSockAddr));

	std::cout << "client connect" << endl;

	C2S_Login LoginData;
	LoginData.UserID = "junios";
	LoginData.HashKey = "1as3f356dsd6gyhg";

	Header LoginHeader;
	LoginHeader.MakeHeader(static_cast<unsigned short>(LoginData.ToString().length()), EPacketType::C2S_Login);

	//Login 요청
	if (SendAll(ServerSocket, (char*)&LoginHeader, HeaderSize) <= 0)
	{
		std::cout << "login header Error" << endl;
	}

	if (SendAll(ServerSocket, LoginData.ToString().c_str(), (int)LoginData.ToString().length()) <= 0)
	{
		std::cout << "login data Error" << endl;
	}

	HANDLE ThreadHandles[2] = { 0, };

	ThreadHandles[0] = (HANDLE)_beginthreadex(0, 0, RecvThread, &ServerSocket, 0, 0);
	ThreadHandles[1] = (HANDLE)_beginthreadex(0, 0, SendThread, &ServerSocket, 0, 0);


	const Uint8* KeyState = SDL_GetKeyboardState(NULL);

	while (true)
	{
		SDL_Event MyEvent;
		SDL_PollEvent(&MyEvent);
		if (MyEvent.type == SDL_QUIT)
		{
			IsRecvThreadRunning = false;
			IsSendThreadRunning = false;
			break;
		}
		else if (MyEvent.type == SDL_KEYDOWN)
		{
			if (KeyState[SDL_SCANCODE_ESCAPE])
			{
				IsRecvThreadRunning = false;
				IsSendThreadRunning = false;
				break;
			}
			int KeyCode = 0;
			if (KeyState[SDL_SCANCODE_W])
			{
				KeyBuffer.push('W');
			}
			if (KeyState[SDL_SCANCODE_S])
			{
				KeyBuffer.push('S');
			}
			if (KeyState[SDL_SCANCODE_A])
			{
				KeyBuffer.push('A');
			}
			if (KeyState[SDL_SCANCODE_D])
			{
				KeyBuffer.push('D');
			}
		}

		Render();
	}

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

	SDL_DestroyWindow(MyWindow);
	SDL_Quit();

	return 0;
}


void Render()
{
	//system("cls");

	//for (auto Player : MySessionManager.SessionList)
	//{
	//	COORD Where;
	//	Where.X = Player.X;
	//	Where.Y = Player.Y;
	//	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), Where);
	//	std::cout << (char)Player.Shape << endl;
	//}


	SDL_SetRenderDrawColor(MyRenderer, 0, 0, 0, 0);
	SDL_RenderClear(MyRenderer);

	{
		lock_guard<std::mutex> lock(SessionLock);
		//SessionLock.lock();
		for (auto Player : MySessionManager.SessionList)
		{
			SDL_SetRenderDrawColor(MyRenderer, Player.R, Player.G, Player.B, 0);
			SDL_Rect MyRect = { Player.X, Player.Y, 30, 30 };
			SDL_RenderFillRect(MyRenderer, &MyRect);
		}
		//SessionLock.unlock();
	}


	SDL_RenderPresent(MyRenderer);

}

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer, const Header& InHeader)
{
	switch ((EPacketType)InHeader.PacketType)
	{
	case EPacketType::S2C_Login:
	{
		S2C_Login LoginPacket;
		LoginPacket.Parse(InBuffer);
		//std::cout << LoginPacket.ToString() << endl;
		MyClientID = LoginPacket.ClientSocketID;
	}
	break;
	case EPacketType::S2C_Spawn:
	{
		S2C_Spawn SpawnData;
		SpawnData.Parse(InBuffer);
		//std::cout << SpawnData.ToString() << endl;

		Session InSession;
		InSession.ClientSocket = SpawnData.ClientSocket;
		InSession.Shape = SpawnData.Shape;
		InSession.X = SpawnData.X;
		InSession.Y = SpawnData.Y;
		InSession.R = SpawnData.R;
		InSession.G = SpawnData.G;
		InSession.B = SpawnData.B;

		{
			lock_guard<std::mutex> lock(SessionLock);
			MySessionManager.Add(InSession);
		}
		//		Render();
	}
	break;
	case EPacketType::S2C_Move:
	{
		S2C_Move MoveData;
		MoveData.Parse(InBuffer);
		Session* FindSession = MySessionManager.GetSession(MoveData.ClientSocket);
		FindSession->X = MoveData.X;
		FindSession->Y = MoveData.Y;

		//std::cout << MoveData.ToString() << endl;
//		Render();
	}
	break;
	case EPacketType::S2C_Destroy:
	{
		S2C_Destroy DestroyPacket;
		DestroyPacket.Parse(InBuffer);

		Session* FindSession = MySessionManager.GetSession(DestroyPacket.ClientSocket);

		//std::cout << "Quit : " << FindSession->ClientSocket << endl;

		{
			lock_guard<std::mutex> lock(SessionLock);
			MySessionManager.Delete(*FindSession);
		}
		//		Render();
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
			std::cout << "header recv fail " << endl;
			break;
		}

		DataHeader.NetworkToHost();

		memset(RecvBuffer, 0, sizeof(RecvBuffer));
		//data JSON
		RecvBytes = RecvAll(ServerSocket, RecvBuffer, DataHeader.PacketSize);
		if (RecvBytes <= 0)
		{
			std::cout << "Data recv fail " << endl;
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

		if (KeyBuffer.empty())
		{
			Sleep(0);
			continue;
		}

		C2S_Move MoveData;
		MoveData.ClientSocket = MyClientID;
		MoveData.Direction = KeyBuffer.front();
		KeyBuffer.pop();

		//header
		Header DataHeader;
		DataHeader.MakeHeader((int)(MoveData.ToString().length()), EPacketType::C2S_Move);
		int SentBytes = SendAll(ServerSocket, (char*)&DataHeader, HeaderSize);
		if (SentBytes <= 0)
		{
			std::cout << "header send fail." << endl;
		}

		//Data
		SentBytes = SendAll(ServerSocket, MoveData.ToString().c_str(), (int)(MoveData.ToString().length()));
		if (SentBytes <= 0)
		{
			std::cout << "Data send fail." << endl;
		}


	}

	return 0;
}