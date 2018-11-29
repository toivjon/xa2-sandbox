#include <cassert>
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
//
// Note that a single process can create multiple XAudio2 instances, where each
// will operate in own thread. Only debugging settings will be shared.
// ============================================================================
ComPtr<IXAudio2> initXAudio2()
{
  // initialize COM.
  throwOnFail(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

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
// XAudio2 - Create Mastering Voice
// Mastering voice is a wrapper around an audio device. It is the gateway to
// present the audio that passes through an audio graph and it can be created
// with the XAudio2 instance that was previosly created and with parameters.
// 
//   InputChannels.....Number of channels expected by mastering voice.
//   InputSampleRate...Sample rate of the input audio data of mastering voice.
//   Flags.............Flags that specify the behavior. This must be 0.
//   DeviceId..........Identifier that receives the output audio.
//   EffectChain.......A pointer to XAUDIO2_EFFECT_CHAIN. 
//   StreamCategory....The audio stream category to be used.
// ============================================================================
IXAudio2MasteringVoice* createMasteringVoice(ComPtr<IXAudio2> xaudio2)
{
  assert(xaudio2);

  // create a new mastering voice for the target XAudio2 engine.
  IXAudio2MasteringVoice* masteringVoice = nullptr;
  throwOnFail(xaudio2->CreateMasteringVoice(
    &masteringVoice,
    XAUDIO2_DEFAULT_CHANNELS,   // autodetect
    XAUDIO2_DEFAULT_SAMPLERATE, // autodetect
    0,
    nullptr,                    // autodetect
    nullptr,                    // no effects
    AudioCategory_GameEffects
  ));
  return masteringVoice;
}

// ============================================================================

int main()
{
  auto xaudio2 = initXAudio2();
  auto masteringVoice = createMasteringVoice(xaudio2);

  return 0;
}