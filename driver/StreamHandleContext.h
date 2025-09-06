#pragma once

class StreamHandleContext {

public:
    LARGE_INTEGER m_firstWriteTime = {};
    ULONGLONG m_writeCount = {};
    Mutex Lock;
    bool AddedToSourceList = {};

    StreamHandleContext()
    {
        Lock.Init();
    }

    ~StreamHandleContext()
    {
    }

    static NTSTATUS Factory(_In_ PCFLT_RELATED_OBJECTS FltObjects, _Out_ StreamHandleContext** Context)
    {
        *Context = nullptr;

        auto status = FltAllocateContext(FltObjects->Filter,
            FLT_STREAMHANDLE_CONTEXT, sizeof(StreamHandleContext), NonPagedPool, // Mutex must be non-paged
            (PFLT_CONTEXT*)Context);

        if (!NT_SUCCESS(status)) {
            return status;
        }

        // Placement new
        new (*Context) StreamHandleContext();

        return STATUS_SUCCESS;
    }

    static NTSTATUS SetContext(PCFLT_RELATED_OBJECTS FltObjects, StreamHandleContext* Context, bool Replace)
    {
        StreamHandleContext* OldContext = nullptr;

        auto status = FltSetStreamHandleContext(FltObjects->Instance,
            FltObjects->FileObject,
            Replace ?  FLT_SET_CONTEXT_REPLACE_IF_EXISTS : FLT_SET_CONTEXT_KEEP_IF_EXISTS,
            Context,
            reinterpret_cast<PFLT_CONTEXT*>(&OldContext));

        if (OldContext)
        {
            FltReleaseContext(OldContext);
        }

        return status;
    }

    // This is essentially our destructor, called by Filter Manager when the context is deleted
    static void Cleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
    {
        if (ContextType != FLT_STREAMHANDLE_CONTEXT)
        {
            ASSERT(FALSE);
            return;
        }
        auto ctx = reinterpret_cast<StreamHandleContext*>(Context);
        ctx->~StreamHandleContext();
        return;
    }
};