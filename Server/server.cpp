#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "NetUtil.h"

#include <winsock2.h>
#include <iostream>

#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/driver.h"
#include "jdbc/cppconn/exception.h"
#include "jdbc/cppconn/resultset.h"
#include "jdbc/cppconn/statement.h"
#include "jdbc/cppconn/prepared_statement.h"

#ifndef _DEBUG
#pragma comment(lib, "mysqlcppconn")
#else
#pragma comment(lib, "mysqlcppconn.lib")
#endif //_DEBUG



#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")

using namespace std;

char Buffer[65536] = { 0, };

SessionManager MySessionManager;

sql::Driver* MyDriver; //workbench
sql::Connection* MyConnection; //접속 정보
//sql::Statement* MyStatement; //쿼리 창
sql::ResultSet* MyResultSet; //결과 창
sql::PreparedStatement* MyPreparedStatement; //쿼리를 만들때 injection 방어함.

void ConnectDB()
{
	try
	{
		MyDriver = get_driver_instance();
		MyConnection = MyDriver->connect("tcp://127.0.0.1", "eunseo", "1234");

		MyConnection->setSchema("membership");
	}
	catch (sql::SQLException Exception)
	{
		std::cout << Exception.what() << std::endl;
		std::cout << Exception.getSQLState() << std::endl;
	}
}


bool Login(std::string UserID, std::string Password)
{
	//Stored Procesdure를 사용함.
	sql::SQLString Query = "select * from user where `user_id` = ? and `user_pw` = sha2( ?, 512) and is_delete = 'N';";

	MyPreparedStatement = MyConnection->prepareStatement(Query);
	MyPreparedStatement->setString(1, UserID);
	MyPreparedStatement->setString(2, Password);
	MyResultSet = MyPreparedStatement->executeQuery();

	std::cout << Query << std::endl;

	if (MyResultSet->rowsCount() == 0)
	{
		//Redis와 같은 캐시 서버에 저장
		//hash 키값을 전송함.
		std::cout << "아이디 비번이 틀립니다.";

		return false;
	}
	else
	{
		return true;
	}
}

bool Signup(std::string UserID, std::string Password, std::string Name)
{
	try
	{
		//SQL X
		sql::SQLString Query = "insert into user (`user_id`, `user_pw`, `name`) values ( ?,  sha2(?, 512), ?);";

		MyPreparedStatement = MyConnection->prepareStatement(Query);
		MyPreparedStatement->setString(1, UserID);
		MyPreparedStatement->setString(2, Password);
		MyPreparedStatement->setString(3, Name);
		MyResultSet = MyPreparedStatement->executeQuery();

		std::cout << Query << std::endl;
	}
	catch (sql::SQLException E)
	{
		std::cout << "이미 사용하는 아이디 입니다.";

		return false;
	}

	return true;
}

bool Logout(std::string UserID, std::string Password)
{
	//서버 한대로 작업, 서비스?
	return true;
}


void DisconnectSocket(SOCKET DisconnectedSocket, fd_set* Sockets)
{
	SOCKET ClosedSocket = DisconnectedSocket;

	SOCKADDR_IN ClosedSockAddr;
	memset(&ClosedSockAddr, 0, sizeof(ClosedSockAddr));
	int ClosedSockAddrLength = sizeof(ClosedSockAddr);

	getpeername(ClosedSocket, (SOCKADDR*)&ClosedSockAddr, &ClosedSockAddrLength);

	cout << "disconnect : " << ClosedSocket << endl;

	cout << "disconnect : " << inet_ntoa(ClosedSockAddr.sin_addr) << endl;

	FD_CLR(ClosedSocket, Sockets);
	closesocket(ClosedSocket);

	flatbuffers::FlatBufferBuilder SendBuilder;

	auto DestroyData = UserPacket::CreateS2C_Destroy(
		SendBuilder,
		(uint16_t)ClosedSocket
	);

	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_S2C_Destroy,
		DestroyData.Union()
	);

	SendBuilder.Finish(UserPacketData);

	//dangling pointer
	Session* FindSession = MySessionManager.GetSession(ClosedSocket);

	if (FindSession)
	{
		MySessionManager.Delete(*FindSession);
	}


	//모든 유저한테 이동 패킷 보내줌
	for (auto Receiver : MySessionManager.SessionList)
	{
		SendAll(Receiver.ClientSocket, SendBuilder);
	}
}



void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);

	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_C2S_Login:
	{

		auto LoginPacket = UserPacketData->data_as_C2S_Login();

		bool Result = Login(LoginPacket->user_id()->c_str(),
			LoginPacket->user_pw()->c_str());

		flatbuffers::FlatBufferBuilder SendBuilder;

		if (Result)
		{
			std::cout << "로그인에 성공 했습니다" << endl;

			Session InSession;
			InSession.ClientSocket = ProcessSocket;
			InSession.UserID = LoginPacket->user_id()->c_str();
			InSession.X = rand() % 640;
			InSession.Y = rand() % 480;
			InSession.R = rand() % 255;
			InSession.G = rand() % 255;
			InSession.B = rand() % 255;

			InSession.Shape = 65 + (rand() % 26);

			MySessionManager.Add(InSession);
			//접속 한 아이한테 확인 패킷(S2C_Login)
			auto S2C_Login_Data = UserPacket::CreateS2C_Login(
				SendBuilder,
				(uint16_t)ProcessSocket,
				SendBuilder.CreateString(u8"환영합니다."),
				SendBuilder.CreateString(u8"qw3oddpui2"),
				true
			);

			auto UserPacketData = UserPacket::CreatePacketData(
				SendBuilder,
				UserPacket::PacketType_S2C_Login,
				S2C_Login_Data.Union()
			);

			SendBuilder.Finish(UserPacketData);
		}
		else
		{
			std::cout << "로그인에 실패 했습니다" << endl;

			auto S2C_Login_Data = UserPacket::CreateS2C_Login(
				SendBuilder,
				(uint16_t)ProcessSocket,
				SendBuilder.CreateString("Failed."),
				SendBuilder.CreateString(""),
				false
			);

			auto UserPacketData = UserPacket::CreatePacketData(
				SendBuilder,
				UserPacket::PacketType_S2C_Login,
				S2C_Login_Data.Union()
			);

			SendBuilder.Finish(UserPacketData);
		}


		SendAll(ProcessSocket, SendBuilder);

		//접속한 모든 유저한테 현재 모든 유저의 정보를 보내준다.
		for (auto Item : MySessionManager.SessionList)
		{
			flatbuffers::FlatBufferBuilder LoginSendBuilder;

			UserPacket::FVector2D Position(Item.X, Item.Y);
			UserPacket::FColor Color(Item.R, Item.G, Item.B);
			auto SpawnData = UserPacket::CreateS2C_Spawn(
				LoginSendBuilder,
				(uint16_t)Item.ClientSocket,
				&Position,
				&Color,
				Item.Shape
			);

			auto UserSpawnPacketData = UserPacket::CreatePacketData(
				LoginSendBuilder,
				UserPacket::PacketType_S2C_Spawn,
				SpawnData.Union()
			);

			LoginSendBuilder.Finish(UserSpawnPacketData);

			for (auto Receiver : MySessionManager.SessionList)
			{
				int SentBytes = SendAll(Receiver.ClientSocket, LoginSendBuilder);
				if (SentBytes <= 0)
				{
					std::cout << "header send fail." << endl;
				}
			}
		}
	}
	break;

	case UserPacket::PacketType_C2S_Move:
	{
		flatbuffers::FlatBufferBuilder SendBuilder;

		auto MovePacket = UserPacketData->data_as_C2S_Move();
		Session* FindSession = MySessionManager.GetSession((SOCKET)MovePacket->client_socket_id());
		switch (MovePacket->direction())
		{
		case 'W':
		case 'w':
			FindSession->Y--;
			break;
		case 'S':
		case 's':
			FindSession->Y++;
			break;
		case 'A':
		case 'a':
			FindSession->X--;
			break;
		case 'D':
		case 'd':
			FindSession->X++;
			break;
		}

		UserPacket::FVector2D Position(FindSession->X, FindSession->Y);
		auto S2C_MoveData = UserPacket::CreateS2C_Move(
			SendBuilder,
			(uint16_t)FindSession->ClientSocket,
			&Position
		);

		//std::cout << FindSession->ClientSocket << std::endl;

		auto MoveData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_Move,
			S2C_MoveData.Union()
		);

		SendBuilder.Finish(MoveData);

		//모든 유저한테 이동 패킷 보내줌
		for (auto Receiver : MySessionManager.SessionList)
		{
			int SentBytes = SendAll(Receiver.ClientSocket, SendBuilder);
			if (SentBytes <= 0)
			{
				std::cout << "move send fail." << endl;
			}
		}
	}
	break;

	case UserPacket::PacketType_C2S_ChangeColor:
	{
		flatbuffers::FlatBufferBuilder SendBuilder;

		auto ChangeColorPacket = UserPacketData->data_as_C2S_ChangeColor();

		Session* ChangeSession = MySessionManager.GetSession((SOCKET)ChangeColorPacket->client_socket_id());

		ChangeSession->R = rand() % 255;
		ChangeSession->G = rand() % 255;
		ChangeSession->B = rand() % 255;

		UserPacket::FColor Color(ChangeSession->R, ChangeSession->G, ChangeSession->B);

		auto S2C_ColorData = UserPacket::CreateS2C_ChangeColor(
			SendBuilder,
			ChangeColorPacket->client_socket_id(),
			&Color
		);

		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_ChangeColor,
			S2C_ColorData.Union()
		);

		SendBuilder.Finish(UserPacketData);

		//모든 유저한테 이동 패킷 보내줌
		for (auto Receiver : MySessionManager.SessionList)
		{
			int SentBytes = SendAll(Receiver.ClientSocket, SendBuilder);
			if (SentBytes <= 0)
			{
				std::cout << "change color send fail." << endl;
			}
		}
	}
	break;

	case UserPacket::PacketType_C2S_Signup:
	{
		auto SignupPacket = UserPacketData->data_as_C2S_Signup();

		bool Result = Signup(SignupPacket->user_id()->c_str(),
			SignupPacket->user_pw()->c_str(),
			SignupPacket->name()->c_str()
		);

		flatbuffers::FlatBufferBuilder SendBuilder;

		auto S2C_ColorData = UserPacket::CreateS2C_Signup(
			SendBuilder,
			SendBuilder.CreateString(Result ? "가입 성공했습니다." : "가입에 실패 했습니다."),
			Result
		);

		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_Signup,
			S2C_ColorData.Union()
		);

		SendBuilder.Finish(UserPacketData);
		SendAll(ProcessSocket, SendBuilder);
	}
	break;
	}


}

//blocking, synchrous, multiplexing(polling)
int main()
{
	srand((unsigned int)time(nullptr));

	cout << "server start" << endl;

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ListenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ListenSockAddr;
	memset(&ListenSockAddr, 0, sizeof(ListenSockAddr));
	ListenSockAddr.sin_family = AF_INET;
	ListenSockAddr.sin_addr.s_addr = INADDR_ANY;
	ListenSockAddr.sin_port = htons(35000);

	//already use port 이미 포트 사용중
	::bind(ListenSocket, (SOCKADDR*)&ListenSockAddr, sizeof(ListenSockAddr));

	listen(ListenSocket, SOMAXCONN);

	ConnectDB();

	//blocking, synchronous(TimeOut)
	TIMEVAL TimeOut;
	TimeOut.tv_sec = 0;
	TimeOut.tv_usec = 500000;

	fd_set ReadSockets;
	fd_set CopyReadSockets;

	FD_ZERO(&ReadSockets);
	FD_SET(ListenSocket, &ReadSockets);

	while (true)
	{
		CopyReadSockets = ReadSockets;

		//0.5초씩 blocking
		int ChangeCount = select(0, &CopyReadSockets, 0, 0, &TimeOut);

		if (ChangeCount <= 0)
		{
			//Server Work
			//0.5초한번 서버 작업을 하는거
			continue;
		}

		//몬가 자료 있다.
		for (int i = 0; i < (int)ReadSockets.fd_count; ++i)
		{
			if (FD_ISSET(ReadSockets.fd_array[i], &CopyReadSockets))
			{
				if (ReadSockets.fd_array[i] == ListenSocket)
				{
					//connect process
					SOCKADDR_IN ClientSockAddr;
					memset(&ClientSockAddr, 0, sizeof(ClientSockAddr));
					int ClientSockSockLength = sizeof(ClientSockAddr);

					//blocking, synchronous
					SOCKET ClientSocket = accept(ListenSocket, (SOCKADDR*)&ClientSockAddr, &ClientSockSockLength);

					cout << "connect client " << inet_ntoa(ClientSockAddr.sin_addr) << endl;

					FD_SET(ClientSocket, &ReadSockets);
				}
				else
				{
					//Data Receive
					int RecvBytes = RecvAll(ReadSockets.fd_array[i], Buffer);
					if (RecvBytes <= 0)
					{
						cout << "data recv fail " << endl;
						DisconnectSocket(ReadSockets.fd_array[i], &ReadSockets);
						continue;
					}
					else
					{
						ProcessPacket(ReadSockets.fd_array[i], Buffer);
					}
				}
			}
		}
	}






	closesocket(ListenSocket);
	WSACleanup();

	return 0;
}