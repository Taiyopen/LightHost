#include "LoopbackDevices.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <functiondiscoverykeys_devpkey.h>

namespace
{
    /** 只在本次呼叫真正初始化 COM 時才 Uninitialize，避免破壞 UI 執行緒既有的 COM 狀態 */
    struct ComScope
    {
        ComScope()  { hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED); }
        ~ComScope() { if (hr == S_OK) CoUninitialize(); }

        bool isReady() const noexcept
        {
            return hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;
        }

        HRESULT hr = RPC_E_CHANGED_MODE;
    };
}
#endif

juce::Array<LoopbackDeviceInfo> enumerateLoopbackOutputDevices()
{
    juce::Array<LoopbackDeviceInfo> devices;

   #if JUCE_WINDOWS
    const ComScope com;

    if (! com.isReady())
        return devices;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;

    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof (IMMDeviceEnumerator),
                                     reinterpret_cast<void**> (&enumerator)))
        && SUCCEEDED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &collection)))
    {
        UINT count = 0;
        collection->GetCount (&count);

        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* device = nullptr;
            if (FAILED (collection->Item (i, &device)) || device == nullptr)
                continue;

            LPWSTR idStr = nullptr;
            juce::String deviceId;
            if (SUCCEEDED (device->GetId (&idStr)) && idStr != nullptr)
            {
                deviceId = juce::String (idStr);
                CoTaskMemFree (idStr);
            }

            juce::String friendlyName;
            IPropertyStore* props = nullptr;
            if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &props)))
            {
                PROPVARIANT value;
                PropVariantInit (&value);
                if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &value)))
                {
                    if (value.vt == VT_LPWSTR && value.pwszVal != nullptr)
                        friendlyName = juce::String (value.pwszVal);

                    PropVariantClear (&value);
                }
                props->Release();
            }

            if (friendlyName.isEmpty())
                friendlyName = deviceId;

            devices.add ({ deviceId, friendlyName });
            device->Release();
        }

        collection->Release();
    }

    if (enumerator != nullptr)
        enumerator->Release();
   #endif

    return devices;
}

juce::String getDefaultLoopbackDeviceName()
{
   #if JUCE_WINDOWS
    const ComScope com;

    if (! com.isReady())
        return "Windows Default";

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    juce::String name = "Windows Default";

    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof (IMMDeviceEnumerator),
                                     reinterpret_cast<void**> (&enumerator)))
        && SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &device)))
    {
        IPropertyStore* props = nullptr;
        if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &props)))
        {
            PROPVARIANT value;
            PropVariantInit (&value);
            if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &value)))
            {
                if (value.vt == VT_LPWSTR && value.pwszVal != nullptr)
                    name = juce::String (value.pwszVal);

                PropVariantClear (&value);
            }
            props->Release();
        }
        device->Release();
    }

    if (enumerator != nullptr)
        enumerator->Release();

    return name;
   #else
    return "Default Output";
   #endif
}
