#include <cassert>
#include <iostream>
#include <comdef.h>
#include <vector>
#include <wrl.h>

// XAudio2
#include <xaudio2.h>

// Windows Media Foundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace Microsoft::WRL;

// ============================================================================

struct AudioFile
{
  std::vector<BYTE> data;
  WAVEFORMATEX*     format;
  unsigned int      formatlength;
};

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
// has flags and processor definition to provide further customisation.
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
// WMF - Initialize
// The use of Windows Media Foundation (WMF) is not necessary, but it provides
// an easy way to get access to load both compressed and uncompressed files. In
// this function, we initialize WMF and build a media reader configuration that
// will be further used by our media loading function to get access to data.
// ============================================================================
ComPtr<IMFAttributes> initWMF()
{
  // initialize the core Window Media Foundation.
  throwOnFail(MFStartup(MF_VERSION));

  // specify that want to user reader with low latency.
  ComPtr<IMFAttributes> readerConfiguration;
  throwOnFail(MFCreateAttributes(&readerConfiguration, 1));
  throwOnFail(readerConfiguration->SetUINT32(MF_LOW_LATENCY, true));

  // return the created WMF object.
  return readerConfiguration;
}

// ============================================================================
// WMF - Load a file into a XAudio2 supported format.
// Windows Media Foundation contains useful functions to load audio data from a
// file. We may also use a decoder functionality to load and decode audio that
// is compressed e.g. as mp3 or such.
// ============================================================================
AudioFile loadFile(const std::wstring& file, ComPtr<IMFAttributes> config)
{
  // construct a source reader.
  ComPtr<IMFSourceReader> reader;
  throwOnFail(MFCreateSourceReaderFromURL(file.c_str(), config.Get(), &reader));

  // select only the very first audio stream.
  auto streamIndex = MF_SOURCE_READER_FIRST_AUDIO_STREAM;
  throwOnFail(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, false));
  throwOnFail(reader->SetStreamSelection(streamIndex, true));

  // get information about the media file contents.
  ComPtr<IMFMediaType> mediaType;
  throwOnFail(reader->GetNativeMediaType(streamIndex, 0, &mediaType));

  // ensure that the provided file is an audio file.
  GUID majorType = {};
  throwOnFail(mediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType));
  assert(majorType == MFMediaType_Audio);

  // configure WMF to decompress the audio file if it is compressed.
  GUID subType = {};
  throwOnFail(mediaType->GetGUID(MF_MT_SUBTYPE, &subType));
  if (subType != MFAudioFormat_Float && subType != MFAudioFormat_PCM) {
    ComPtr<IMFMediaType> target;
    throwOnFail(MFCreateMediaType(&target));
    throwOnFail(target->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    throwOnFail(target->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
    throwOnFail(reader->SetCurrentMediaType(streamIndex, nullptr, target.Get()));
  }

  // process the data and load it into a XAudio2 buffer.
  AudioFile audioFile = {};
  ComPtr<IMFMediaType> audioType;
  throwOnFail(reader->GetCurrentMediaType(streamIndex, &audioType));
  throwOnFail(MFCreateWaveFormatExFromMFMediaType(
    audioType.Get(),
    &audioFile.format,
    &audioFile.formatlength
  ));

  // ensure that the target stream is being selected.
  throwOnFail(reader->SetStreamSelection(streamIndex, true));

  // read samples from the source file into a byte vector. 
  ComPtr<IMFSample> sample;
  ComPtr<IMFMediaBuffer> buffer;
  BYTE* audioData = nullptr;
  DWORD audioDataSize = 0;
  while (true) {
    DWORD flags = 0;
    throwOnFail(reader->ReadSample(streamIndex, 0, nullptr, &flags, nullptr, &sample));

    // check whether data type is changed or EOF has been reached.
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
      break;
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
      break;

    // get data from the audio sample via a buffer.
    throwOnFail(sample->ConvertToContiguousBuffer(&buffer));
    throwOnFail(buffer->Lock(&audioData, nullptr, &audioDataSize));
    for (auto i = 0u; i < audioDataSize; i++) {
      audioFile.data.push_back(audioData[i]);
    }
    throwOnFail(buffer->Unlock());
  }

  // return the decoded and XAudio2 ready audio package.
  return audioFile;
}

// ============================================================================

int main()
{
  // initialize Windows Media Foundation.
  auto wmfReader = initWMF();
  auto audioFile = loadFile(L"test.mp3", wmfReader);

  // initialize XAudio2.
  auto xaudio2 = initXAudio2();
  auto masteringVoice = createMasteringVoice(xaudio2);

  // stop and and remove the mastering voice from the XAudio2 graph.
  masteringVoice->DestroyVoice();

  // shutdown Windows Media Foundation (WMF).
  MFShutdown();
  return 0;
}