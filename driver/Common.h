#pragma once

enum class PortMessageType {
	VolumeMessage = 1,
	FileMessage = 2,
	ErrorMessage = 4,
};

struct PortMessage {
	PortMessageType type;
	ULONG dataLenBytes;
    UCHAR data[1]; // Variable length data follows
};

#define FILTER_PORT_NAME L"\\BackupPort"

constexpr ULONG COMMUNICATION_BUFFER_LEN = 1 << 12; // 4 KB
