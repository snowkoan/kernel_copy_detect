#pragma once

//
// File contexts cover all streams of a file.
//
class StreamContext {

public:
	Mutex Lock;
	ULONGLONG m_OpenFOCount = 0;

	StreamContext()
	{
		Lock.Init();
	}

	static NTSTATUS Factory(_In_ PCFLT_RELATED_OBJECTS FltObjects, _Out_ StreamContext** Context)
	{
		*Context = nullptr;

		auto status = FltAllocateContext(FltObjects->Filter,
			FLT_STREAM_CONTEXT, sizeof(StreamContext), PagedPool,
			(PFLT_CONTEXT*)Context);

		if (!NT_SUCCESS(status)) {
			return status;
		}

		// Placement new
		new (*Context) StreamContext();

		return STATUS_SUCCESS;
	}

	// This is essentially our destructor, called by Filter Manager when the context is deleted
	static void Cleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
	{
		if (ContextType != FLT_STREAM_CONTEXT)
		{
			ASSERT(FALSE);
			return;
		}
        auto ctx = reinterpret_cast<StreamContext*>(Context);
		UNREFERENCED_PARAMETER(ctx);
		return;
	}

};


