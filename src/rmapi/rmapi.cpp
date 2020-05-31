//
// Copyright(c) Microsoft Corporation.All rights reserved.
// Licensed under the MIT License.
//
// rmapi.cpp - Simplifying abstraction of an RDMA interface
//
// This API is split into a server and a client part, which can be compiled as exe or dynamic library.
// The server side allocates a buffer, advertises it, and gives access permission to the client.
// The client performs RDMA reads and writes on this buffer with no further involvement of the server.
//

#include "rmapi.h"
#include "helpers.h"

const bool bBlocking = false;
const LPCWSTR LOGFILE = L"rmapi.exe";

int __cdecl _tmain(int argc, TCHAR* argv[])
{
    INIT_LOG(LOGFILE);

    WSADATA wsaData;
    int ret = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (ret != 0)
    {
        printf("Failed to initialize Windows Sockets: %d\n", ret);
        exit(__LINE__);
    }

    HRESULT hr = NdStartup();
    if (FAILED(hr))
    {
        LOG_FAILURE_HRESULT_AND_EXIT(hr, L"NdStartup failed with %08x", __LINE__);
    }

    Config conf = parseArgs(argc, argv);

    if (conf.bServer)
    {
        NdrPingServer server(conf.bOpRead);
        server.RunTest(conf.v4Server, 0, conf.nSge);
    }
    else
    {
        struct sockaddr_in v4Src;
        SIZE_T len = sizeof(v4Src);
        HRESULT hr = NdResolveAddress((const struct sockaddr*)&conf.v4Server,
            sizeof(conf.v4Server), (struct sockaddr*)&v4Src, &len);
        if (FAILED(hr))
        {
            LOG_FAILURE_HRESULT_AND_EXIT(hr, L"NdResolveAddress failed with %08x", __LINE__);
        }

        NdrPingClient client(bBlocking, conf.bOpRead);
        client.RunTest(v4Src, conf.v4Server, 0, conf.nSge);
    }

    hr = NdCleanup();
    if (FAILED(hr))
    {
        LOG_FAILURE_HRESULT_AND_EXIT(hr, L"NdCleanup failed with %08x", __LINE__);
    }

    END_LOG(LOGFILE);
    _fcloseall();
    WSACleanup();
    return 0;
}
