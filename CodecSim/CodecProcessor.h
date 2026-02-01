#pragma once

//==============================================================================
// CodecProcessor.h
// Generic codec processor for CodecSim
// Copyright 2025 MouseSoft
//==============================================================================

#include "CodecSim.h"
#include "CodecRegistry.h"
#include "FFmpegPipeManager.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

//==============================================================================
// GenericCodecProcessor
// Uses FFmpegPipeManager with codec parameters from CodecInfo
//==============================================================================
class GenericCodecProcessor : public ICodecProcessor
{
public:
  explicit GenericCodecProcessor(const CodecInfo& codecInfo);
  ~GenericCodecProcessor() override;

  // ICodecProcessor interface
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

  // Configuration
  void SetBitrate(int bitrateKbps);
  void SetSampleRate(int sampleRate);
  void SetAdditionalArgs(const std::string& args);

  bool HasFirstAudioArrived() const;
  const CodecInfo& GetCodecInfo() const { return mCodecInfo; }

private:
  CodecInfo mCodecInfo;
  std::unique_ptr<FFmpegPipeManager> mPipeManager;

  int mSampleRate;
  int mChannels;
  int mBitrate;     // in bps
  int mFrameSize;
  int mLatencySamples;
  bool mInitialized;

  mutable std::recursive_mutex mMutex;
  std::vector<float> mProcessBuffer;
};
