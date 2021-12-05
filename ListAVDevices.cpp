//
// Created by mattb on 09.11.21.
//

#include "ListAVDevices.h"

#ifdef _WIN32

static std::string unicode2utf8(const WCHAR* uni) {
    static char temp[500];
    memset(temp, 0, 500);
    WideCharToMultiByte(CP_UTF8, 0, uni, -1, temp, 500, nullptr, nullptr);
    std::string res = std::string(temp);
    return res;
}

[[maybe_unused]] std::string GbkToUtf8(const char* src_str) {
    int len = MultiByteToWideChar(CP_ACP, 0, src_str, -1, nullptr, 0);
    auto* wstr = new wchar_t[len + 1];
    memset(wstr, 0, len + 1);
    MultiByteToWideChar(CP_ACP, 0, src_str, -1, wstr, len);
    len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    char* str = new char[len + 1];
    memset(str, 0, len + 1);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, nullptr, nullptr);
    std::string strTemp = str;
    delete[] wstr;
    delete[] str;
    return strTemp;
}

[[maybe_unused]] std::string Utf8ToGbk(const char* src_str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, src_str, -1, nullptr, 0);
    auto* wszGBK = new wchar_t[len + 1];
    memset(wszGBK, 0, len * 2 + 2);
    MultiByteToWideChar(CP_UTF8, 0, src_str, -1, wszGBK, len);
    len = WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, nullptr, 0, nullptr, nullptr);
    char* szGBK = new char[len + 1];
    WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, szGBK, len, nullptr, nullptr);
    std::string strTemp(szGBK);
    delete[] wszGBK;
    delete[] szGBK;
    return strTemp;
}

HRESULT DS_GetAudioVideoInputDevices(std::vector<std::string> &vectorDevices, const std::string& deviceType) {
    GUID guidValue;
    if(deviceType == "v") {
        guidValue = CLSID_VideoInputDeviceCategory;
    }
    else if(deviceType == "a") {
        guidValue = CLSID_AudioInputDeviceCategory;
    }
    else {
        throw std::invalid_argument("param deviceType must be 'a' or 'v'.");
    }
    HRESULT hr;
    vectorDevices.clear();
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if(FAILED(hr)) {
        return hr;
    }
    ICreateDevEnum *pSysDevEnum = nullptr;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void **) &pSysDevEnum);
    if(FAILED(hr)) {
        CoUninitialize();
        return hr;
    }
    IEnumMoniker* pEnumCat = nullptr;
    hr = pSysDevEnum->CreateClassEnumerator(guidValue, &pEnumCat, 0);
    if(hr == S_OK) {
        IMoniker* pMoniker = nullptr;
        ULONG cFetched;
        while(pEnumCat->Next(1, &pMoniker, &cFetched) == S_OK) {
            IPropertyBag* pPropBag;
            hr = pMoniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void **) &pPropBag);
            if(SUCCEEDED(hr)) {
                VARIANT varName;
                VariantInit(&varName);
                hr=pPropBag->Read(L"FriendlyName", &varName, nullptr);
                if(SUCCEEDED(hr)) {
                    vectorDevices.push_back(unicode2utf8(varName.bstrVal));
                }
                VariantClear(&varName);
                pPropBag->Release();
            }
            pMoniker->Release();
        }
        pEnumCat->Release();
    }
    pSysDevEnum->Release();
    CoUninitialize();
    return hr;
}

std::string DS_GetDefaultDevice(const std::string& type) {
    if(type == "a") {
        std::vector<std::string> v;
        int ret = DS_GetAudioVideoInputDevices(v, "a");
        if(ret >= 0 && !v.empty()) {
            return v[0];
        }
        else {
            return "";
        }
    }
    else if(type == "v") {
        std::vector<std::string> v;
        int ret = DS_GetAudioVideoInputDevices(v, "v");
        if(ret >= 0 && !v.empty()) {
            return v[0];
        }
        else {
            return "";
        }
    }
    else {
        throw std::invalid_argument("param type must be 'a' or 'v'.");
    }
}

#endif