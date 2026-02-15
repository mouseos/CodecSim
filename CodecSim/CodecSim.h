#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
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
  kCtrlTagBitrateLabel,
  kCtrlTagBitrateSelector,
  kCtrlTagBitrateCustom,
  kCtrlTagSampleRateSelector,

  kCtrlTagApplyButton,
  kCtrlTagLogDisplay,
  kCtrlTagSpinner,

  // Right panel tab system
  kCtrlTagDetailTabSwitch,

  // Options tab: 5 label+control slots
  kCtrlTagOptionLabel0,
  kCtrlTagOptionControl0,
  kCtrlTagOptionLabel1,
  kCtrlTagOptionControl1,
  kCtrlTagOptionLabel2,
  kCtrlTagOptionControl2,
  kCtrlTagOptionLabel3,
  kCtrlTagOptionControl3,
  kCtrlTagOptionLabel4,
  kCtrlTagOptionControl4,

  kCtrlTagNoOptionsText,

  kCtrlTagPresetSelector,
  kCtrlTagPresetSaveButton,
  kCtrlTagPresetNameEntry,

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

  virtual bool HasFirstAudioArrived() const = 0;
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

  // State serialization (used by VST3/CLAP/AU hosts for project save/load)
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
  void OnRestoreState() override;

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

  // Decoded sample accumulation buffer (absorbs bursty decode pipeline)
  std::deque<float> mDecodedBuffer;

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
  void ApplyCodecSettings();
  void StopCodec();
  void AddLogMessage(const std::string& msg);
  void UpdateBitrateForCodec(int codecIndex);
  int GetEffectiveBitrate();
  void UpdateOptionsForCodec(int codecIndex);
  void SetDetailTab(int tabIndex);
  std::string BuildCurrentAdditionalArgs();
  void SaveStandaloneState();
  void LoadStandaloneState();
  static std::string GetAppDataPath();

  // User preset management (file-based)
  void SaveUserPreset(const std::string& name);
  void LoadUserPreset(const std::string& name);
  void DeleteUserPreset(const std::string& name);
  std::vector<std::string> GetUserPresetList();
  static std::string GetPresetsDir();

  // Dynamic bitrate presets for current codec
  std::vector<int> mCurrentBitratePresets;
  bool mCurrentCodecIsLossless = false;
  bool mCurrentCodecHasOther = false; // true if "Other" (custom) option is available

  // Codec option values and tab state
  std::map<std::string, int> mCodecOptionValues;
  int mDetailTabIndex = 0; // 0=Options, 1=Log (default to Options)

  // Thread safety
  std::recursive_mutex mCodecMutex;

  bool mIsInitializing = false;
  std::atomic<bool> mInitializing{false};
  std::thread mInitThread;
  bool mConstructed = false;

  // Pending changes indicator
  std::atomic<bool> mPendingApply{false};
  std::atomic<bool> mCancelInit{false};

  // UI state tracking (to avoid redundant updates in OnIdle)
  int mLastApplyButtonState = -1; // -1=unknown, 0=applied(green), 1=pending(orange)

  // Deferred codec update: OnParamChange(kParamCodec) runs on host thread,
  // but UpdateBitrateForCodec/UpdateOptionsForCodec must run on the UI thread.
  std::atomic<bool> mPendingCodecUpdate{false};

  // Cache the last bitrate display string to avoid redundant SetValueStr calls in OnIdle
  std::string mLastBitrateDisplayStr;
};
