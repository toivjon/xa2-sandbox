// ============================================================================
// XAudio2 Sandbox
// 
// XAudio2 is an audio API for all kinds of games. It is a successor to older
// DirectSound and XAudio and has a support for signal processing and mixing.
//
// The core object in XAudio2 is the IXAudio2 interface, which is used to build
// all other XAudio2 related objects. It is created with XAudio2Create method.
//
// ### Voice Types ###
//
// XAudio2 contains contains the following kinds of voices.
//   1. source......Handle audio data provided by the client.
//   2. submix......Handle mixing of the voices.
//   3. mastering...Handle passing voice data to audio device.
//
// All XAudio2 voices contain the following features.
//   1. Overall volume
//   2. Channel specific volume
//   3. DSP effects
//   4. Mix matrices
//
// ### Audio Callbacks ###
//
// Audio callbacks can be used to call functions when certain events take place
// in the audio processing thread. Callbacks are divided into two interfaces.
//   1. IXAudio2EngineCallback which handles global audio events.
//      OnProcessingPassEnd()............After audio processing pass end.
//      OnProcessingPassStart()..........Before audio processing pass begin.
//      OnCriticalError(HRESULT error)...On a critical error.
//   2. IXAudio2VoiceCallback which handles voice specific audio events.
//      OnStreamEnd()................................Stream finished.
//      OnVoiceProcessingPassEnd()...................After voice pass.
//      OnVoiceProcessingPassStart(UINT32 samples)...Before void pass.
//      OnBufferEnd(void* bufferCtx).................After a buffer.
//      OnBufferStart(void* bufferCtx)...............Before a new buffer.
//      OnLoopEnd(void* bufferCtx)...................When reaching end-of-loop.
//      OnVoiceError(void* bufferCtx, HRESULT err)...On a critical error.
//
// Callback functions must be implemented carefully and without delays like...
//   1. Don't access hard disk or other permanent storage.
//   2. Don't make expensive or blocking API calls.
//   3. Don't synchronize with other parts of the code.
//   4. Don't require significant CPU usage.
// Whether these are required, just provide task to other thread to do the job.
//
// ### Audio Graphs ###
// 
// Audio graphs are chains that work in the following kind of sequence:
//   1. Receive audio streams as input.
//   2. Process the provided streams.
//   3. Output the output to an audio device.
//
// All audio graph processing takes place in a separate thread with where the
// periodicity is defined by the graph's quantum (e.g. 10ms on Windows).
//
// Audio graphs can be dynamically controlled, by enabling/disabling parts of
// the graph and changing effects and interconnections even while it's running.
// Here's an abstract list of graph operations that will change it's state.
//   1. Create/destroy voices.
//   2. Start/stop voices.
//   3. Change the voice destination.
//   4. Modify effect chain.
//   5. Enable/disable effects.
//   6. Specify effect parameters or SRCs, filters, volumes and mixers.
// 
// Any graph state changing operations can be combined as an atomic operation
// by using the operation sets (which are discussed in more detail later on).
//
// Note that XAudio2 stores and processes audio always as a 32-bit float PCM.
// If any supported encoded data is given to XAudio2 it will be first decoded.
//
// XAudio2 handles all sample rate and channel conversion with following limits.
//   1. Destination voices must be running at same sample rate.
//   2. Effects can change channel count but NOT sample rate.
//   3. Effect channel count must match with the voices.
//   4. No dynamic graph change can made which breaks the above rules.
//
// ### Audio Effects ###
//
// XAudio contains an inbuilt support for the following audio effects (XAPO).
//   reverb.........Created with XAudio2CreateReverb
//   volume-meter...Created with XAudio2CreateVolumeMeter
//
// Each new effect must also have a populated XAUDIO2_EFFECT_DESCRIPTOR. This
// struct contains the following kind of attributes.
//   IUnknown* effect........Pointer to effect object (XAPO).
//   BOOL initialState.......True whether to initially enable the effect.
//   UINT32 outputChannels...Number of output channels.
//
// Effects are passed to voices as effect chains. To build an effect chain, one
// should use the XAUDIO2_EFFECT_CHAIN structure to specify it's contents.
//   UINT32 effectCount............................The number of effects.
//   XAUDIO_EFFECT_DESCRIPTOR* effectDescriptors...The array of effects.
// This structure is then passed to voice->SetEffectChain(&chain) to apply it.
// 
// NOTE: XAPO objects can be released after being assigned to give ownership to
//       XAudio2. This ensures that XAudio releases them when no longer needed.
//
// Here's a list of other useful effect management functions.
//   voice->SetEffectParameters(...)...To specify effect behavior.
//   voice->DisableEffect(...).........To disable effect from the voice.
//   voice->EnableEffect(...)..........To enable effect on the voice.
//
// Custom audio processing objects (XAPOs) can be created with CXAPOBase and
// IXAPO interface. XAPOFX can be used for some common mechanisms to create
// new effect instances.
// ============================================================================
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
// XAudio2 - Create a new source voice.
// Source voices act as a containers of audio data that can be provided by the
// application using the XAudio2 API.
// ============================================================================
IXAudio2SourceVoice* createVoice(ComPtr<IXAudio2> xa2, AudioFile& file)
{
  assert(xa2);
  assert(file.format);

  // create a new source voice with a desired sound format.
  IXAudio2SourceVoice* sourceVoice = nullptr;
  throwOnFail(xa2->CreateSourceVoice(&sourceVoice, file.format));

  // return the created source voice.
  return sourceVoice;
}

// ============================================================================
// XAudio2 - Play a source voice.
// First fills the source voice buffer with the audio data from the read audio
// file and then start playing the actual sound by sending it to audio queue.
// ============================================================================
void playVoice(IXAudio2SourceVoice* voice, AudioFile& file)
{
  assert(voice);
  assert(!file.data.empty());

  // fill a buffer descriptor with the file details.
  XAUDIO2_BUFFER buffer = {};
  buffer.AudioBytes = file.data.size();
  buffer.pAudioData = &file.data[0];

  // submit audio buffer into the source voice.
  throwOnFail(voice->SubmitSourceBuffer(&buffer));

  // it's time start playing the voice.
  voice->Start();
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
  auto sourceVoice = createVoice(xaudio2, audioFile);

  // play the loaded sounds.
  playVoice(sourceVoice, audioFile);

  Sleep(7000);

  // stop and and remove the mastering voice from the XAudio2 graph.
  masteringVoice->DestroyVoice();

  // shutdown Windows Media Foundation (WMF).
  MFShutdown();
  return 0;
}