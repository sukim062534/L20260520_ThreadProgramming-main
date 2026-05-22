#pragma once
#include "Packet.h"
class S2C_Spawn : public IPacket
{
public:

	SOCKET ClientSocket;
	int X;
	int Y;
	char Shape = ' ';

	// Inherited via IPacket
	void Parse(std::string InString) override;
	std::string ToString() override;
};

