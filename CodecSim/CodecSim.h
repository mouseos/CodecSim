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
  kParamCodec = 0,      // Codec selection (dynamic, based on available codecs)
  kParamBitrate,        // Bitrate preset selection (enum: 32,48,64,96,128,160,192,256,320,Other)
  kParamBitrateCustom,  // Custom bitrate in kbps (used when "Other" is selected)
  kParamSampleRate,     // Sample rate selection (enum: 8000,16000,22050,32000,44100,48000,88200,96000)
  kParamEnabled,        // Start/Stop toggle (0=stopped, 1=running)

  kNumParams
};

//==============================================================================
// Control Tags
//==============================================================================
enum ECtrlTags
{
  kCtrlTagVersionNumber = 0,
  kCtrlTagTitle,

  kCtrlTagCodecSelector,
  kCtrlTagBitrateSelector,
  kCtrlTagBitrateCustom,
  kCtrlTagSampleRateSelector,

  kCtrlTagStartStopButton,
  kCtrlTagStatusDisplay,
  kCtrlTagLogDisplay,

  kNumCtrlTags
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
  // Pre-allocated interleaved buffers for ProcessBlock
  std::vector<float> mInterleavedInput;
  std::vector<float> mInterleavedOutput;

  // Codec processor
  std::unique_ptr<ICodecProcessor> mCodecProcessor;

  // State
  int mCurrentCodecIndex;     // Index into available codec list
  int mSampleRate;
  int mNumChannels;

  // Latency tracking
  std::atomic<int> mLatencySamples;

  // Log display
  std::vector<std::string> mLogMessages;
  std::mutex mLogMutex;
  static constexpr int kMaxLogLines = 12;

  // Helper methods
  void InitializeCodec(int codecIndex);
  void StopCodec();
  void AddLogMessage(const std::string& msg);

  // Thread safety
  std::recursive_mutex mCodecMutex;

  bool mIsInitializing = false;
  std::atomic<bool> mEnabled{false};
  bool mConstructed = false;
};
