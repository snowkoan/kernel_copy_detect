#include "pch.h"
#include "Driver.h"
#include "Communication.h"

PFLT_FILTER g_Filter = nullptr;

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) 
{
	auto status = InitMiniFilter(DriverObject, RegistryPath);

	if (!NT_SUCCESS(status)) 
	{
		KdPrint((DRIVER_PREFIX "Failed to initialize mini-filter (0x%X)\n", status));
		return status;
	}

	do {
		//
		// Initialize dynamic imports
		//
		DynamicImports::Instance();

		//
		// Set up our filter communication port
		//
        if (!NT_SUCCESS(CommunicationPort::Instance()->InitFilterPort(g_Filter))) 
		{
            KdPrint((DRIVER_PREFIX "Failed to initialize filter port (0x%X)\n", status));
            break;
        }

		//
		//  Start filtering i/o
		//
		status = FltStartFiltering(g_Filter);
	} while (false);

	if (!NT_SUCCESS(status)) 
	{
		MinifilterUnload(0);
	}

	return status;
}
