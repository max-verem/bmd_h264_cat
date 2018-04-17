#define _CRT_SECURE_NO_WARNINGS

#define DIV_RATIO 1000

#include <windows.h>
#include <comutil.h>
#include <stdio.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <time.h>

#include <Mmsystem.h>
#pragma comment(lib, "winmm.lib")

#define WS_VER_MAJOR 2
#define WS_VER_MINOR 2
#pragma comment(lib, "ws2_32.lib")

#include "../DeckLinkAPI_h.h"
#include "../DeckLinkAPI_i.c"

#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "winmm")

#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "BMDStreamingEncodingFrameRate.h"
#include "BMDStreamingH264EntropyCoding.h"
#include "BMDStreamingH264Level.h"
#include "BMDStreamingH264Profile.h"

static int f_socket_init = 0;

class CStreamingOutput :
    public IBMDStreamingDeviceNotificationCallback,
    public IBMDStreamingH264InputCallback
{
public:
    int p1;
    BOOL m_playing;
    IDeckLink* m_streamingDevice;
    IBMDStreamingDiscovery*	m_streamingDiscovery;
    IBMDStreamingDeviceInput* m_streamingDeviceInput;
    BMDStreamingDeviceMode m_deviceMode;
    BMDVideoConnection m_inputConnector;
    BMDDisplayMode m_inputMode;

    int m_AudioBitrateKbs, m_VideoBitrateKbs, m_PresetAlter;
    int m_SrcX, m_SrcY, m_SrcWidth, m_SrcHeight, m_DstWidth, m_DstHeight, m_AudioSampleRate, m_AudioChannelCount;
    char m_Preset[PATH_MAX], m_Profile[PATH_MAX], m_Entropy[PATH_MAX], m_FrameRate[PATH_MAX], m_Level[PATH_MAX];
    struct
    {
        char host[PATH_MAX];
        int port;
        SOCKET socket;
        int sndbuf;
    } tcp;
    struct
    {
        char host[PATH_MAX];
        int port;
        struct sockaddr_in addr;
        SOCKET socket;
        int idx;
        char buf[4096];
        int sndbuf;
    } udp;
    struct
    {
        FILE* descriptor;
        FILE* stdout_descriptor;
        char filename[PATH_MAX];
    } file;

    int start()
    {
        HRESULT result;

        // Note: at this point you may get device notification messages!
        result = m_streamingDiscovery->InstallDeviceNotifications(this);
        if (FAILED(result))
        {
            fprintf(stderr, "ERROR: InstallDeviceNotifications failed\n");
            return -1;
        };

        fprintf(stderr, "%s: InstallDeviceNotifications done\n", __FUNCTION__);

        return 0;
    };

    int stop()
    {
        if (m_playing)
            m_streamingDeviceInput->StopCapture();
        m_streamingDiscovery->UninstallDeviceNotifications();
        SAFE_RELEASE(m_streamingDeviceInput);
        SAFE_RELEASE(m_streamingDevice);
        return 0;
    };

    int release()
    {
        fprintf(stderr, "%s:%d\n", __FUNCTION__, __LINE__);

        if (udp.socket > 0)
        {
//            shutdown(udp.socket, SD_BOTH);
            closesocket(udp.socket);
        }

        if (tcp.socket > 0)
        {
//            shutdown(udp.socket, SD_BOTH);
            closesocket(tcp.socket);
        }

        if (file.descriptor)
            fclose(file.descriptor);

        SAFE_RELEASE(m_streamingDiscovery);

        fprintf(stderr, "%s:%d\n", __FUNCTION__, __LINE__);

        return 0;
    };

    int init()
    {
        int r;
        HRESULT result;
        WSADATA wsaData;

        fprintf(stderr, "%s:%d\n", __FUNCTION__, __LINE__);

        result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(result))
        {
            fprintf(stderr, "%s:%d ERROR! CoInitializeEx failed\n", __FUNCTION__, __LINE__);
            return -1;
        };

        result = CoCreateInstance(CLSID_CBMDStreamingDiscovery_v10_8, NULL, CLSCTX_ALL, IID_IBMDStreamingDiscovery, (void**)&m_streamingDiscovery);
        if (FAILED(result))
        {
            fprintf(stderr, "%s:%d ERROR! Failed to create streaming discovery (v10_8)\n", __FUNCTION__, __LINE__);

            result = CoCreateInstance(CLSID_CBMDStreamingDiscovery, NULL, CLSCTX_ALL, IID_IBMDStreamingDiscovery, (void**)&m_streamingDiscovery);
            if (FAILED(result))
            {
                fprintf(stderr, "%s:%d ERROR! Failed to create streaming discovery (current)\n", __FUNCTION__, __LINE__);
                return -1;
            }
        };

        /* file to save */
        if(!strcmp(file.filename, "-"))
        {
            file.stdout_descriptor = stdout;
            _setmode(_fileno(file.stdout_descriptor), O_BINARY);
        }
        else if (file.filename[0])
        {
            file.descriptor = fopen(file.filename, "wb");
            if (!file.descriptor)
            {
                fprintf(stderr, "%s:%d ERROR! Failed to open file [%s]\n", __FUNCTION__, __LINE__, file.filename);
                return -1;
            };
            fprintf(stderr, "%s:%d data will be saved to file [%s]\n", __FUNCTION__, __LINE__, file.filename);
        }
        else
        {
            fprintf(stderr, "%s:%d ERROR! Filename to save not specified, either specify file name directlry or provide -savefile flag\n",
                __FUNCTION__, __LINE__);
            return -1;
        }

        /* init winsock */
        if (!f_socket_init)
        {
            if (WSAStartup(((unsigned long)WS_VER_MAJOR) | (((unsigned long)WS_VER_MINOR) << 8), &wsaData) != 0)
            {
                fprintf(stderr, "%s:%d ERROR! WSAStartup failed\n", __FUNCTION__, __LINE__);
                return -WSAGetLastError();
            };
        };

        /* open udp socket */
        if (udp.port && udp.host[0])
        {
            char* tmp;
            struct in_addr *tmp_in_addr;
            struct sockaddr_in saddr;
            struct hostent *host_ip;
            unsigned long
                multicast_a = ntohl(inet_addr("224.0.0.0")),
                multicast_b = ntohl(inet_addr("239.255.255.255"));

            /* resolv hostname */
            host_ip = gethostbyname(udp.host);
            if (NULL == host_ip)
            {
                host_ip = gethostbyaddr(udp.host, strlen(udp.host), AF_INET);
                if (NULL == host_ip)
                {
                    fprintf(stderr, "%s:%d ERROR! failed to resolv [%s]\n", __FUNCTION__, __LINE__, udp.host);
                    return -1;
                }
            }

            /* create communication socket */
            udp.socket = socket(AF_INET, SOCK_DGRAM, 0);
            if (INVALID_SOCKET == udp.socket)
            {
                r = -WSAGetLastError();
                fprintf(stderr, "%s:%d ERROR! failed to create socket\n", __FUNCTION__, __LINE__);
                return r;
            }

            /* setup source address */
            memset(&saddr, 0, sizeof(struct sockaddr_in));
            saddr.sin_family = PF_INET;
            saddr.sin_port = htons(0); // Use the first free port
            saddr.sin_addr.s_addr = htonl(INADDR_ANY); // bind socket to any interface
            r = bind(udp.socket, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
            if (INVALID_SOCKET == r)
            {
                r = -WSAGetLastError();
                fprintf(stderr, "%s:%d ERROR! failed to bind socket\n", __FUNCTION__, __LINE__);
                return r;
            }

            /* prepare address */
            udp.addr.sin_family = AF_INET;
            tmp_in_addr = (struct in_addr*)host_ip->h_addr_list[0];
            tmp = inet_ntoa(*tmp_in_addr);
            udp.addr.sin_addr.s_addr = inet_addr(tmp);
            udp.addr.sin_port = htons((unsigned short)udp.port);

            /* check for multicast setup */
            if (ntohl(udp.addr.sin_addr.s_addr) > multicast_a && udp.addr.sin_addr.s_addr < multicast_b)
            {
                struct in_addr iaddr;
                unsigned char ttl = 32;
                unsigned char one = 1;

                memset(&iaddr, 0, sizeof(struct in_addr));

                iaddr.s_addr = INADDR_ANY; // use DEFAULT interface

                // Set the outgoing interface to DEFAULT
                r = setsockopt(udp.socket, IPPROTO_IP, IP_MULTICAST_IF,
                    (const char *)&iaddr, sizeof(struct in_addr));

                // Set multicast packet TTL to 3; default TTL is 1
                r = setsockopt(udp.socket, IPPROTO_IP, IP_MULTICAST_TTL,
                    (const char *)&ttl, sizeof(unsigned char));

                // send multicast traffic to myself too
                r = setsockopt(udp.socket, IPPROTO_IP, IP_MULTICAST_LOOP,
                    (const char *)&one, sizeof(unsigned char));
            };

            if (udp.sndbuf)
            {
                r = udp.sndbuf;
                r = setsockopt(udp.socket, SOL_SOCKET, SO_SNDBUF, (const char *)&r, sizeof(r));
            };
        };

        /* open udp socket */
        if (tcp.port && tcp.host[0])
        {
            char* tmp;
            struct in_addr *tmp_in_addr;
            struct sockaddr_in saddr;
            struct hostent *host_ip;

            /* resolv hostname */
            host_ip = gethostbyname(tcp.host);
            if (NULL == host_ip)
            {
                host_ip = gethostbyaddr(tcp.host, strlen(tcp.host), AF_INET);
                if (NULL == host_ip)
                {
                    fprintf(stderr, "%s:%d ERROR! failed to resolv [%s]\n", __FUNCTION__, __LINE__, udp.host);
                    return -1;
                }
            }

            /* create communication socket */
            tcp.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (INVALID_SOCKET == tcp.socket)
            {
                r = -WSAGetLastError();
                fprintf(stderr, "%s:%d ERROR! failed to create socket [%ld]\n", __FUNCTION__, __LINE__, -r);
                return r;
            }

            /* setup address */
            tmp_in_addr = (struct in_addr*)host_ip->h_addr_list[0];
            tmp = inet_ntoa(*tmp_in_addr);

            memset(&saddr, 0, sizeof(struct sockaddr_in));
            saddr.sin_family = AF_INET;
            saddr.sin_port = htons(tcp.port);
            saddr.sin_addr.s_addr = inet_addr(tmp);

            r = connect(tcp.socket, (struct sockaddr *)&saddr, sizeof(struct sockaddr));
            if (INVALID_SOCKET == r)
            {
                r = -WSAGetLastError();
                fprintf(stderr, "%s:%d ERROR! failed to connect socket [%ld]\n", __FUNCTION__, __LINE__, -r);
                return r;
            }

            if (tcp.sndbuf)
            {
                r = tcp.sndbuf;
                r = setsockopt(tcp.socket, SOL_SOCKET, SO_SNDBUF, (const char *)&r, sizeof(r));
            };
        }


        return 0;
    };

    static void print_usage()
    {
        int i;

        fprintf
        (
            stderr,
            "Usage:\n"
            "    bmd_h264_cat.exe <args> [- | filename]\n"
            "Where args are:\n"
            "    -ab <INT>          audio bitrate in kbs\n"
            "    -vb <INT>          video bitrate in kbs\n"
            "    -ar <INT>          audio samplerate\n"
            "    -ac <INT>          audio channels\n"
            "    -framerate <STR>   framerate, see list below\n"
            "    -profile <STR>     h264 encoding profile to use, see list below\n"
            "    -entropy <STR>     h264 encoding entropy to use, see list below\n"
            "    -level <STR>       h264 encoding level to use, see list below\n"
            "    -preset <STR>      hardware encoder preset name to use, see list in logs\n"
            "    -src-x <INT>       source rectangle\n"
            "    -src-y <INT>\n"
            "    -src-width <INT>\n"
            "    -src-height <INT>\n"
            "    -dst-width <INT>   destination width\n"
            "    -dst-height <INT>  destination height\n"
            "    -savefile          save files timestamped\n"
            "    -udp-host <STR>    host where sent UDP packet\n"
            "    -udp-port <INT>    port where sent UDP packet\n"
            "    -udp-sndbuf <INT>  SO_SNDBUF of UDP socket\n"
            "    -tcp-host <STR>    host where sent TCP packet\n"
            "    -tcp-port <INT>    port where sent TCP packet\n"
            "    -tcp-sndbuf <INT>  SO_SNDBUF of TCP socket\n"
        );

        fprintf(stderr, "Framerates:");
        for (i = 0; BMDStreamingEncodingFrameRate_pairs[i]; i += 2)
            fprintf(stderr, " [%s]", (char*)BMDStreamingEncodingFrameRate_pairs[i + 1]);
        fprintf(stderr, "\n");

        fprintf(stderr, "Entropyies:");
        for (i = 0; BMDStreamingH264EntropyCoding_pairs[i]; i += 2)
            fprintf(stderr, " [%s]", (char*)BMDStreamingH264EntropyCoding_pairs[i + 1]);
        fprintf(stderr, "\n");

        fprintf(stderr, "Levels:");
        for (i = 0; BMDStreamingH264Level_pairs[i]; i += 2)
            fprintf(stderr, " [%s]", (char*)BMDStreamingH264Level_pairs[i + 1]);
        fprintf(stderr, "\n");

        fprintf(stderr, "Profiles:");
        for (i = 0; BMDStreamingH264Profile_pairs[i]; i += 2)
            fprintf(stderr, " [%s]", (char*)BMDStreamingH264Profile_pairs[i + 1]);
        fprintf(stderr, "\n");
    }

    int load_args(int argc, char** argv)
    {
#define IF_PARAM0(V) if (!strcmp(V, argv[i]))
#define IF_PARAM1(V) if (!strcmp(V, argv[i]) && (i + 1) < argc)
#define PARAM1_INT(N, P) IF_PARAM1(N) { i++;  P = atol(argv[i]); m_PresetAlter++;}
#define PARAM1_INT_NA(N, P) IF_PARAM1(N) { i++;  P = atol(argv[i]);}
#define PARAM1_CHAR(N, P) IF_PARAM1(N) { i++;  strncpy(P, argv[i], PATH_MAX); m_PresetAlter++;}
#define PARAM1_CHAR_NA(N, P) IF_PARAM1(N) { i++;  strncpy(P, argv[i], PATH_MAX);}
        /* setup arguments */
        for (int i = 1; i < argc; i++)
        {
            PARAM1_INT("-ab", m_AudioBitrateKbs)
            else PARAM1_INT("-ar", m_AudioSampleRate)
            else PARAM1_INT("-ac", m_AudioChannelCount)
            else PARAM1_INT("-vb", m_VideoBitrateKbs)
            else PARAM1_CHAR_NA("-preset", m_Preset)
            else PARAM1_CHAR("-profile", m_Profile)
            else PARAM1_CHAR("-level", m_Level)
            else PARAM1_CHAR("-entropy", m_Level)
            else PARAM1_CHAR("-framerate", m_FrameRate)
            else PARAM1_INT("-src-x", m_SrcX)
            else PARAM1_INT("-src-y", m_SrcY)
            else PARAM1_INT("-src-width", m_SrcWidth)
            else PARAM1_INT("-src-height", m_SrcHeight)
            else PARAM1_INT("-dst-width", m_DstWidth)
            else PARAM1_INT("-dst-height", m_DstHeight)
            else PARAM1_CHAR_NA("-udp-host", udp.host)
            else PARAM1_INT_NA("-udp-port", udp.port)
            else PARAM1_INT_NA("-udp-sndbuf", udp.sndbuf)
            else PARAM1_CHAR_NA("-tcp-host", tcp.host)
            else PARAM1_INT_NA("-tcp-port", tcp.port)
            else PARAM1_INT_NA("-tcp-sndbuf", tcp.sndbuf)
            else if (!strcmp("-savefile", argv[i]))
            {
                time_t ltime;
                struct tm *rtime;

                /* check if log file should be rotated */
                time(&ltime);

                /* date to filename */
                rtime = localtime(&ltime);
                strftime(file.filename, MAX_PATH, "%Y%m%d_%H%M%S.ts", rtime);
            }
            else if (!strcmp("-h", argv[i]))
            {
                print_usage();
                return -1;
            }
            else
                strncpy(file.filename, argv[i], sizeof(file.filename));
        }

        return 0;
    };

    CStreamingOutput(int argc, char** argv)
    {
        p1 = 0;
        m_playing = false;
        m_streamingDevice = NULL;
        m_streamingDiscovery = NULL;
        m_streamingDeviceInput = NULL;
        m_PresetAlter = m_AudioBitrateKbs = m_VideoBitrateKbs = 0;
        m_SrcX = m_SrcY = m_SrcWidth = m_SrcHeight = m_DstWidth = m_DstHeight = m_AudioSampleRate = m_AudioChannelCount = 0;
        m_Level[0] = m_FrameRate[0] = m_Preset[0] = m_Profile[0] = m_Entropy[0] = 0;
        memset(&udp, 0, sizeof(udp));
        memset(&tcp, 0, sizeof(tcp));
        memset(&file, 0, sizeof(file));

        if(load_args(argc, argv))
            exit(0);

        if(!file.filename[0])
        {
            print_usage();
            exit(0);
        };
    };

    ~CStreamingOutput()
    {
        //		stop();
        SAFE_RELEASE(m_streamingDiscovery);
    };

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv)
    {
        HRESULT result = E_NOINTERFACE;

        if (ppv == NULL)
            return E_POINTER;
        *ppv = NULL;

        if (iid == IID_IUnknown)
        {
            *ppv = static_cast<IUnknown*>(static_cast<IBMDStreamingDeviceNotificationCallback*>(this));
            AddRef();
            result = S_OK;
        }
        else if (iid == IID_IBMDStreamingDeviceNotificationCallback)
        {
            *ppv = static_cast<IBMDStreamingDeviceNotificationCallback*>(this);
            AddRef();
            result = S_OK;
        }
        else if (iid == IID_IBMDStreamingH264InputCallback)
        {
            *ppv = static_cast<IBMDStreamingH264InputCallback*>(this);
            AddRef();
            result = S_OK;
        }

        return result;
    }

public:
    // IUknown
    // We need to correctly implement QueryInterface, but not the AddRef/Release
    virtual ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() { return 1; }

public:
    // IBMDStreamingDeviceNotificationCallback
    virtual HRESULT STDMETHODCALLTYPE StreamingDeviceArrived(IDeckLink* device)
    {
        HRESULT result;

        fprintf(stderr, "%s\n", __FUNCTION__);

        // Check we don't already have a device.
        if (m_streamingDevice != NULL)
        {
            fprintf(stderr, "%s: we already use device\n", __FUNCTION__);
            return S_OK;
        }

        // See if it can do input:
        result = device->QueryInterface(IID_IBMDStreamingDeviceInput, (void**)&m_streamingDeviceInput);
        if (FAILED(result))
        {
            // This device doesn't support input. We can ignore this device.
            fprintf(stderr, "%s: ERROR: device does not support input\n", __FUNCTION__);
            return S_OK;
        }

        // Ok, we're happy with this device, hold a reference to the device (we
        // also have a reference held from the QueryInterface, too).
        m_streamingDevice = device;
        m_streamingDevice->AddRef();

        // Now install our callbacks. To do this, we must query our own delegate
        // to get it's IUnknown interface, and pass this to the device input interface.
        // It will then query our interface back to a IBMDStreamingH264InputCallback,
        // if that's what it wants.
        // Note, although you may be tempted to cast directly to an IUnknown, it's
        // not particular safe, and is invalid COM.
        IUnknown* ourCallbackDelegate;
        this->QueryInterface(IID_IUnknown, (void**)&ourCallbackDelegate);
        //
        result = m_streamingDeviceInput->SetCallback(ourCallbackDelegate);
        //
        // Finally release ourCallbackDelegate, since we created a reference to it
        // during QueryInterface. The device will hold its own reference.
        ourCallbackDelegate->Release();

        if (FAILED(result))
        {
            SAFE_RELEASE(m_streamingDeviceInput);
            SAFE_RELEASE(m_streamingDevice);
            return S_OK;
        }

        fprintf(stderr, "%s!\n", __FUNCTION__);
        UpdateUIForNewDevice();

        return S_OK;
    };

    virtual HRESULT STDMETHODCALLTYPE StreamingDeviceRemoved(IDeckLink* device)
    {
        // These messages will happen on the main loop as a result
        // of the message pump.

        // We only care about removal of the device we are using
        if (device != m_streamingDevice)
            return S_OK;

        m_streamingDeviceInput->SetCallback(NULL);
        SAFE_RELEASE(m_streamingDeviceInput);
        SAFE_RELEASE(m_streamingDevice);

        fprintf(stderr, "%s!\n", __FUNCTION__);
        exit(0);

        return S_OK;
    };

    virtual HRESULT STDMETHODCALLTYPE StreamingDeviceModeChanged(IDeckLink* device, BMDStreamingDeviceMode mode)
    {
        fprintf(stderr, "%s!\n", __FUNCTION__);

        if (mode == m_deviceMode)
            return S_OK;

        m_deviceMode = mode;

        UpdateUIForModeChanges();

        return S_OK;
    };

    virtual HRESULT STDMETHODCALLTYPE StreamingDeviceFirmwareUpdateProgress(IDeckLink* device, unsigned char percent)
    {
        fprintf(stderr, "%s: %d%%\n", __FUNCTION__, percent);
        return S_OK;
    };

    void UpdateUIForNewDevice()
    {
        char* m;
        BSTR modelName;
        HRESULT hr;

        if (m_streamingDevice->GetModelName(&modelName) != S_OK)
        {
            fprintf(stderr, "%s: GetModelName failed\n", __FUNCTION__);
            return;
        }
        m = _com_util::ConvertBSTRToString(modelName);
        SysFreeString(modelName);
        fprintf(stderr, "%s: found model name [%s]\n", __FUNCTION__, m);
        delete[] m;

        if (m_streamingDevice->GetDisplayName(&modelName) != S_OK)
        {
            fprintf(stderr, "%s: GetDisplayName failed\n", __FUNCTION__);
            return;
        }
        m = _com_util::ConvertBSTRToString(modelName);
        SysFreeString(modelName);
        fprintf(stderr, "%s: found display [%s]\n", __FUNCTION__, m);
        delete[] m;

        // Add video input modes:
        IDeckLinkDisplayModeIterator* inputModeIterator;
        if (FAILED(m_streamingDeviceInput->GetVideoInputModeIterator(&inputModeIterator)))
        {
            fprintf(stderr, "%s: ERROR, GetVideoInputModeIterator failed\n", __FUNCTION__);
            return;
        }

        BMDDisplayMode currentInputModeValue;
        if (FAILED(m_streamingDeviceInput->GetCurrentDetectedVideoInputMode(&currentInputModeValue)))
        {
            fprintf(stderr, "%s: ERROR, GetCurrentDetectedVideoInputMode failed\n", __FUNCTION__);
            return;
        }

        IDeckLinkDisplayMode* inputMode;
        while (inputModeIterator->Next(&inputMode) == S_OK)
        {
            BSTR modeName;
            if (inputMode->GetName(&modeName) != S_OK)
            {
                SAFE_RELEASE(inputMode);
                SAFE_RELEASE(inputModeIterator);
                fprintf(stderr, "%s: ERROR, inputMode->GetName failed\n", __FUNCTION__);
                return;
            }

            if (inputMode->GetDisplayMode() == currentInputModeValue)
            {
                m = _com_util::ConvertBSTRToString(modeName);
                fprintf(stderr, "%s: current mode: %s\n", __FUNCTION__, m);
                delete[] m;
            }
            SysFreeString(modeName);

            SAFE_RELEASE(inputMode);
        }

        SAFE_RELEASE(inputModeIterator);

        IBMDStreamingVideoEncodingModePresetIterator* presetIterator;

        if (SUCCEEDED(m_streamingDeviceInput->GetVideoEncodingModePresetIterator(currentInputModeValue, &presetIterator)))
        {
            int f_preset_done = 0;
            IBMDStreamingVideoEncodingMode* encodingMode = NULL;
            BSTR encodingModeName;

            while (presetIterator->Next(&encodingMode) == S_OK && !f_preset_done)
            {
                encodingMode->GetName(&encodingModeName);

                m = _com_util::ConvertBSTRToString(encodingModeName);
                fprintf(stderr, "%s: supported encoding preset mode: [%s]\n", __FUNCTION__, m);

                if (!strcmp(m_Preset, m))
                {
                    if (S_OK == m_streamingDeviceInput->SetVideoEncodingMode(encodingMode))
                    {
                        fprintf(stderr, "%s: encoding preset mode set: %s\n", __FUNCTION__, m_Preset);
                    }
                    else
                        fprintf(stderr, "%s: failed to set encoding preset mode: %s\n", __FUNCTION__, m_Preset);
                }

                delete[] m;

                SysFreeString(encodingModeName);

                encodingMode->Release();
            };

            SAFE_RELEASE(presetIterator);
        }

        IBMDStreamingVideoEncodingMode* currentVideoEncodingMode;
        if (SUCCEEDED(m_streamingDeviceInput->GetVideoEncodingMode(&currentVideoEncodingMode)))
        {
            char* ms;
            long long l;
            IBMDStreamingMutableVideoEncodingMode* m = NULL;
            if (m_PresetAlter)
            {
                if (S_OK != currentVideoEncodingMode->CreateMutableVideoEncodingMode(&m))
                    fprintf(stderr, "%s: CreateMutableVideoEncodingMode failed\n", __FUNCTION__);
            };

            BSTR encodingModeName;
            if (currentVideoEncodingMode->GetName(&encodingModeName) == S_OK)
            {
                ms = _com_util::ConvertBSTRToString(encodingModeName);
                fprintf(stderr, "%s: ENCODING: name=[%s]\n", __FUNCTION__, ms);
                delete[] ms;
                SysFreeString(encodingModeName);
            };

            /* source rect update */
            fprintf(stderr, "%s: ENCODING: source=[%d, %d, %d, %d]", __FUNCTION__,
                currentVideoEncodingMode->GetSourcePositionX(),
                currentVideoEncodingMode->GetSourcePositionY(),
                currentVideoEncodingMode->GetSourceWidth(),
                currentVideoEncodingMode->GetSourceHeight());
            if (m && m_SrcWidth && m_SrcHeight)
            {
                fprintf(stderr, " => [%d, %d, %d, %d]", m_SrcX, m_SrcY, m_SrcWidth, m_SrcHeight);
                if (S_OK != m->SetSourceRect(m_SrcX, m_SrcY, m_SrcWidth, m_SrcHeight))
                    fprintf(stderr, " FAILED");
            };
            fprintf(stderr, "\n");

            /* destination width */
            fprintf(stderr, "%s: ENCODING: destination=[%d, %d]", __FUNCTION__,
                currentVideoEncodingMode->GetDestWidth(),
                currentVideoEncodingMode->GetDestHeight());
            if (m && m_DstWidth && m_DstHeight)
            {
                fprintf(stderr, " => [%d, %d]", m_DstWidth, m_DstHeight);
                if (S_OK != m->SetDestSize(m_DstWidth, m_DstHeight))
                    fprintf(stderr, " FAILED");
            };
            fprintf(stderr, "\n");

            /* video bitrate */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyVideoBitRateKbps, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyVideoBitRateKbps failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: video bitrate=[%lld]Kbps", __FUNCTION__, l);
                if (m && m_VideoBitrateKbs)
                {
                    fprintf(stderr, " => [%d]Kbps", m_VideoBitrateKbs);
                    if (S_OK != m->SetInt(bmdStreamingEncodingPropertyVideoBitRateKbps, m_VideoBitrateKbs))
                        fprintf(stderr, " FAILED");
                };
            };
            fprintf(stderr, "\n");

            /* audio bitrate */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyAudioBitRateKbps, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyAudioBitRateKbps failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: audio bitrate=[%lld]Kbps", __FUNCTION__, l);
                if (m && m_AudioBitrateKbs)
                {
                    fprintf(stderr, " => [%d]Kbps", m_AudioBitrateKbs);
                    if (S_OK != m->SetInt(bmdStreamingEncodingPropertyAudioBitRateKbps, m_AudioBitrateKbs))
                        fprintf(stderr, " FAILED");
                };
            };
            fprintf(stderr, "\n");

            /* audio samplerate */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyAudioSampleRate, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyAudioSampleRate failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: audio samplerate=[%lld]Hz", __FUNCTION__, l);
                if (m && m_AudioSampleRate)
                {
                    fprintf(stderr, " => [%d]Hz", m_AudioSampleRate);
                    if (S_OK != m->SetInt(bmdStreamingEncodingPropertyAudioSampleRate, m_AudioSampleRate))
                        fprintf(stderr, " FAILED");
                };
            };
            fprintf(stderr, "\n");

            /* audio channels count */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyAudioChannelCount, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyAudioChannelCount failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: audio channels count=[%lld]", __FUNCTION__, l);
                if (m && m_AudioSampleRate)
                {
                    fprintf(stderr, " => [%d]", m_AudioChannelCount);
                    if (S_OK != m->SetInt(bmdStreamingEncodingPropertyAudioChannelCount, m_AudioChannelCount))
                        fprintf(stderr, " FAILED");
                };
            };
            fprintf(stderr, "\n");

            /* H264Level */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyH264Level, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyH264Level failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: H264Level=[%s]", __FUNCTION__,
                    BMDStreamingH264Level_to_str((BMDStreamingH264Level)l));
                if (m && m_Level[0])
                {
                    if(BMDStreamingH264Level_from_str(m_Level))
                    {
                        fprintf(stderr, " => [%s]", m_Level);
                        if (S_OK != m->SetInt(bmdStreamingEncodingPropertyH264Level,
                            BMDStreamingH264Level_from_str(m_Level)))
                            fprintf(stderr, " FAILED");
                    }
                    else
                        fprintf(stderr, " NOT RECOGNIZED [%s]", m_Level);
                };
            };
            fprintf(stderr, "\n");

            /* Profile */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyH264Profile, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyH264Profile failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: H264Profile=[%s]", __FUNCTION__,
                    BMDStreamingH264Profile_to_str((BMDStreamingH264Profile)l, "UNKNOWN"));
                if (m && m_Profile[0])
                {
                    if (BMDStreamingH264Profile_from_str(m_Profile))
                    {
                        fprintf(stderr, " => [%s]", m_Profile);
                        if (S_OK != m->SetInt(bmdStreamingEncodingPropertyH264Profile,
                            BMDStreamingH264Profile_from_str(m_Profile)))
                            fprintf(stderr, " FAILED");
                    }
                    else
                        fprintf(stderr, " NOT RECOGNIZED [%s]", m_Profile);
                };
            };
            fprintf(stderr, "\n");

            /* Entropy */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyH264EntropyCoding, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyH264EntropyCoding failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: H264EntropyCoding=[%s]", __FUNCTION__,
                    BMDStreamingH264EntropyCoding_to_str((BMDStreamingH264EntropyCoding)l, "UNKNOWN"));
                if (m && m_Entropy[0])
                {
                    if (BMDStreamingH264EntropyCoding_from_str(m_Entropy))
                    {
                        fprintf(stderr, " => [%s]", m_Entropy);
                        if (S_OK != m->SetInt(bmdStreamingEncodingPropertyH264EntropyCoding,
                            BMDStreamingH264EntropyCoding_from_str(m_Entropy)))
                            fprintf(stderr, " FAILED");
                    }
                    else
                        fprintf(stderr, " NOT RECOGNIZED [%s]", m_Entropy);

                };
            };
            fprintf(stderr, "\n");

            /* Framerate */
            if (S_OK != currentVideoEncodingMode->GetInt(bmdStreamingEncodingPropertyVideoFrameRate, &l))
                fprintf(stderr, "%s: bmdStreamingEncodingPropertyVideoFrameRate failed\n", __FUNCTION__);
            else
            {
                fprintf(stderr, "%s: ENCODING: FrameRate=[%s]", __FUNCTION__,
                    BMDStreamingEncodingFrameRate_to_str((BMDStreamingEncodingFrameRate)l, "UNKNOWN"));
                if (m && m_FrameRate[0])
                {
                    if (BMDStreamingEncodingFrameRate_from_str(m_FrameRate))
                    {
                        fprintf(stderr, " => [%s]", m_FrameRate);
                        if (S_OK != m->SetInt(bmdStreamingEncodingPropertyVideoFrameRate,
                            BMDStreamingEncodingFrameRate_from_str(m_FrameRate)))
                            fprintf(stderr, " FAILED");
                    }
                    else
                        fprintf(stderr, " NOT RECOGNIZED [%s]", m_FrameRate);

                };
            };
            fprintf(stderr, "\n");

            /* try to probe */
            if (m)
            {
                BMDStreamingEncodingSupport supp;
                IBMDStreamingVideoEncodingMode* m_new = NULL;

                hr = m_streamingDeviceInput->DoesSupportVideoEncodingMode(currentInputModeValue, m, &supp, &m_new);
                if (S_OK != hr)
                    fprintf(stderr, "%s: ERROR, DoesSupportVideoEncodingMode failed\n", __FUNCTION__);
                else
                {
                    IBMDStreamingVideoEncodingMode* m_sup = NULL;

                    if (supp == bmdStreamingEncodingModeNotSupported)
                        fprintf(stderr, "%s: ERROR, Altered mode not supported\n", __FUNCTION__);
                    else if (supp == bmdStreamingEncodingModeSupported)
                    {
                        fprintf(stderr, "%s: Altered mode fully supported\n", __FUNCTION__);
                        m_sup = m;
                    }
                    else if (supp == bmdStreamingEncodingModeSupportedWithChanges)
                    {
                        fprintf(stderr, "%s: Altered mode supported with changes\n", __FUNCTION__);
                        m_sup = m_new;

                        fprintf(stderr, "%s: ALTERED ENCODING: source=[%d, %d, %d, %d]\n", __FUNCTION__,
                            m_new->GetSourcePositionX(),
                            m_new->GetSourcePositionY(),
                            m_new->GetSourceWidth(),
                            m_new->GetSourceHeight());

                        fprintf(stderr, "%s: ALTERED ENCODING: destination=[%d, %d]\n", __FUNCTION__,
                            m_new->GetDestWidth(),
                            m_new->GetDestHeight());

                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyVideoBitRateKbps, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyVideoBitRateKbps failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: video bitrate=[%lld]Kbps\n", __FUNCTION__, l);

                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyVideoFrameRate, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyVideoFrameRate failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: video framerate=[%lld]Kbps\n", __FUNCTION__, l);

                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyAudioBitRateKbps, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyAudioBitRateKbps failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: audio bitrate=[%lld]Kbps\n", __FUNCTION__, l);

                        /* audio samplerate */
                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyAudioSampleRate, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyAudioSampleRate failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: audio samplerate=[%lld]Hz\n", __FUNCTION__, l);

                        /* audio channels count */
                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyAudioChannelCount, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyAudioChannelCount failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: audio channels count=[%lld]\n", __FUNCTION__, l);

                        /* level */
                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyH264Level, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyH264Level failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: H264Level=[%s]\n", __FUNCTION__,
                                BMDStreamingH264Level_to_str((BMDStreamingH264Level)l));

                        /* Profile */
                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyH264Profile, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyH264Profile failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: H264Profile=[%s]\n", __FUNCTION__,
                                BMDStreamingH264Profile_to_str((BMDStreamingH264Profile)l, "UNKNOWN"));

                        /* Entropy */
                        if (S_OK != m_new->GetInt(bmdStreamingEncodingPropertyH264EntropyCoding, &l))
                            fprintf(stderr, "%s: bmdStreamingEncodingPropertyH264EntropyCoding failed\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ALTERED ENCODING: H264EntropyCoding=[%s]\n", __FUNCTION__,
                                BMDStreamingH264EntropyCoding_to_str((BMDStreamingH264EntropyCoding)l, "UNKNOWN"));
                    };

                    if (m_sup)
                    {
                        hr = m_streamingDeviceInput->SetVideoEncodingMode(m_sup);
                        if (S_OK == hr)
                            fprintf(stderr, "%s: SetVideoEncodingMode(ALTERED)\n", __FUNCTION__);
                        else
                            fprintf(stderr, "%s: ERROR SetVideoEncodingMode(ALTERED) %s\n", __FUNCTION__, (E_FAIL == hr) ? "E_FAIL" : "E_INVALIDARG");
                    };

                    SAFE_RELEASE(m_new);
                };

                SAFE_RELEASE(m);
            };

            SAFE_RELEASE(currentVideoEncodingMode);
        };

        /* start capture */
        m_streamingDeviceInput->StartCapture();
        m_playing = true;
    };

    void							UpdateUIForNoDevice();
    void							UpdateUIForModeChanges()
    {
        fprintf(stderr, "%s\n", __FUNCTION__);
    };
    void							UpdateEncodingPresetsUIForInputMode();
    void							EncodingPresetsRemoveItems();

public:
    virtual HRESULT STDMETHODCALLTYPE H264NALPacketArrived(IBMDStreamingH264NALPacket* nalPacket)
    {
#if 0
        static unsigned char pp[] = { '/', '|', '\\', '-' };
        long s = nalPacket->GetPayloadSize();
        void* data;

        nalPacket->GetBytes(&data);

        fprintf(stderr, "%s: %5d bytes %c\r", __FUNCTION__, s, pp[(p1++) % 4]);

        fwrite(data, 1, s, stdout);
#endif
        return S_OK;
    };
    virtual HRESULT STDMETHODCALLTYPE H264AudioPacketArrived(IBMDStreamingAudioPacket* audioPacket){ return S_OK; };
    virtual HRESULT STDMETHODCALLTYPE MPEG2TSPacketArrived(IBMDStreamingMPEG2TSPacket* mpeg2TSPacket)
    {
        static unsigned char pp[] = { '/', '|', '\\', '-' };
        long s = mpeg2TSPacket->GetPayloadSize();
        void* data;

        mpeg2TSPacket->GetBytes(&data);

        if(!(p1 % DIV_RATIO))
        {
            fprintf(stderr, "%s: %5d bytes %c\r", __FUNCTION__, s, pp[(p1 / DIV_RATIO) % 4]);
        };

        p1++;

        /* save to file */
        if (file.descriptor)
            fwrite(data, 1, s, file.descriptor);

        /* send to stdout */
        if (file.stdout_descriptor)
            fwrite(data, 1, s, file.stdout_descriptor);

        /* send to UDP */
        if (188 == s && udp.socket > 0)
        {
            memcpy(udp.buf + 188 * udp.idx++, data, s);

            if(7 == udp.idx)
            {
                int l, r;

                /* send datagram */
                l = sizeof(struct sockaddr_in);
                r = sendto
                (
                    udp.socket,                     /* Socket to send result */
                    (const char*)udp.buf,           /* The datagram buffer */
                    188 * 7,                        /* The datagram lngth */
                    0,                              /* Flags: no options */
                    (struct sockaddr *)&udp.addr,   /* addr */
                    l                               /* Server address length */
                );

                udp.idx = 0;
            };
        };

        /* send to TCP */
        if (tcp.socket > 0)
        {
            send(tcp.socket, (const char*)data, s, 0);
        };

        return S_OK;
    };
    virtual HRESULT STDMETHODCALLTYPE H264VideoInputConnectorScanningChanged(void){ return E_NOTIMPL; };
    virtual HRESULT STDMETHODCALLTYPE H264VideoInputConnectorChanged(void){ return E_NOTIMPL; };
    virtual HRESULT STDMETHODCALLTYPE H264VideoInputModeChanged(void)
    {
        fprintf(stderr, "%s!\n", __FUNCTION__);

        UpdateUIForModeChanges();

        return S_OK;
    };
};

int main(int argc, char** argv)
{
    CStreamingOutput* strm;

    fprintf(stderr, "bmd_h264_cat built on " __DATE__ " " __TIME__ "\n");

    strm = new CStreamingOutput(argc, argv);
    if (strm->init() >= 0)
    {
        strm->start();

        getc(stdin);

        strm->stop();

        strm->release();
    }
    delete strm;

    return 0;
}
