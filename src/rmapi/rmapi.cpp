//
// Copyright(c) Microsoft Corporation.All rights reserved.
// Licensed under the MIT License.
//
// rmapi.cpp - Simplified RDMA interface
//
// This API is split into a server and a client part, which can be compiled as exe or dynamic library.
// The server side allocates a buffer, advertises it, and gives access permission to the client.
// The client performs RDMA reads and writes on this buffer with no further involvement of the server.
//

#include <time.h>
#include <chrono>

#include "rmapi.h"
#include "helpers.h"

const bool bBlocking = true;

int __cdecl _tmain(int argc, TCHAR* argv[])
{
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
        RTestServer server;
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

        Client cl(bBlocking);
        cl.RunTest(v4Src, conf.v4Server, 0, conf.nSge); // sets up a session
        // cl.SimpleTest(false);
        // cl.SimpleTest(true);
        
        // QZ: start the clock
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        cl.SimpleWriteTest();
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        // time(&t_end);
        printf("Write XX GB data in %lf seonds.\n", (std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000000.0));

        begin = std::chrono::steady_clock::now();
        cl.SimpleReadTest();
        end = std::chrono::steady_clock::now();
        // time(&t_end);
        printf("Transfer XX GB data in %lf seonds.\n", (std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000000.0));
        cl.EndSession();
    }

    hr = NdCleanup();
    if (FAILED(hr))
    {
        LOG_FAILURE_HRESULT_AND_EXIT(hr, L"NdCleanup failed with %08x", __LINE__);
    }

    _fcloseall(); // Close FDs?
    WSACleanup(); // Close the socket
    return 0;
}
