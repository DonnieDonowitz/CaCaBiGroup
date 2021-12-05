#include "ScreenAudioCapture.h"
#include <string>
#include <iostream>
#include <algorithm>
#include <regex>
#ifdef _WIN32
#include <winuser.h>
#include <shlobj.h>
#endif

static std::string toUpperCase(std::string src)
{
    std::string dst = "";
    for (char const &c : src)
    {
        dst += toupper(c);
    }
    return dst;
}

bool GetScreenResolution(int *screenWidth, int *screenHeight)
{
#ifdef WIN32
    if (SetProcessDPIAware())
    {
        *screenWidth = GetSystemMetrics(SM_CXSCREEN);
        *screenHeight = GetSystemMetrics(SM_CYSCREEN);
        return true;
    }
    return false;
#elif __APPLE__
    return false;
#endif
    return false;
}

bool IsPositiveNumber(const std::string &s)
{
    return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c)
                                      { return !std::isdigit(c); }) == s.end();
}

bool IsEmptyOrSpaces(std::string str)
{
    return str.empty() || str.find_first_not_of(' ') == std::string::npos;
}

void AskAndSetDimensions(ScreenAudioCapture *screenAudioCapture)
{
    int mainScreenWidth = 0;
    int mainScreenHeight = 0;
    if (!GetScreenResolution(&mainScreenWidth, &mainScreenHeight))
    {
        std::cout << "Couldn't get screen sizes. Insert them manually. If inserted wrong, program could fail." << std::endl;
        std::string mainScreenWidthStr;
        std::cout << "Screen width (min=320, max=15360): ";
        std::getline(std::cin, mainScreenWidthStr);
        while (!IsPositiveNumber(mainScreenWidthStr))
        {
            std::cout << "Insert a positive number." << std::endl;
            std::cout << "Screen width (min=320, max=15360): ";
            std::getline(std::cin, mainScreenWidthStr);
        }
        mainScreenWidth = std::stoi(mainScreenWidthStr);
        while (mainScreenWidth < 320 || mainScreenWidth > 15360)
        {
            mainScreenWidthStr = "";
            std::cout << "Not in the range. Try again." << std::endl;
            std::cout << "Screen width (min=320, max=15360): ";
            std::getline(std::cin, mainScreenWidthStr);
            while (!IsPositiveNumber(mainScreenWidthStr))
            {
                std::cout << "Insert a positive number." << std::endl;
                std::cout << "Screen width (min=320, max=15360): ";
                std::getline(std::cin, mainScreenWidthStr);
            }
            mainScreenWidth = std::stoi(mainScreenWidthStr);
        }
        std::cout << std::endl;

        std::string mainScreenHeightStr;
        std::cout << "Screen height (min=240, max=8640): ";
        std::getline(std::cin, mainScreenHeightStr);
        while (!IsPositiveNumber(mainScreenHeightStr))
        {
            std::cout << "Insert a positive number." << std::endl;
            std::cout << "Screen height (min=240, max=8640): ";
            std::getline(std::cin, mainScreenHeightStr);
        }
        mainScreenHeight = std::stoi(mainScreenHeightStr);
        while (mainScreenHeight < 320 || mainScreenHeight > 15360)
        {
            mainScreenHeightStr = "";
            std::cout << "Not in the range. Try again." << std::endl;
            std::cout << "Screen height (min=240, max=8640): ";
            std::getline(std::cin, mainScreenHeightStr);
            while (!IsPositiveNumber(mainScreenHeightStr))
            {
                std::cout << "Insert a positive number." << std::endl;
                std::cout << "Screen height (min=240, max=8640): ";
                std::getline(std::cin, mainScreenHeightStr);
            }
            mainScreenHeight = std::stoi(mainScreenHeightStr);
        }
        std::cout << std::endl;
    }

    int width;
    int widthOffset;
    int height;
    int heightOffset;

    std::string widthStr;
    std::cout << "Insert width of output video (interval 240:" << mainScreenWidth << ", default: " << mainScreenWidth << "): ";
    std::getline(std::cin, widthStr);
    while (!IsPositiveNumber(widthStr) && !IsEmptyOrSpaces(widthStr))
    {
        std::cout << "Insert a positive number." << std::endl;
        std::cout << "Insert width of output video (interval 240:" << mainScreenWidth << ", default: " << mainScreenWidth << "): ";
        std::getline(std::cin, widthStr);
    }
    width = IsEmptyOrSpaces(widthStr) ? mainScreenWidth : std::stoi(widthStr);
    while (width < 240 || width > mainScreenWidth)
    {
        widthStr = "";
        std::cout << "Not in the range. Try again." << std::endl;
        std::cout << "Insert width of output video (interval 240:" << mainScreenWidth << ", default: " << mainScreenWidth << "): ";
        std::getline(std::cin, widthStr);
        while (!IsPositiveNumber(widthStr) && !IsEmptyOrSpaces(widthStr))
        {
            std::cout << "Insert a positive number." << std::endl;
            std::cout << "Insert width of output video (interval 240:" << mainScreenWidth << ", default: " << mainScreenWidth << "): ";
            std::getline(std::cin, widthStr);
        }
        width = IsEmptyOrSpaces(widthStr) ? mainScreenWidth : std::stoi(widthStr);
    }
    std::cout << std::endl;

    std::string widthOffsetStr;
    std::cout << "Starting from pixel: (interval 0:" << (mainScreenWidth - width) << ", default: 0): ";
    std::getline(std::cin, widthOffsetStr);
    while (!IsPositiveNumber(widthOffsetStr) && !IsEmptyOrSpaces(widthOffsetStr))
    {
        std::cout << "Insert a positive number." << std::endl;
        std::cout << "Starting from pixel: (interval 0:" << (mainScreenWidth - width) << ", default: 0): ";
        std::getline(std::cin, widthOffsetStr);
    }
    widthOffset = IsEmptyOrSpaces(widthOffsetStr) ? 0 : std::stoi(widthOffsetStr);
    while (widthOffset > mainScreenWidth - width)
    {
        widthOffsetStr = "";
        std::cout << "Not in the range. Try again." << std::endl;
        std::cout << "Starting from pixel: (interval 0:" << (mainScreenWidth - width) << ", default: 0): ";
        std::getline(std::cin, widthOffsetStr);
        while (!IsPositiveNumber(widthOffsetStr) && !IsEmptyOrSpaces(widthOffsetStr))
        {
            std::cout << "Insert a positive number." << std::endl;
            std::cout << "Starting from pixel: (interval 0:" << (mainScreenWidth - width) << ", default: 0): ";
            std::getline(std::cin, widthOffsetStr);
        }
        widthOffset = IsEmptyOrSpaces(widthOffsetStr) ? 0 : std::stoi(widthOffsetStr);
    }
    std::cout << std::endl;

    std::string heightStr;
    std::cout << "Insert height of output video (interval 240:" << mainScreenHeight << ", default: " << mainScreenHeight << "): ";
    std::getline(std::cin, heightStr);
    while (!IsPositiveNumber(heightStr) && !IsEmptyOrSpaces(heightStr))
    {
        std::cout << "Insert a positive number." << std::endl;
        std::cout << "Insert height of output video (interval 240:" << mainScreenHeight << ", default: " << mainScreenHeight << "): ";
        std::getline(std::cin, heightStr);
    }
    height = IsEmptyOrSpaces(heightStr) ? mainScreenHeight : std::stoi(heightStr);
    while (height < 240 || height > mainScreenHeight)
    {
        heightStr = "";
        std::cout << "Not in the range. Try again." << std::endl;
        std::cout << "Insert height of output video (interval 240:" << mainScreenHeight << ", default: " << mainScreenHeight << "): ";
        std::getline(std::cin, heightStr);
        while (!IsPositiveNumber(heightStr) && !IsEmptyOrSpaces(heightStr))
        {
            std::cout << "Insert a positive number." << std::endl;
            std::cout << "Insert height of output video (interval 240:" << mainScreenHeight << ", default: " << mainScreenHeight << "): ";
            std::getline(std::cin, heightStr);
        }
        height = IsEmptyOrSpaces(heightStr) ? mainScreenHeight : std::stoi(heightStr);
    }
    std::cout << std::endl;

    std::string heightOffsetStr;
    std::cout << "Starting from pixel: (interval 0:" << (mainScreenHeight - height) << ", default: 0): ";
    std::getline(std::cin, heightOffsetStr);
    while (!IsPositiveNumber(heightOffsetStr) && !IsEmptyOrSpaces(heightOffsetStr))
    {
        std::cout << "Insert a positive number." << std::endl;
        std::cout << "Starting from pixel: (interval 0:" << (mainScreenHeight - height) << ", default: 0): ";
        std::getline(std::cin, heightOffsetStr);
    }
    heightOffset = IsEmptyOrSpaces(heightOffsetStr) ? 0 : std::stoi(heightOffsetStr);
    while (heightOffset > mainScreenHeight - height)
    {
        heightOffsetStr = "";
        std::cout << "Not in the range. Try again." << std::endl;
        std::cout << "Starting from pixel: (interval 0:" << (mainScreenHeight - height) << ", default: 0): ";
        std::getline(std::cin, heightOffsetStr);
        while (!IsPositiveNumber(heightOffsetStr) && !IsEmptyOrSpaces(heightOffsetStr))
        {
            std::cout << "Insert a positive number." << std::endl;
            std::cout << "Starting from pixel: (interval 0:" << (mainScreenHeight - height) << ", default: 0): ";
            std::getline(std::cin, heightOffsetStr);
        }
        heightOffset = IsEmptyOrSpaces(heightOffsetStr) ? 0 : std::stoi(heightOffsetStr);
    }
    std::cout << std::endl;
    screenAudioCapture->SetDimensions(width, widthOffset, height, heightOffset);
}

bool EndsWith(std::string const &fullString, std::string const &ending)
{
    if (fullString.length() >= ending.length())
    {
        return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
    }
    else
    {
        return false;
    }
}

#ifdef WIN32
std::wstring s2ws(const std::string &s)
{
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
    wchar_t *buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;
    return r;
}
#endif

bool IsValidFileName(std::string filenameStr)
{
#ifdef WIN32
    std::wstring stemp = s2ws(filenameStr);
    LPCWSTR filename = stemp.c_str();
    WCHAR valid_invalid[MAX_PATH];
    wcscpy_s(valid_invalid, filename);
    int result = PathCleanupSpec(nullptr, valid_invalid);
    // If return value is non-zero, or if 'valid_invalid'
    // is modified, file-name is assumed invalid
    return result == 0 && wcsicmp(valid_invalid, filename) == 0;
#endif
    return true;
}

std::string AskForOutputFile()
{
    std::string outputFile;
    std::cout << "Insert name for output video (it will be saved as mp4 file in the running directory, default is 'output'): ";
    std::getline(std::cin, outputFile);
    while (!IsEmptyOrSpaces(outputFile) && !IsValidFileName(outputFile))
    {
        std::cout << "Not a valid file name. Try again." << std::endl;
        std::cout << "Insert name for output video (it will be saved as .mp4 in the running directory, default is 'output'): ";
        std::getline(std::cin, outputFile);
    }
    std::cout << std::endl;
    if (IsEmptyOrSpaces(outputFile))
    {
        std::string tmp("output");
        outputFile = tmp;
    }
    if (EndsWith(outputFile, ".mp4"))
    {
        outputFile = outputFile.substr(0, outputFile.length() - 4);
    }
    if (IsEmptyOrSpaces(outputFile))
    {
        std::string tmp("output");
        outputFile = tmp;
    }
    return outputFile;
}

int main()
{
    std::cout
        << "======================================================================================================================" << std::endl
        << "================================================ SCREEN-AUDIO CAPTURE ================================================" << std::endl
        << "======================================================================================================================" << std::endl
        << std::endl
        << "Developed for project of \"Programmazione di Sistema\"" << std::endl
        << "It allows to record desktop screen (optionally with audio) in customizable size and save output video on specified file" << std::endl
        << "Authors: Angelo Carmollingo - Matteo Biffoni - Simone Cavallo" << std::endl
        << std::endl;
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
    std::string outputFile = AskForOutputFile();
    ScreenAudioCapture screenAudioCapture{outputFile.append(".mp4"), ""};
    AskAndSetDimensions(&screenAudioCapture);
    screenAudioCapture.PrintDimensions();
    try
    {
        std::cout << std::endl
                  << std::endl;
        std::cout << "Type 'start' to start recording, then available commands will be 'pause', 'resume' and 'stop'." << std::endl
                  << std::endl;
        while (true)
        {
            try
            {
                std::string command;
                std::getline(std::cin, command);
                std::cout << std::endl;
                command = toUpperCase(command);
                if (command == "START")
                {
                    screenAudioCapture.Start();
                }
                else if (command == "PAUSE")
                {
                    screenAudioCapture.Pause();
                }
                else if (command == "RESUME")
                {
                    screenAudioCapture.Resume();
                }
                else if (command == "STOP")
                {
                    screenAudioCapture.Stop();
                    break;
                }
                else
                {
                    throw std::runtime_error("Invalid command, try again.");
                }
            }
            catch (std::runtime_error e)
            {
                if (screenAudioCapture.WasFatal())
                    throw std::runtime_error(e.what());
                else
                    std::cout << e.what() << std::endl;
            }
        }
        if (screenAudioCapture.DoneSomething())
        {
            std::cout << "Cleaning up remaining data..." << std::endl
                      << std::endl;
            while (!screenAudioCapture.hasFinished())
                ;
            screenAudioCapture.Release();
        }
        std::string reason = screenAudioCapture.GetLastError();
        if (!reason.empty())
        {
            throw std::runtime_error(reason);
        }
    }
    catch (std::exception &e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        exit(-1);
    }
    return 0;
}