#pragma once

//==============================================================================
// CodecProcessor.h
// Codec processor implementations for CodecSim (MP3, HE-AAC)
// Copyright 2025 MouseSoft
//==============================================================================

#include "CodecSim.h"
#include "FFmpegPipeManager.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

//==============================================================================
// Base Codec Processor with FFmpegPipeManager integration
//==============================================================================
class BaseCodecProcessor : public ICodecProcessor
{
public:
  BaseCodecProcessor();
  virtual ~BaseCodecProcessor();

  // ICodecProcessor interface implementation
  bool Initialize(int sampleRate, int channels) override;
  void Shutdown() override;
  void Reset() override;

  int Encode(const float* input, int numSamples, uint8_t* output, int maxOutputBytes) override;
  int Decode(const uint8_t* input, int inputBytes, float* output, int maxOutputSamples) override;
  int Process(const float* input, int numSamples, float* output, int maxOutputSamples) override;

  int GetLatencySamples() const override;
  int GetFrameSize() const override;
  bool IsInitialized() const override;
  void SetLogCallback(std::function<void(const std::string&)> callback) override;

protected:
  // Virtual methods for codec-specific configuration
  virtual std::string GetCodecName() const = 0;
  virtual std::string GetAdditionalArgs() const = 0;
  virtual int GetDefaultBitrate() const = 0;
  virtual int GetDefaultFrameSize() const = 0;

  // Helper methods
  void SetBitrate(int bitrateKbps);
  void SetSampleRate(int sampleRate);

  // Member variables
  std::unique_ptr<FFmpegPipeManager> mPipeManager;
  int mSampleRate;
  int mChannels;
  int mBitrate;
  int mFrameSize;
  int mLatencySamples;
  bool mInitialized;

  // Thread safety
  mutable std::recursive_mutex mMutex;

  // Intermediate buffers
  std::vector<float> mProcessBuffer;
};

//==============================================================================
// MP3 Codec Processor
// Uses libmp3lame encoder via FFmpegPipeManager
// CBR (Constant Bit Rate) mode only
//==============================================================================
class MP3CodecProcessor : public BaseCodecProcessor
{
public:
  MP3CodecProcessor();
  ~MP3CodecProcessor() override = default;

  // MP3-specific configuration
  void SetBitrate(int bitrateKbps);  // 8-320 kbps

protected:
  std::string GetCodecName() const override { return "libmp3lame"; }
  std::string GetAdditionalArgs() const override;
  int GetDefaultBitrate() const override { return 128; }
  int GetDefaultFrameSize() const override { return 1152; }

private:
  int GetCodecSampleRate() const;
};

//==============================================================================
// HE-AAC Codec Processor
// Uses libfdk_aac encoder via FFmpegPipeManager
// HE-AAC v1 profile (SBR only)
//==============================================================================
class HEAACCodecProcessor : public BaseCodecProcessor
{
public:
  HEAACCodecProcessor();
  ~HEAACCodecProcessor() override = default;

  // HE-AAC-specific configuration
  void SetBitrate(int bitrateKbps);  // 24-128 kbps (recommended for HE-AAC)
  void SetProfile(int profile);      // 5 = HE-AAC v1, 29 = HE-AAC v2

protected:
  std::string GetCodecName() const override { return mUseFdk ? "libfdk_aac" : "aac"; }
  std::string GetAdditionalArgs() const override;
  int GetDefaultBitrate() const override { return 64; }
  int GetDefaultFrameSize() const override { return 1024; }

private:
  int mProfile;  // AAC profile (5 = HE-AAC v1, 29 = HE-AAC v2)
  bool mUseFdk = false;  // Whether libfdk_aac is available
};

//==============================================================================
// Codec Processor Factory
//==============================================================================
class CodecProcessorFactory
{
public:
  /**
   * Create a codec processor instance
   * @param codecType Type of codec to create
   * @return Unique pointer to the codec processor, or nullptr on failure
   */
  static std::unique_ptr<ICodecProcessor> Create(ECodecType codecType);

  /**
   * Create and initialize a codec processor
   * @param codecType Type of codec to create
   * @param sampleRate Sample rate for the codec
   * @param channels Number of channels
   * @return Unique pointer to the initialized codec processor, or nullptr on failure
   */
  static std::unique_ptr<ICodecProcessor> Create(ECodecType codecType, int sampleRate, int channels);

  /**
   * Get human-readable name for codec type
   */
  static const char* GetCodecName(ECodecType codecType);

  /**
   * Check if codec is available (FFmpeg with required codec support)
   */
  static bool IsCodecAvailable(ECodecType codecType);
};

//==============================================================================
// Helper functions for parameter application
//==============================================================================

/**
 * Apply MP3-specific parameters to a codec processor
 * @param processor Pointer to codec processor (must be MP3CodecProcessor)
 * @param bitrateKbps Bitrate in kbps (8-320)
 */
inline void ApplyMP3Parameters(ICodecProcessor* processor, int bitrateKbps)
{
  if (auto* mp3 = dynamic_cast<MP3CodecProcessor*>(processor))
  {
    mp3->SetBitrate(bitrateKbps);
  }
}

/**
 * Apply HE-AAC-specific parameters to a codec processor
 * @param processor Pointer to codec processor (must be HEAACCodecProcessor)
 * @param bitrateKbps Bitrate in kbps (24-128)
 */
inline void ApplyHEAACParameters(ICodecProcessor* processor, int bitrateKbps)
{
  if (auto* heaac = dynamic_cast<HEAACCodecProcessor*>(processor))
  {
    heaac->SetBitrate(bitrateKbps);
  }
}

/**
 * Apply HE-AAC profile setting
 * @param processor Pointer to codec processor (must be HEAACCodecProcessor)
 * @param profile AAC profile (5 = HE-AAC v1, 29 = HE-AAC v2)
 */
inline void ApplyHEAACProfile(ICodecProcessor* processor, int profile)
{
  if (auto* heaac = dynamic_cast<HEAACCodecProcessor*>(processor))
  {
    heaac->SetProfile(profile);
  }
}
