// This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <fltUser.h>
#include <stdio.h>
#include <string>
#include "..\driver\Common.h"

#pragma comment(lib, "fltlib")

// https://gist.github.com/ccbrown/9722406
void DumpHex(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		}
		else {
			ascii[i % 16] = '.';
		}
		if ((i + 1) % 8 == 0 || i + 1 == size) {
			printf(" ");
			if ((i + 1) % 16 == 0) {
				printf("|  %s \n", ascii);
			}
			else if (i + 1 == size) {
				ascii[(i + 1) % 16] = '\0';
				if ((i + 1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i + 1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

void HandleMessage(const BYTE* buffer) {
	auto msg = (PortMessage*)buffer;

	switch (msg->type)
	{
	case PortMessageType::VolumeMessage:
	case PortMessageType::FileMessage:
	{
		std::wstring output_string(reinterpret_cast<wchar_t*>(msg->stringMsg.data), msg->stringMsg.dataLenBytes / sizeof(wchar_t));
        bool needsNewline = !output_string.empty() && output_string.back() != L'\n';
        wprintf(L"%s%s", output_string.c_str(), needsNewline ? L"\n" : L"");
		break;
	}
	case PortMessageType::SectionMessage:
	{
        // We received a section handle from the driver. We now own it.
        wprintf(L"Received section handle - file size %d bytes, handle %u\n", 
			msg->sectionMsg.fileSizeBytes,
			HandleToUlong(msg->sectionMsg.sectionHandle));

        // Map the section into our address space so we can look at it.
        PBYTE data = reinterpret_cast<PBYTE>(MapViewOfFile(msg->sectionMsg.sectionHandle, FILE_MAP_READ, 0, 0, 0));
		if (0 == data)
		{
            wprintf(L"Failed to map section into address space. Error %d\n", GetLastError());
            break;
        }

        // Dump some of the data
		constexpr ULONG maxBytesToPrint = 32;
        DumpHex(data, min(msg->sectionMsg.fileSizeBytes, maxBytesToPrint));
        // printBytes(data, min(msg->dataLenBytes, maxBytesToPrint));
		if (msg->sectionMsg.fileSizeBytes > maxBytesToPrint)
		{
			wprintf(L"...\n");
            
			// We may end up printing some bytes twice. It's a POC.
			ULONG initialOffset = msg->sectionMsg.fileSizeBytes - maxBytesToPrint;
            DumpHex(data + initialOffset, maxBytesToPrint);
		}

		UnmapViewOfFile(data);
		CloseHandle(msg->sectionMsg.sectionHandle);

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

