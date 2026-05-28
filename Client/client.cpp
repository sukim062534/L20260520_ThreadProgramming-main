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
std::mutex KeyBufferLock;


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
	ServerSockAddr.sin_addr.s_addr = inet_addr("192.168.0.95");
	ServerSockAddr.sin_port = htons(35000);

	connect(ServerSocket, (SOCKADDR*)&ServerSockAddr, sizeof(ServerSockAddr));

	std::cout << "client connect" << endl;

	//memory(Data) -> ByteArray(char []) -> Serialize(flatbuffer)
	flatbuffers::FlatBufferBuilder SendBuilder;
	auto C2S_LoginData = UserPacket::CreateC2S_Login(
		SendBuilder,
		SendBuilder.CreateString("junios"),
		SendBuilder.CreateString("1as3f356dsd6gyhg")
	);

	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_C2S_Login,
		C2S_LoginData.Union()
	);

	SendBuilder.Finish(UserPacketData);

	SendAll(ServerSocket, SendBuilder);

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
				lock_guard<std::mutex> KeyLock(KeyBufferLock);
				KeyBuffer.push('W');
			}
			if (KeyState[SDL_SCANCODE_S])
			{
				lock_guard<std::mutex> KeyLock(KeyBufferLock);
				KeyBuffer.push('S');
			}
			if (KeyState[SDL_SCANCODE_A])
			{
				lock_guard<std::mutex> KeyLock(KeyBufferLock);
				KeyBuffer.push('A');
			}
			if (KeyState[SDL_SCANCODE_D])
			{
				lock_guard<std::mutex> KeyLock(KeyBufferLock);
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

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);


	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_S2C_Login:
	{
		MyClientID = UserPacketData->data_as_S2C_Login()->client_socket_id();
	}
	break;
	case UserPacket::PacketType_S2C_Spawn:
	{
		Session InSession;
		auto SpawnData = UserPacketData->data_as_S2C_Spawn();
		InSession.ClientSocket = SpawnData->client_socket_id();
		InSession.Shape = SpawnData->shape();
		InSession.X = SpawnData->position()->x();
		InSession.Y = SpawnData->position()->y();
		InSession.R = SpawnData->color()->r();
		InSession.G = SpawnData->color()->g();
		InSession.B = SpawnData->color()->b();

		{
			lock_guard<std::mutex> lock(SessionLock);
			MySessionManager.Add(InSession);
		}
		//		Render();
	}
	break;
	case UserPacket::PacketType_S2C_Move:
	{
		auto MoveData = UserPacketData->data_as_S2C_Spawn();

		Session* FindSession = MySessionManager.GetSession(MoveData->client_socket_id());
		FindSession->X = MoveData->position()->x();
		FindSession->Y = MoveData->position()->y();
	}
	break;
	case UserPacket::PacketType_S2C_Destroy:
	{
		auto DestroyPacket = UserPacketData->data_as_S2C_Destroy();

		Session* FindSession = MySessionManager.GetSession(DestroyPacket->client_socket_id());
		{
			lock_guard<std::mutex> lock(SessionLock);
			MySessionManager.Delete(*FindSession);
		}
	}
	break;
	}
}



unsigned WINAPI RecvThread(void* Argument)
{
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsRecvThreadRunning)
	{
		int RecvBytes = RecvAll(ServerSocket, RecvBuffer);
		if (RecvBytes <= 0)
		{
			std::cout << "recv fail " << endl;
			break;
		}

		ProcessPacket(ServerSocket, RecvBuffer);
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
			YieldProcessor();
			//Sleep(0);
			continue;

		}
		flatbuffers::FlatBufferBuilder SendBuilder;

		flatbuffers::Offset<UserPacket::C2S_Move> C2S_MoveData;
		{
			lock_guard<std::mutex> KeyLock(KeyBufferLock);
			C2S_MoveData = UserPacket::CreateC2S_Move(
				SendBuilder,
				(uint16_t)MyClientID,
				KeyBuffer.front()
			);
			KeyBuffer.pop();
		}

		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_C2S_Move,
			C2S_MoveData.Union()
		);

		SendBuilder.Finish(UserPacketData);

		SendAll(ServerSocket, SendBuilder);
	}

	return 0;
}