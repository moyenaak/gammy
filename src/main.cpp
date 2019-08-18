/**
 * Copyright (C) 2019 Francesco Fusco. All rights reserved.
 * License: https://github.com/Fushko/gammy#license
 */

#include <QApplication>
#include <QLabel>

#include "mainwindow.h"
#include "main.h"

#ifdef _WIN32
    #include <Windows.h>
    #include "dxgidupl.h"

    #pragma comment(lib, "gdi32.lib")
    #pragma comment(lib, "user32.lib")
    #pragma comment(lib, "DXGI.lib")
    #pragma comment(lib, "D3D11.lib")
    #pragma comment(lib, "Advapi32.lib")
#elif __linux__
    #include "x11.h"
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
    #include <signal.h>
#endif

#include <array>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>

int scrBr = default_brightness; //Current screen brightness

int	polling_rate_min = 10;
int	polling_rate_max = 500;

std::array<std::pair<std::string, int>, cfg_count> cfg =
{{
   { min_br_str, 176 },
   { max_br_str, 255 },
   { offset_str, 70 },
   { temp_str, 1 },
   { speed_str, 3 },
   { threshold_str, 32 },
   { polling_rate_str, 100 }
}};

#ifdef _WIN32
const HDC screenDC = GetDC(nullptr);
const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN) - GetSystemMetrics(SM_XVIRTUALSCREEN);
const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN) - GetSystemMetrics(SM_YVIRTUALSCREEN);
const int screenRes = w * h;
#else
X11 x11;
const int screenRes = x11.getWidth() * x11.getHeight();
#endif

const unsigned bufLen = screenRes * 4;

int calcBrightness(uint8_t* buf)
{

    int r_sum = 0;
    int g_sum = 0;
    int b_sum = 0;

    for (int i = bufLen - 4; i > 0; i -= 4)
    {
        r_sum += buf[i + 2];
        g_sum += buf[i + 1];
        b_sum += buf[i];
    }

    int luma = int((r_sum * 0.2126f + g_sum * 0.7152f + b_sum * 0.0722f)) / screenRes;

#ifdef dbgluma
    std::cout << "\nRed: " << r_sum << '\n';
    std::cout << "Green: " << g_sum << '\n';
    std::cout << "Blue: " << b_sum << '\n';
    std::cout << "Luma:" << luma << '\n';
#endif

    return luma;
}

struct Args
{
    int img_br = 0;
    int target_br = 0;
    float img_delta = 0;
    size_t callcnt = 0;

    std::mutex mtx;
    std::condition_variable cvr;
    MainWindow* w = nullptr;
};

void adjustBrightness(Args &args)
{
    size_t c = 0;
    size_t old_c = 0;

    while(!args.w->quit)
    {
        {
#ifdef dbgthr
            std::cout << "adjustBrightness: waiting (" << c << ")\n";
#endif
            std::unique_lock<std::mutex> lock(args.mtx);
            args.cvr.wait(lock, [&]{return args.callcnt > old_c;});
        }

        c = args.callcnt;

#ifdef dbgthr
        std::cout << "adjustBrightness: working (" << c << ")\n";
#endif

        int sleeptime = (100 - int(args.img_delta) / 4) / cfg[Speed].second;
        args.img_delta = 0;

        if (args.target_br > cfg[MaxBr].second)
        {
             args.target_br = cfg[MaxBr].second;
        }
        else if (args.target_br < cfg[MinBr].second)
        {
             args.target_br = cfg[MinBr].second;
        }

        if (scrBr < args.target_br) sleeptime /= 3;

        while (scrBr != args.target_br && c == args.callcnt)
        {
            if (scrBr < args.target_br)
            {
                ++scrBr;
            }
            else --scrBr;

            if(!args.w->quit)
            {
                #ifdef _WIN32
                setGDIBrightness(scrBr, cfg[Temp].second);
                #else
                x11.setXF86Brightness(scrBr, cfg[Temp].second);
                #endif
            }

            if(args.w->isVisible()) args.w->updateBrLabel();

            if (scrBr == cfg[MinBr].second || scrBr == cfg[MaxBr].second)
            {
                args.target_br = scrBr;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(sleeptime));
        }

        old_c = c;

#ifdef dbgthr
        std::cout << "adjustBrightness: complete (" << c << ")\n";
#endif
    }
}

void app(MainWindow* wnd, Args &args)
{
    #ifdef dbg
    std::cout << "Starting screenshots\n";
    #endif

    int old_imgBr  = default_brightness;
    int old_min    = default_brightness;
    int old_max    = default_brightness;
    int old_offset = default_brightness;

    //Buffer to store screen pixels
    uint8_t* buf = new uint8_t[bufLen];

    bool forceChange = false;

    #ifdef _WIN32
    DXGIDupl dx;
    bool useDXGI = dx.initDXGI();

    if (!useDXGI)
    {
        polling_rate_min = 1000;
        polling_rate_max = 5000;
        wnd->updatePollingSlider(polling_rate_min, polling_rate_max);
    }
    #else
    x11.setXF86Brightness(scrBr, cfg[Temp].second);
    #endif

    std::once_flag f;

    while (!wnd->quit)
    {
#ifdef _WIN32
        if (useDXGI)
        {
            while (!dx.getDXGISnapshot(buf))
            {
                dx.restartDXGI();
            }
        }
        else
        {
            getGDISnapshot(buf);
        }
#else
        x11.getX11Snapshot(buf);
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg[Polling_rate].second));
#endif

        args.img_br = calcBrightness(buf);
        args.img_delta += abs(old_imgBr - args.img_br);

        std::call_once(f, [&](){ args.img_delta = 0; });

        if (args.img_delta > cfg[Threshold].second || forceChange)
        {
            args.target_br = default_brightness - args.img_br + cfg[Offset].second;
            if(args.target_br > default_brightness) args.target_br = default_brightness;

            if(args.target_br != scrBr)
            {
                ++args.callcnt;
    #ifdef dbgthr
                std::cout << "app: ready (" << args.callcnt << ")\n";
    #endif
                args.cvr.notify_one();

                forceChange = false;
            }
            else args.img_delta = 0;
        }

        if (cfg[MinBr].second != old_min || cfg[MaxBr].second != old_max || cfg[Offset].second != old_offset)
        {
            forceChange = true;
        }

        old_imgBr  = args.img_br;
        old_min    = cfg[MinBr].second;
        old_max    = cfg[MaxBr].second;
        old_offset = cfg[Offset].second;
    }

#ifdef _WIN32
    setGDIBrightness(default_brightness, 1);
#else
    x11.setInitialGamma(false);
#endif

    ++args.callcnt;
    #ifdef dbgthr
        std::cout << "app: notifying to quit (" << args.callcnt << ")\n";
    #endif
    args.cvr.notify_one();

    delete[] buf;
    QApplication::quit();
}

int main(int argc, char *argv[])
{
    checkInstance();
#ifdef _WIN32
    #ifdef dbg
    FILE *f1, *f2, *f3;
    AllocConsole();
    freopen_s(&f1, "CONIN$", "r", stdin);
    freopen_s(&f2, "CONOUT$", "w", stdout);
    freopen_s(&f3, "CONOUT$", "w", stderr);
    #endif

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    checkGammaRange();
#else
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
        std::cout << "Error: can't catch SIGINT\n";
    }
    if (signal(SIGQUIT, sig_handler) == SIG_ERR)
    {
        std::cout << "Error: can't catch SIGQUIT\n";
    }
    if (signal(SIGTERM, sig_handler) == SIG_ERR)
    {
        std::cout << "Error: can't catch SIGTERM\n";
    }
#endif

    QApplication a(argc, argv);
    MainWindow wnd;
    //wnd.show();

    Args args;
    args.w = &wnd;

    std::thread t1(adjustBrightness, std::ref(args));
    std::thread t2(app, &wnd, std::ref(args));

    a.exec();
    t1.join();
    t2.join();
}

//_______________________________________________________

void readConfig()
{
#ifdef dbg
    std::cout << "Opening config...\n";
#endif

#ifdef _WIN32
    const std::wstring path = getExecutablePath(true);
#else
    const std::string path = getHomePath(true);
#endif

    std::fstream file(path, std::fstream::in | std::fstream::out | std::fstream::app);

    if(!file.is_open())
    {
        #ifdef dbg
        std::cout << "Unable to open settings file.\n";
        #endif
        return;
    }

    file.seekg(0, std::ios::end);
    bool empty = file.tellg() == 0;

    if(empty)
    {
        #ifdef dbg
        std::cout << "Config empty. Writing defaults...\n";
        #endif

        size_t c = 0;
        for(const auto &s : cfg[c++].first) file << s << '\n';

        file.close();
        return;
    }

    file.seekg(0);

    size_t c = 0;
    for (std::string line; std::getline(file, line);)
    {
        #ifdef dbg
        std::cout << line << '\n';
        #endif

        if(!line.empty())
        {
            size_t pos = line.find('=') + 1;
            std::string val = line.substr(pos);

            cfg[c++].second = std::stoi(val);
        }
    }

    file.close();
}

void updateConfig()
{
#ifdef dbg
    std::cout << "Saving config... ";
#endif

#ifdef _WIN32
    static const std::wstring path = getExecutablePath(true);
#else
    static const std::string path = getHomePath(true);
#endif

    std::ofstream file(path, std::ofstream::out | std::ofstream::trunc);

    if(file.good() && file.is_open())
    {
        for(size_t i = 0; i < cfg_count; i++)
        {
            std::string s = cfg[i].first;
            int val = cfg[i].second;

            std::string line (s + std::to_string(val));

            file << line << '\n';
        }

        file.close();
    }
}

void checkInstance()
{
    #ifdef _WIN32
    HANDLE hStartEvent = CreateEventA(nullptr, true, false, "Gammy");

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hStartEvent);
        hStartEvent = nullptr;
        exit(0);
    }
    #else
        //@TODO: Avoid multiple instances on linux
    #endif
}

#ifdef __linux
std::string getHomePath(bool add_cfg)
{
    const char* home_path;

    if ((home_path = getenv("HOME")) == nullptr)
    {
        home_path = getpwuid(getuid())->pw_dir;
    }

    std::string path(home_path);

    if(add_cfg) path += "/.gammy";

#ifdef dbg
    std::cout << "Path: " << path << '\n';
#endif

    return path;
}

void sig_handler(int signo)
{
    switch(signo)
    {
        case SIGINT:
        {
            #ifdef dbg
            std::cout << "Received SIGINT.\n";
            #endif
            break;
        }
        case SIGTERM:
        {
            #ifdef dbg
            std::cout << "Received SIGTERM.\n";
            #endif
            break;
        }
        case SIGQUIT:
        {
            #ifdef dbg
            std::cout << "Received SIGQUIT.\n";
            #endif
            break;
        }
    }

    updateConfig();
    x11.setInitialGamma(false);
    _exit(0);
}
#else

void getGDISnapshot(uint8_t* buf)
{
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, w, h);
    HDC memoryDC = CreateCompatibleDC(screenDC); //Memory DC for GDI screenshot

    HGDIOBJ oldObj = SelectObject(memoryDC, hBitmap);

    BitBlt(memoryDC, 0, 0, w, h, screenDC, 0, 0, SRCCOPY); //Slow, but works on Win7 and below

    //GetObject(hScreen, sizeof(screen), &screen);
    //int width(screen.bmWidth), height(screen.bmHeight);
    BITMAPINFOHEADER bminfoheader;
    ::ZeroMemory(&bminfoheader, sizeof(BITMAPINFOHEADER));
    bminfoheader.biSize = sizeof(BITMAPINFOHEADER);
    bminfoheader.biWidth = w;
    bminfoheader.biHeight = -h;
    bminfoheader.biPlanes = 1;
    bminfoheader.biBitCount = 32;
    bminfoheader.biCompression = BI_RGB;
    bminfoheader.biSizeImage = bufLen;
    bminfoheader.biClrUsed = 0;
    bminfoheader.biClrImportant = 0;

    GetDIBits(memoryDC, hBitmap, 0, h, buf, LPBITMAPINFO(&bminfoheader), DIB_RGB_COLORS);
    Sleep(cfg[Polling_rate].second);

    SelectObject(memoryDC, oldObj);
    DeleteObject(hBitmap);
    DeleteObject(oldObj);
    DeleteDC(memoryDC);
}

void setGDIBrightness(WORD brightness, int temp)
{

    if (brightness > default_brightness) {
        return;
    }

    WORD gammaArr[3][256];

    float gdiv = 1;
    float bdiv = 1;
    float val = temp;

    if(temp > 1)
    {
        bdiv += (val / 100);
        gdiv += (val / 270);
    }

    for (WORD i = 0; i < 256; ++i)
    {
        WORD gammaVal = i * brightness;

        gammaArr[0][i] = WORD (gammaVal);
        gammaArr[1][i] = WORD (gammaVal / gdiv);
        gammaArr[2][i] = WORD (gammaVal / bdiv);
    }

    SetDeviceGammaRamp(screenDC, LPVOID(gammaArr));
}

void checkGammaRange()
{
    LPCWSTR subKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM";
    LSTATUS s;

    s = RegGetValueW(HKEY_LOCAL_MACHINE, subKey, L"GdiICMGammaRange", RRF_RT_REG_DWORD, nullptr, nullptr, nullptr);

    if (s == ERROR_SUCCESS)
    {
#ifdef dbg
        std::cout << "Gamma registry key found.\n";
#endif
        return;
    }

#ifdef dbg
        std::cout << "Gamma registry key not found. Creating one...\n";
#endif

    HKEY hKey;

    s = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);

    if (s == ERROR_SUCCESS)
    {
#ifdef dbg
        std::cout << "Gamma registry key created.\n";
#endif
        DWORD val = 256;

        s = RegSetValueExW(hKey, L"GdiICMGammaRange", 0, REG_DWORD, LPBYTE(&val), sizeof(val));

        if (s == ERROR_SUCCESS)
        {
            MessageBoxW(nullptr, L"Gammy has extended the brightness range. Restart to apply the changes.", L"Gammy", 0);
#ifdef dbg
            std::cout << "Gamma registry value set.\n";
#endif
        }
#ifdef dbg
        else std::cout << "Error when setting Gamma registry value.\n";
#endif
    }
#ifdef dbg
    else
    {
        std::cout << "Error when creating/opening gamma RegKey.\n";

        if (s == ERROR_ACCESS_DENIED)
        {
            std::cout << "Access denied.\n";
        }
    }
#endif

    if (hKey) RegCloseKey(hKey);
}

void toggleRegkey(bool isChecked)
{
    LSTATUS s;
    HKEY hKey = nullptr;
    LPCWSTR subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    if (isChecked)
    {
        WCHAR path[MAX_PATH + 3], tmpPath[MAX_PATH + 3];
        GetModuleFileName(nullptr, tmpPath, MAX_PATH + 1);
        wsprintfW(path, L"\"%s\"", tmpPath);

        s = RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &hKey, nullptr);

        if (s == ERROR_SUCCESS)
        {
            #ifdef dbg
            std::cout << "RegKey opened.\n";
            #endif

            s = RegSetValueExW(hKey, L"Gammy", 0, REG_SZ, LPBYTE(path), int((wcslen(path) * sizeof(WCHAR))));

            #ifdef dbg
                if (s == ERROR_SUCCESS) {
                    std::cout << "RegValue set.\n";
                }
                else {
                    std::cout << "Error when setting RegValue.\n";
                }
            #endif
        }
        #ifdef dbg
        else {
            std::cout << "Error when opening RegKey.\n";
        }
        #endif
    }
    else {
        s = RegDeleteKeyValueW(HKEY_CURRENT_USER, subKey, L"Gammy");

        #ifdef dbg
            if (s == ERROR_SUCCESS)
                std::cout << "RegValue deleted.\n";
            else
                std::cout << "RegValue deletion failed.\n";
        #endif
    }

    if(hKey) RegCloseKey(hKey);
}

std::wstring getExecutablePath(bool add_cfg)
{
    wchar_t buf[FILENAME_MAX] {};
    GetModuleFileNameW(nullptr, buf, FILENAME_MAX);
    std::wstring path(buf);

    std::wstring appname = L"Gammy.exe";
    path.erase(path.find(appname), appname.length());

    if(add_cfg) path += L"gammysettings.cfg";

    return path;
}
#endif
