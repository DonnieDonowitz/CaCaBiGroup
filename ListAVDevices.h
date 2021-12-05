//
// Created by mattb on 09.11.21.
//

#ifndef SCREENAUDIOCAPTURE_LISTAVDEVICES_H
#define SCREENAUDIOCAPTURE_LISTAVDEVICES_H

#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <vector>
#include <dshow.h>
#include <string>
#include <stdexcept>
#include <iostream>

#ifndef MACRO_GROUP_DEVICENAME
#define MACRO_GROUP_DEVICENAME

#endif

HRESULT DS_GetAudioVideoInputDevices(std::vector<std::string> &vectorDevices, const std::string& deviceType);

[[maybe_unused]] std::string GbkToUtf8(const char* src_str);

[[maybe_unused]] std::string Utf8ToGbk(const char* src_str);

std::string DS_GetDefaultDevice(const std::string& type);

#endif
#endif //SCREENAUDIOCAPTURE_LISTAVDEVICES_H
