#pragma once

//==============================================================================
// FFmpegPipeManager.h
// FFmpeg pipe communication manager for real-time encoding/decoding
// Copyright 2025 MouseSoft
//==============================================================================

#ifndef _WIN32
  #error "FFmpegPipeManager.h is Windows-only."
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>

//==============================================================================
// FFmpegPipeManager Class
// Manages ffmpeg.exe process with pipe communication for real-time audio processing
//==============================================================================

class FFmpegPipeManager
{
public:
  //--------------------------------------------------------------------------
  // Constructor/Destructor
  //--------------------------------------------------------------------------
  FFmpegPipeManager();
  ~FFmpegPipeManager();

  // Non-copyable
  FFmpegPipeManager(const FFmpegPipeManager&) = delete;
  FFmpegPipeManager& operator=(const FFmpegPipeManager&) = delete;

  //--------------------------------------------------------------------------
  // Configuration
  //--------------------------------------------------------------------------
  struct Config
  {
    std::string ffmpegPath;           // Path to ffmpeg.exe
    std::string codecName;            // Codec name (e.g., "libmp3lame", "libopus", "aac")
    int sampleRate;                   // Sample rate (e.g., 48000)
    int channels;                     // Number of channels (e.g., 2)
    int bitrate;                      // Bitrate in bps (e.g., 128000)
    std::string additionalArgs;       // Additional ffmpeg arguments
    std::string muxerFormat;          // Container format for encoder output (e.g., "mp3", "adts", "ogg")
    std::string demuxerFormat;        // Container format for decoder input (e.g., "mp3", "aac", "ogg")
    size_t bufferSize;                // Internal buffer size in bytes

    Config()
      : ffmpegPath("ffmpeg.exe")
      , codecName("libmp3lame")
      , sampleRate(48000)
      , channels(2)
      , bitrate(128000)
      , bufferSize(65536)
    {}
  };

  //--------------------------------------------------------------------------
  // Lifecycle
  //--------------------------------------------------------------------------

  /**
   * Start ffmpeg process with pipe communication
   * @param config Configuration for ffmpeg process
   * @return true if successful, false otherwise
   */
  bool Start(const Config& config);

  /**
   * Stop ffmpeg process and close pipes
   */
  void Stop();

  /**
   * Check if ffmpeg process is running
   * @return true if running, false otherwise
   */
  bool IsRunning() const { return mIsRunning; }

  //--------------------------------------------------------------------------
  // Data Transfer
  //--------------------------------------------------------------------------

  /**
   * Write audio samples to input queue (non-blocking)
   * Data is queued and written to ffmpeg pipe by InputWriteThread
   * @param data Pointer to audio data (float samples, interleaved)
   * @param numSamples Number of samples per channel
   * @return true if successful, false otherwise
   */
  bool WriteSamples(const float* data, size_t numSamples);

  /**
   * Read processed audio samples from ffmpeg output pipe (blocking)
   * @param data Pointer to buffer for audio data (float samples, interleaved)
   * @param numSamples Number of samples per channel to read
   * @param timeout Timeout in milliseconds (0 = no timeout)
   * @return Number of samples actually read, or 0 on error/timeout
   */
  size_t ReadSamples(float* data, size_t numSamples, DWORD timeout = 0);

  /**
   * Check if output data is available
   * @return Number of samples available to read
   */
  size_t AvailableOutputSamples() const;

  /**
   * Flush input buffer and wait for output
   */
  void Flush();

  //--------------------------------------------------------------------------
  // Error Handling
  //--------------------------------------------------------------------------

  /**
   * Get last error message
   * @return Error message string
   */
  std::string GetLastErrorMessage() const { return mLastError; }

  /**
   * Set logging callback
   * @param callback Function to call for log messages
   */
  void SetLogCallback(std::function<void(const std::string&)> callback);

  //--------------------------------------------------------------------------
  // Status
  //--------------------------------------------------------------------------

  /**
   * Get current latency estimate in samples
   * @return Estimated latency
   */
  size_t GetLatencySamples() const { return mLatencySamples; }

private:
  //--------------------------------------------------------------------------
  // Internal types
  //--------------------------------------------------------------------------

  struct PipeHandles
  {
    HANDLE hInputRead;
    HANDLE hInputWrite;
    HANDLE hOutputRead;
    HANDLE hOutputWrite;
    HANDLE hErrorRead;
    HANDLE hErrorWrite;

    PipeHandles()
      : hInputRead(INVALID_HANDLE_VALUE)
      , hInputWrite(INVALID_HANDLE_VALUE)
      , hOutputRead(INVALID_HANDLE_VALUE)
      , hOutputWrite(INVALID_HANDLE_VALUE)
      , hErrorRead(INVALID_HANDLE_VALUE)
      , hErrorWrite(INVALID_HANDLE_VALUE)
    {}
  };

  //--------------------------------------------------------------------------
  // Internal methods
  //--------------------------------------------------------------------------

  /**
   * Create pipes for stdin/stdout/stderr
   */
  bool CreatePipes();

  /**
   * Close all pipes
   */
  void ClosePipes();

  /**
   * Launch encoder and decoder ffmpeg processes
   */
  bool LaunchProcesses(const Config& config);

  /**
   * Terminate encoder and decoder ffmpeg processes
   */
  void TerminateProcesses();

  /**
   * Build ffmpeg encoder command line
   */
  std::string BuildEncoderCommand(const Config& config) const;

  /**
   * Build ffmpeg decoder command line
   */
  std::string BuildDecoderCommand(const Config& config) const;

  /**
   * Get intermediate format for codec
   */
  std::string GetIntermediateFormat(const std::string& codecName) const;

  /**
   * Background thread for reading stderr
   */
  void ErrorReadThread();

  /**
   * Background thread for reading stdout
   */
  void OutputReadThread();

  /**
   * Background thread for writing stdin (non-blocking input)
   */
  void InputWriteThread();

  /**
   * Convert float samples to S16LE format
   */
  void FloatToS16LE(const float* input, int16_t* output, size_t numSamples);

  /**
   * Convert S16LE format to float samples
   */
  void S16LEToFloat(const int16_t* input, float* output, size_t numSamples);

  /**
   * Log message
   */
  void Log(const std::string& message);

  /**
   * Log error and store in mLastError
   */
  void LogError(const std::string& message);

  //--------------------------------------------------------------------------
  // Member variables
  //--------------------------------------------------------------------------

  // Configuration
  Config mConfig;

  // Process handles (two ffmpeg processes: encoder + decoder)
  PROCESS_INFORMATION mEncoderProcessInfo;
  PROCESS_INFORMATION mDecoderProcessInfo;
  PipeHandles mPipes;
  HANDLE mIntermediatePipeRead;   // Encoder stdout -> Decoder stdin
  HANDLE mIntermediatePipeWrite;
  HANDLE mJobObject;

  // State
  std::atomic<bool> mIsRunning;
  std::string mLastError;
  size_t mLatencySamples;

  // Threading
  std::thread mErrorThread;
  std::thread mOutputThread;
  std::thread mInputThread;
  std::mutex mMutex;
  mutable std::mutex mOutputMutex;
  std::mutex mInputMutex;

  // Buffers
  std::vector<float> mInputFloatBuffer;   // Non-blocking input queue
  std::vector<uint8_t> mOutputRawBuffer;
  std::queue<float> mOutputFloatBuffer;

  // Logging
  std::function<void(const std::string&)> mLogCallback;
};
