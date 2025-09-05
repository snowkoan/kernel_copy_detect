#pragma once

enum class PortMessageType {
	VolumeMessage = 1,
	FileMessage = 2,
	SectionMessage = 3
};

struct PortStringMessage
{
	ULONG dataLenBytes;
    UCHAR data[1]; // Variable length data follows
};

struct PortSectionMessage
{
    ULONG fileSizeBytes;
    HANDLE sectionHandle;
};

struct PortMessage {
	PortMessageType type;

	union
	{
		PortStringMessage stringMsg;
        PortSectionMessage sectionMsg;
	};
};

#define FILTER_PORT_NAME L"\\CopyDetectPort"

constexpr ULONG COMMUNICATION_BUFFER_LEN = 1 << 15; // 32 KB
