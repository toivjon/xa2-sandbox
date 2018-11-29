// ============================================================================
// XAudio2 - Core Concepts
// The heart of the engine is the IXAudio2 interface. It is used on following:
//   --- Enumerate audio devices.
//   --- Configure global API.
//   --- Create voices.
//   --- Monitor performance.
// A new IXAudio2 instance can be create by using the XAudio2Create helper. 
// ============================================================================
#include <iostream>
#include <comdef.h>
#include <wrl.h>

#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")

using namespace Microsoft::WRL;

// ============================================================================
// Utility - End Application on Failure
// This helper utility simply checks whether the HRESULT contains an error and 
// ends the application by throwing a COM error exception.
// ============================================================================
inline void throwOnFail(HRESULT hr)
{
  if (FAILED(hr)) throw _com_error(hr);
}

// ============================================================================
// XAudio2 - Initialization
// The heart of the engine is the IXAudio2 interface. It is used to enumerate
// audio devices, configure API, create voices and to monitor performance.
// 
// A new IXAudio2 instance can be created by using the XAudio2Create helper. It
// has flags and processor definition, but they should be as default values.
// ============================================================================
ComPtr<IXAudio2> initXAudio2()
{
  // create a new instance of the XAudio2 engine.
  ComPtr<IXAudio2> xaudio2;
  throwOnFail(XAudio2Create(
    &xaudio2,
    0,
    XAUDIO2_DEFAULT_PROCESSOR
  ));
  return xaudio2;
}

// ============================================================================

int main()
{
  auto xaudio2 = initXAudio2();

  return 0;
}