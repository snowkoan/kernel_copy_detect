#pragma once

extern PFLT_FILTER g_Filter;

#define DRIVER_PREFIX "CopyDetect: "

NTSTATUS InitMiniFilter(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
NTSTATUS MinifilterUnload(FLT_FILTER_UNLOAD_FLAGS Flags);
