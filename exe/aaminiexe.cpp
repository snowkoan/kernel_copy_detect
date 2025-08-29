// This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <fltUser.h>
#include <stdio.h>
#include <string>
#include "..\driver\Common.h"

#pragma comment(lib, "fltlib")

void HandleMessage(const BYTE* buffer) {
	auto msg = (PortMessage*)buffer;

	switch (msg->type)
	{
	case PortMessageType::VolumeMessage:
	case PortMessageType::FileMessage:
	{
		std::wstring output_string(reinterpret_cast<wchar_t*>(msg->data), msg->dataLenBytes / sizeof(wchar_t));
        bool needsNewline = !output_string.empty() && output_string.back() != L'\n';
        wprintf(L"%s%s", output_string.c_str(), needsNewline ? L"\n" : L"");
		break;
	}
	default:
	{
		printf("Unknown message type");
		break;
	}
	}
}

int main() 
{
	HANDLE hPort;
	auto hr = FilterConnectCommunicationPort(FILTER_PORT_NAME, 0, nullptr, 0, nullptr, &hPort);
	if (FAILED(hr)) 
	{
		wprintf(L"Error connecting to port (HR=0x%08X)\n", hr);
		return 1;
	}
    wprintf(L"Connected to port %ls. Waiting for messages.\n", FILTER_PORT_NAME);

	BYTE buffer[COMMUNICATION_BUFFER_LEN];	// 4 KB
	auto message = (FILTER_MESSAGE_HEADER*)buffer;

	for (;;) {
		hr = FilterGetMessage(hPort, message, sizeof(buffer), nullptr);

		if (FAILED(hr)) 
		{
			wprintf(L"Error receiving message (0x%08X)\n", hr);
			break;
		}

		HandleMessage(buffer + sizeof(FILTER_MESSAGE_HEADER));
	}

	CloseHandle(hPort);

	return 0;
}

