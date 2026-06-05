#include "pch.h"

#include "NetUtil.h"

#include <iostream>

int SendAll(SOCKET TargetSocket, const flatbuffers::FlatBufferBuilder& Builder)
{
	int SentBytes = 0;

	unsigned short PacketSize = Builder.GetSize();
	PacketSize = htons(PacketSize);

	//::send(TargetSocket, (char*)PacketSize, 2, 0);
	//Header
	SentBytes = SendAll(TargetSocket, (char*)&PacketSize, sizeof(PacketSize));
	//std::cout << "send header : " << SentBytes << std::endl;
	if (SentBytes <= 0)
	{
		std::cout << "header send Error" << std::endl;
	}

	//Data
	SentBytes = SendAll(TargetSocket, (char*)Builder.GetBufferPointer(), Builder.GetSize());
	//std::cout << "send data : " << SentBytes << std::endl;
	if (SentBytes <= 0)
	{
		std::cout << "data send Error" << std::endl;
	}

	return SentBytes;
}


int SendAll(SOCKET TargetSocket, const char* Data, int Size)
{
	int TotalSendDataSize = 0;
	int WantSendDataSize = Size;
	int SentBytes = 0;
	int Count = 0;
	do
	{
		SentBytes = ::send(TargetSocket, Data + TotalSendDataSize, WantSendDataSize - TotalSendDataSize, 0);
		TotalSendDataSize += SentBytes;
		if (SentBytes <= 0)
		{
			return SentBytes;
		}
	} while (TotalSendDataSize < WantSendDataSize);

	return WantSendDataSize;
}

int RecvAll(SOCKET SourceSocket, char* OutData)
{
	unsigned short PacketSize = 0;
	int RecvLength = 0;
	
	//header, size
	RecvLength = ::recv(SourceSocket, (char*)&PacketSize, sizeof(PacketSize), MSG_WAITALL);
	//std::cout << "recv header : " << RecvLength << std::endl;
	if (RecvLength <= 0)
	{
		return RecvLength;
	}

	PacketSize = ntohs(PacketSize);

	RecvLength = ::recv(SourceSocket, OutData, PacketSize, MSG_WAITALL);
	//std::cout << "recv data : " << RecvLength << std::endl;
	if (RecvLength <= 0)
	{
		return RecvLength;
	}

	return RecvLength;

}
