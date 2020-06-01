#pragma once

#include "ndcommon.h"
// ntestutil is the main dependency, define server and client base classes
#include "ndtestutil.h"
#include <logging.h>

const SIZE_T x_MaxTransfer = (4 * 1024 * 1024);
const SIZE_T x_HdrLen = 40;
const SIZE_T x_MaxVolume = (500 * x_MaxTransfer);
const SIZE_T x_MaxIterations = 500000;

// rpc state machine: context msg flags
#define RECV_CTXT ((void *) 0x1000) // e.g., peer info msg
#define SEND_CTXT ((void *) 0x2000)
#define READ_CTXT ((void *) 0x3000) // RDMA read cmd
#define WRITE_CTXT ((void *) 0x4000)  // RDMA write cmd

struct PeerInfo
{
    UINT32 m_remoteToken;
    DWORD  m_nIncomingReadLimit;
    UINT64 m_remoteAddress;
};

class RTestServer : public NdTestServerBase
{
public:
    RTestServer() {}

    void RunTest(const struct sockaddr_in& v4Src, DWORD queueDepth, DWORD /*nSge */)
    {
        NdTestBase::Init(v4Src);
        ND2_ADAPTER_INFO adapterInfo = { 0 };
        NdTestBase::GetAdapterInfo(&adapterInfo);

        // Make sure adapter supports in-order RDMA
        if ((adapterInfo.AdapterFlags & ND_ADAPTER_FLAG_IN_ORDER_DMA_SUPPORTED) == 0)
        {
            LOG_FAILURE_AND_EXIT(L"Adapter does not support in-order RDMA.", __LINE__);
        }

        m_maxIncomingReads = adapterInfo.MaxInboundReadLimit;
        NdTestBase::CreateCQ(adapterInfo.MaxCompletionQueueDepth);
        NdTestBase::CreateConnector();
        NdTestBase::CreateQueuePair(min(adapterInfo.MaxCompletionQueueDepth, adapterInfo.MaxReceiveQueueDepth), 1);

        NdTestBase::CreateMR();
        m_pBuf = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, x_MaxTransfer + x_HdrLen));
        if (!m_pBuf)
        {
            LOG_FAILURE_AND_EXIT(L"Failed to allocate data buffer.", __LINE__);
        }

        ULONG flags = ND_MR_FLAG_ALLOW_LOCAL_WRITE | ND_MR_FLAG_ALLOW_REMOTE_READ | ND_MR_FLAG_ALLOW_REMOTE_WRITE;
        NdTestBase::RegisterDataBuffer(m_pBuf, x_MaxTransfer + x_HdrLen, flags);
        printf("Registered DataBuffer\n");

        // post reveive for the terminate message
        ND2_SGE sge = { 0 };
        sge.Buffer = m_pBuf;
        sge.BufferLength = x_MaxTransfer + x_HdrLen;
        sge.MemoryRegionToken = m_pMr->GetLocalToken();
        NdTestBase::PostReceive(&sge, 1, RECV_CTXT);

        NdTestServerBase::CreateListener();

        NdTestServerBase::Listen(v4Src);
        printf("Listening\n");
        NdTestServerBase::GetConnectionRequest();
        NdTestServerBase::Accept(adapterInfo.MaxInboundReadLimit, 0);
        NdTestBase::CreateMW();
        printf("Memory Window created\n");
        NdTestBase::Bind(m_pBuf, x_MaxTransfer + x_HdrLen, ND_OP_FLAG_ALLOW_READ | ND_OP_FLAG_ALLOW_WRITE);
        printf("Found client\n");

        // send remote token and address
        PeerInfo* pInfo = static_cast<PeerInfo*> (m_pBuf);
        pInfo->m_remoteToken = m_pMw->GetRemoteToken();
        pInfo->m_nIncomingReadLimit = m_maxIncomingReads;
        pInfo->m_remoteAddress = reinterpret_cast<UINT64>(m_pBuf);
        NdTestBase::Send(&sge, 1, 0, SEND_CTXT);
        printf("Token and address sent\n");

        // wait for completion
        WaitForCompletionAndCheckContext(SEND_CTXT);
        printf("Sent completion\n");

        // wait for terminate message
        WaitForCompletionAndCheckContext(RECV_CTXT);
        printf("Termination\n");

        //tear down
        NdTestBase::Shutdown();
    }

    ~RTestServer()
    {
        if (m_pBuf != nullptr)
        {
            HeapFree(GetProcessHeap(), 0, m_pBuf);
        }
    }

private:
    ULONG m_maxIncomingReads = 0;
    void* m_pBuf = nullptr;
};



class Client : public NdTestClientBase
{
public:
    Client(bool bUseBlocking) :
        m_bUseBlocking(bUseBlocking)
    {}

    ~Client()
    {
        if (m_pBuf != nullptr)
        {
            HeapFree(GetProcessHeap(), 0, m_pBuf);
        }

        if (m_Sgl != nullptr)
        {
            delete[] m_Sgl;
        }
    }

    DWORD GoUntilQueue(ULONG& iters, DWORD nSge, bool bRead, DWORD flags)
    {
        DWORD numIssued = 0;
        while (m_availCredits > 0 && iters > 0)
        {
            if (bRead)
            {
                NdTestBase::Read(m_Sgl, nSge, m_remoteAddress + iters, m_remoteToken, flags, READ_CTXT);
            }
            else
            {
                NdTestBase::Write(m_Sgl, nSge, m_remoteAddress + iters, m_remoteToken, flags, WRITE_CTXT);
            }
            m_availCredits--; iters--;
            numIssued++;
        }
        return numIssued;
    }


    void SimpleTest(bool bRead) {
        m_availCredits = m_queueDepth;
        ULONG size = 1024;
        ULONG iterations = x_MaxIterations;
        iterations = x_MaxVolume / size;
        currSeg = NdTestBase::PrepareSge(m_Sgl, m_nMaxSge, m_pBuf, size, x_HdrLen, m_pMr->GetLocalToken());

        HRESULT hr = ND_SUCCESS;
        DWORD numIssued = 0, numCompleted = 0;
        DWORD writeFlags = (!bRead && size < m_inlineThreshold) ? ND_OP_FLAG_INLINE : 0;
        numIssued = GoUntilQueue(iterations, currSeg, bRead, writeFlags);
        do
        {
            ND2_RESULT ndRes;
            WaitForCompletion(&ndRes, m_bUseBlocking);
            hr = ndRes.Status;
            switch (hr)
            {
            case ND_SUCCESS:
                if (ndRes.RequestContext != (bRead ? READ_CTXT : WRITE_CTXT))
                {
                    LOG_FAILURE_AND_EXIT(L"Invalid completion context\n", __LINE__);
                }
                numCompleted++; m_availCredits++;
                break;

            case ND_CANCELED:
                break;

            default:
                LOG_FAILURE_HRESULT_AND_EXIT(
                    hr, L"INDCompletionQueue::GetResults returned result with %08x.", __LINE__);
            }
            numIssued += GoUntilQueue(iterations, currSeg, bRead, writeFlags);
        } while ((numIssued != numCompleted || iterations != 0) && hr == ND_SUCCESS);
        printf(bRead ? "RDMA read %lu x\n" : "Finished write %lu x\n",numIssued);
    }

    void RunTest(const struct sockaddr_in& v4Src, const struct sockaddr_in& v4Dst, DWORD queueDepth, DWORD nSge)
    {
        NdTestBase::Init(v4Src);
        ND2_ADAPTER_INFO adapterInfo = { 0 };
        NdTestBase::GetAdapterInfo(&adapterInfo);

        // Make sure adapter supports in-order RDMA
        if ((adapterInfo.AdapterFlags & ND_ADAPTER_FLAG_IN_ORDER_DMA_SUPPORTED) == 0)
        {
            LOG_FAILURE_AND_EXIT(L"Adapter does not support in-order RDMA.", __LINE__);
        }

        m_queueDepth = (queueDepth > 0) ? min(queueDepth, adapterInfo.MaxCompletionQueueDepth) : adapterInfo.MaxCompletionQueueDepth;
        m_queueDepth = min(m_queueDepth, adapterInfo.MaxInitiatorQueueDepth);
        m_nMaxSge = min(nSge, adapterInfo.MaxInitiatorSge);
        m_inlineThreshold = adapterInfo.InlineRequestThreshold;
        // for reads
        m_queueDepth = min(m_queueDepth, adapterInfo.MaxOutboundReadLimit);
        m_nMaxSge = min(nSge, adapterInfo.MaxReadSge);

        NdTestBase::CreateMR();
        m_pBuf = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, x_MaxTransfer + x_HdrLen));
        if (!m_pBuf)
        {
            LOG_FAILURE_AND_EXIT(L"Failed to allocate data buffer.", __LINE__);
        }

        m_Sgl = new (std::nothrow) ND2_SGE[m_nMaxSge];
        if (m_Sgl == nullptr)
        {
            LOG_FAILURE_AND_EXIT(L"Failed to allocate sgl.", __LINE__);
        }

        ULONG flags = ND_MR_FLAG_RDMA_READ_SINK | ND_MR_FLAG_ALLOW_LOCAL_WRITE;
        NdTestBase::RegisterDataBuffer(m_pBuf, x_MaxTransfer + x_HdrLen, flags);

        NdTestBase::CreateCQ(m_queueDepth);
        NdTestBase::CreateConnector();
        NdTestBase::CreateQueuePair(min(m_queueDepth, adapterInfo.MaxReceiveQueueDepth), nSge, m_inlineThreshold);

        ND2_SGE sge;
        sge.Buffer = m_pBuf;
        sge.BufferLength = x_MaxTransfer + x_HdrLen;
        sge.MemoryRegionToken = m_pMr->GetLocalToken();
        NdTestBase::PostReceive(&sge, 1, RECV_CTXT);

        NdTestClientBase::Connect(v4Src, v4Dst, 0, m_queueDepth);
        NdTestClientBase::CompleteConnect();

        // wait for incoming peer info message
        WaitForCompletionAndCheckContext(RECV_CTXT);

        PeerInfo* pInfo = reinterpret_cast<PeerInfo*>(m_pBuf);
        m_remoteToken = pInfo->m_remoteToken;
        m_remoteAddress = pInfo->m_remoteAddress;
        m_queueDepth = min(m_queueDepth, pInfo->m_nIncomingReadLimit);

        m_availCredits = m_queueDepth;

        // set 2 to transfer count
        currSeg = NdTestBase::PrepareSge(m_Sgl, m_nMaxSge, m_pBuf, 2, x_HdrLen, m_pMr->GetLocalToken());
    }


    void EndSession() {
        // send terminate message
        NdTestBase::Send(nullptr, 0, 0);
        WaitForCompletion();

        NdTestBase::Shutdown();
    }

private:
    char* m_pBuf = nullptr;
    bool m_bUseBlocking = false;
    ND2_SGE* m_Sgl = nullptr;
    ULONG m_queueDepth = 0;
    ULONG m_availCredits = 0;
    ULONG m_nMaxSge = 0;
    ULONG m_inlineThreshold = 0;
    UINT64 m_remoteAddress = 0;
    UINT32 m_remoteToken = 0;
    DWORD currSeg = 0;
};