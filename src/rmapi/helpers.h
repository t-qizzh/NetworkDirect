#pragma once

const USHORT x_DefaultPort = 54326;

void ShowUsage()
{
    printf("nrdping [options] <ip>[:<port>]\n"
        "Options:\n"
        "\t-s            - Start as server (listen on IP/Port)\n"
        "\t-c            - Start as client (connect to server IP/Port)\n"
        "\t-n <nSge>     - Number of scatter/gather entries per transfer (default: 1)\n"
        "\t-q <pipeline> - Pipeline limit of <pipeline> requests\n"
        "\t-l <logFile>  - Log output to a file named <logFile>\n"
        "<ip>            - IPv4 Address\n"
        "<port>          - Port number, (default: %hu)\n",
        x_DefaultPort
    );
}

struct Config {
    bool bServer = false;
    bool bClient = false;
    struct sockaddr_in v4Server = { 0 }; // ip4 address
    DWORD nSge = 1; // entries per transfer
    SIZE_T nPipeline = 128; // queue/pipeline size
};

Config parseArgs(int argc, TCHAR* argv[]) {

    Config conf;

    for (int i = 1; i < argc; i++)
    {
        TCHAR* arg = argv[i];
        if ((wcscmp(arg, L"-s") == 0) || (wcscmp(arg, L"-S") == 0))
        {
            conf.bServer = true;
        }
        else if ((wcscmp(arg, L"-c") == 0) || (wcscmp(arg, L"-C") == 0))
        {
            conf.bClient = true;
        }
        else if ((wcscmp(arg, L"-n") == 0) || (wcscmp(arg, L"-N") == 0))
        {
            if (i == argc - 2)
            {
                ShowUsage();
                exit(-1);
            }
            conf.nSge = _ttol(argv[++i]);
        }
        else if ((wcscmp(arg, L"-q") == 0) || (wcscmp(arg, L"-Q") == 0))
        {
            if (i == argc - 2)
            {
                ShowUsage();
                exit(-1);
            }
            conf.nPipeline = _ttol(argv[++i]);
        }
        else if ((wcscmp(arg, L"-l") == 0) || (wcscmp(arg, L"--logFile") == 0))
        {
            RedirectLogsToFile(argv[++i]);
        }
        else if ((wcscmp(arg, L"-h") == 0) || (wcscmp(arg, L"--help") == 0))
        {
            ShowUsage();
            exit(0);
        }
    }

    // ip address is last parameter
    int len = sizeof(conf.v4Server);
    WSAStringToAddress(argv[argc - 1], AF_INET, nullptr,
        reinterpret_cast<struct sockaddr*>(&(conf.v4Server)), &len);

    if ((conf.bClient && conf.bServer) || (!conf.bClient && !conf.bServer))
    {
        printf("Exactly one of client (c) or server (s) must be specified.\n");
        ShowUsage();
        exit(-1);
    }

    if (conf.v4Server.sin_addr.s_addr == 0)
    {
        printf("Bad address.\n\n");
        ShowUsage();
        exit(-1);
    }

    if (conf.v4Server.sin_port == 0)
    {
        conf.v4Server.sin_port = htons(x_DefaultPort);
    }


    if (conf.nSge == 0)
    {
        printf("Invalid or missing SGE length\n\n");
        ShowUsage();
        exit(-1);
    }

    return (conf);
}