#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

const int kNumPresets = 1;

//==============================================================================
// Parameter Definitions
//==============================================================================
enum EParams
{
  // Common parameters
  kParamCodec = 0,      // Codec selection (0=MP3, 1=HE-AAC)
  kParamBitrate,        // Bitrate in kbps (16-510)
  kParamSampleRate,     // Sample rate selection (0=44100, 1=48000, 2=96000)
  kParamEnabled,        // Start/Stop toggle (0=stopped, 1=running)

  kNumParams
};

//==============================================================================
// Codec Type Enumeration
//==============================================================================
enum class ECodecType
{
  MP3 = 0,
  HEAAC,
  kNumCodecs
};

//==============================================================================
// Control Tags
//==============================================================================
enum ECtrlTags
{
  kCtrlTagVersionNumber = 0,
  kCtrlTagTitle,

  // Common controls
  kCtrlTagCodecSelector,
  kCtrlTagBitrateSlider,
  kCtrlTagSampleRateSelector,
  kCtrlTagMixSlider,

  // Meter/display controls
  kCtrlTagInputMeter,
  kCtrlTagOutputMeter,
  kCtrlTagLatencyDisplay,
  kCtrlTagBitrateDisplay,

  kCtrlTagStartStopButton,
  kCtrlTagStatusDisplay,
  kCtrlTagLogDisplay,

  kNumCtrlTags
};

//==============================================================================
// Ring Buffer Class
//==============================================================================
template <typename T>
class RingBuffer
{
public:
  RingBuffer(size_t capacity = 65536)
    : mBuffer(capacity)
    , mCapacity(capacity)
    , mWritePos(0)
    , mReadPos(0)
    , mAvailable(0)
  {
  }

  void Resize(size_t newCapacity)
  {
    mBuffer.resize(newCapacity);
    mCapacity = newCapacity;
    Clear();
  }

  void Clear()
  {
    mWritePos = 0;
    mReadPos = 0;
    mAvailable = 0;
  }

  size_t Write(const T* data, size_t count)
  {
    size_t toWrite = std::min(count, mCapacity - mAvailable.load());
    for (size_t i = 0; i < toWrite; ++i)
    {
      mBuffer[mWritePos] = data[i];
      mWritePos = (mWritePos + 1) % mCapacity;
    }
    mAvailable.fetch_add(toWrite);
    return toWrite;
  }

  size_t Read(T* data, size_t count)
  {
    size_t toRead = std::min(count, mAvailable.load());
    for (size_t i = 0; i < toRead; ++i)
    {
      data[i] = mBuffer[mReadPos];
      mReadPos = (mReadPos + 1) % mCapacity;
    }
    mAvailable.fetch_sub(toRead);
    return toRead;
  }

  size_t Available() const { return mAvailable.load(); }
  size_t Capacity() const { return mCapacity; }
  size_t Space() const { return mCapacity - mAvailable.load(); }

private:
  std::vector<T> mBuffer;
  size_t mCapacity;
  size_t mWritePos;
  size_t mReadPos;
  std::atomic<size_t> mAvailable;
};

//==============================================================================
// Encoder State Base Class
//==============================================================================
struct EncoderState
{
  virtual ~EncoderState() = default;
  virtual void Reset() = 0;
  virtual bool IsInitialized() const = 0;
};

//==============================================================================
// Decoder State Base Class
//==============================================================================
struct DecoderState
{
  virtual ~DecoderState() = default;
  virtual void Reset() = 0;
  virtual bool IsInitialized() const = 0;
};

//==============================================================================
// Codec Processor Interface
//==============================================================================
class ICodecProcessor
{
public:
  virtual ~ICodecProcessor() = default;

  virtual bool Initialize(int sampleRate, int channels) = 0;
  virtual void Shutdown() = 0;
  virtual void Reset() = 0;

  virtual int Encode(const float* input, int numSamples, uint8_t* output, int maxOutputBytes) = 0;
  virtual int Decode(const uint8_t* input, int inputBytes, float* output, int maxOutputSamples) = 0;

  virtual int Process(const float* input, int numSamples, float* output, int maxOutputSamples) = 0;

  virtual int GetLatencySamples() const = 0;
  virtual int GetFrameSize() const = 0;
  virtual bool IsInitialized() const = 0;

  virtual void SetLogCallback(std::function<void(const std::string&)> callback) = 0;
};

using namespace iplug;
using namespace igraphics;

//==============================================================================
// Main Plugin Class
//==============================================================================
class CodecSim final : public Plugin
{
public:
  CodecSim(const InstanceInfo& info);
  ~CodecSim();

#if IPLUG_EDITOR
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }
#endif

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
  void OnIdle() override;
#endif

private:
  // Pre-allocated interleaved buffers for ProcessBlock (avoid allocation in audio thread)
  std::vector<float> mInterleavedInput;
  std::vector<float> mInterleavedOutput;

  // Codec processors
  std::unique_ptr<ICodecProcessor> mCodecProcessor;

  // State tracking
  ECodecType mCurrentCodec;
  int mSampleRate;
  int mNumChannels;

  // Latency tracking
  std::atomic<int> mLatencySamples;
  std::atomic<int> mActualBitrate;

  // Parameter cache for thread-safe access
  std::atomic<int> mCodecParam;
  std::atomic<int> mBitrateParam;

  // Log display
  std::vector<std::string> mLogMessages;
  std::mutex mLogMutex;
  static constexpr int kMaxLogLines = 12;

  // Helper methods
  void InitializeCodec(ECodecType codecType);
  void UpdateCodecParameters();
  void StopCodec();
  void AddLogMessage(const std::string& msg);

  // Thread safety
  std::recursive_mutex mCodecMutex;

  bool mIsInitializing = false;  // Guard against re-entrant InitializeCodec calls
  std::atomic<bool> mEnabled{false};   // Audio processing enabled (Start/Stop)
  bool mConstructed = false;      // Prevent OnParamChange from triggering init during construction
};
