#pragma once

//
// File contexts cover all streams of a file. Not sure how useful this is.
// 
class FileContext {

public:
	Mutex Lock;
	ULONGLONG m_OpenFOCount = 0;

	FileContext()
	{
		Lock.Init();
	}

	static NTSTATUS Factory(_In_ PCFLT_RELATED_OBJECTS FltObjects, _Out_ FileContext** Context) 
	{
		*Context = nullptr;

        auto status = FltAllocateContext(FltObjects->Filter,
            FLT_FILE_CONTEXT, sizeof(FileContext), PagedPool,
            (PFLT_CONTEXT*)Context);

		if (!NT_SUCCESS(status)) {
			return status;
		}

		// Placement new
        new (*Context) FileContext();

        return STATUS_SUCCESS;
	}

	static void Cleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
	{
        if (ContextType != FLT_FILE_CONTEXT)
        {
            ASSERT(FALSE);
            return;
        }
        auto* ctx = reinterpret_cast<FileContext*>(Context);
		UNREFERENCED_PARAMETER(ctx);
		return;
	}
};

