#pragma once

#include "LinkedList.h"

struct SourceFileEntry
{
    LIST_ENTRY   Link; // For LinkedList
    ULONG        ProcessId;
    ULONG        ThreadId;
    PFILE_OBJECT FileObject;
};

class SourceFileList 
{

public:     
    SourceFileList()
    {
        m_List.Init();
    }
    ~SourceFileList()
    {
        while (!m_List.IsEmpty())
        {
            auto entry = m_List.RemoveHead();
            ExFreePoolWithTag(entry, SOURCEFILE_POOLTAG);
        }
        m_List.Finalize();
    }

    bool AddFirst(ULONG processId, ULONG threadId, PFILE_OBJECT fileObject)
    {
        auto entry = (SourceFileEntry*)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(SourceFileEntry), SOURCEFILE_POOLTAG);
        if (!entry)
        {
            return false;
        }

        entry->ProcessId = processId;
        entry->ThreadId = threadId;
        entry->FileObject = fileObject;

        // Let's be careful about lifetimes...
        ObReferenceObject(fileObject);
        m_List.AddHead(entry);

        return true;
    }

    bool Remove(PFILE_OBJECT fileObject)
    {
        SourceFileEntry* entry = m_List.Find([fileObject](SourceFileEntry* e) 
            { return e->FileObject == fileObject; });

        if (entry)
        {
            m_List.RemoveItem(entry);
            ObDereferenceObject(fileObject); // We added a reference in AddFirst
            ExFreePoolWithTag(entry, SOURCEFILE_POOLTAG);
            return true;
        }

        return false;
    }

    PFILE_OBJECT Find(ULONG processId, ULONG threadId)
    {
        SourceFileEntry* entry = m_List.Find([processId, threadId](SourceFileEntry* e)
            { return e->ProcessId == processId && e->ThreadId == threadId; });

        if (entry)
        {
            // Sanity check - make sure we don't have duplicate open source files for this pid/tid. It's OK
            // if we have multiple FOs for the same file (it's not clear that this happens in practice but...)
            // 
            // We use a file system trick for our sanity -- all FOs that point to the same stream
            // have the same FsContext pointer. This is not quite true for network, but true enough
            // for our purposes.
            m_List.ForEach([processId, threadId, entry](SourceFileEntry* e)
            {
                if (e->ProcessId == processId && e->ThreadId == threadId)
                {
                    if (entry->FileObject->FsContext != e->FileObject->FsContext)
                    {
                        CommunicationPort::Instance()->SendOutputMessage(PortMessageType::FileMessage,
                            L"Warning: Multiple source files found for PID %u TID %u - FsContext mismatch (0x%p vs 0x%p)",
                            processId,
                            threadId,
                            entry->FileObject->FsContext,
                            e->FileObject->FsContext);
                    }
                }
            });

            return entry->FileObject;
        }

        return nullptr;
    }

    void* operator new (size_t s)
    {
        // Need nonpaged for synch object - verifier complains
        return ExAllocatePool2(POOL_FLAG_NON_PAGED, s, SOURCEFILE_POOLTAG);
    }

    void operator delete(void* p)
    {
        if (p)
        {
            ExFreePoolWithTag(p, SOURCEFILE_POOLTAG);
        }
    }

private:

    LinkedList<SourceFileEntry> m_List;
};
