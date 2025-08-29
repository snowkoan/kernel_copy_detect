#pragma once

NTSTATUS InitFilterPort(
	_In_ PFLT_FILTER Filter);

void FinalizeFilterPort();

NTSTATUS PortConnectNotify(
	_In_ PFLT_PORT ClientPort,
	_In_opt_ PVOID ServerPortCookie,
	_In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
	_In_ ULONG SizeOfContext,
	_Outptr_result_maybenull_ PVOID* ConnectionPortCookie);

void PortDisconnectNotify(_In_opt_ PVOID ConnectionCookie);

NTSTATUS PortMessageNotify(
	_In_opt_ PVOID PortCookie,
	_In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
	_In_ ULONG InputBufferLength,
	_Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
	_In_ ULONG OutputBufferLength,
	_Out_ PULONG ReturnOutputBufferLength);

NTSTATUS SendOutputMessage(_In_ PortMessageType type, _In_ LPCWSTR formatString, ...);

