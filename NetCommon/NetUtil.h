#pragma once

#include "pch.h"


#include "SessionManager.h"


extern int SendAll(SOCKET TargetSocket, const flatbuffers::FlatBufferBuilder& Builder);
extern int RecvAll(SOCKET SourceSocket, char* OutData);

extern int SendAll(SOCKET TargetSocket, const char* InData, int Size);

