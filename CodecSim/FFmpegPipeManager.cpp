//==============================================================================
// FFmpegPipeManager.cpp
// FFmpeg pipe communication manager implementation
// Copyright 2025 MouseSoft
//==============================================================================

#include "FFmpegPipeManager.h"
#include <sstream>
#include <algorithm>
#include <cstring>

//==============================================================================
// Constructor/Destructor
//==============================================================================

FFmpegPipeManager::FFmpegPipeManager()
  : mIsRunning(false)
  , mLatencySamples(0)
  , mIntermediatePipeRead(INVALID_HANDLE_VALUE)
  , mIntermediatePipeWrite(INVALID_HANDLE_VALUE)
  , mJobObject(nullptr)
{
  std::memset(&mEncoderProcessInfo, 0, sizeof(mEncoderProcessInfo));
  std::memset(&mDecoderProcessInfo, 0, sizeof(mDecoderProcessInfo));
}

FFmpegPipeManager::~FFmpegPipeManager()
{
  Stop();
}

//==============================================================================
// Lifecycle
//==============================================================================

bool FFmpegPipeManager::Start(const Config& config)
{
  std::lock_guard<std::mutex> lock(mMutex);

  if (mIsRunning)
  {
    LogError("FFmpeg process already running");
    return false;
  }

  // Store configuration
  mConfig = config;

  // Create pipes
  if (!CreatePipes())
  {
    LogError("Failed to create pipes");
    return false;
  }

  // Launch ffmpeg process
  if (!LaunchProcesses(config))
  {
    ClosePipes();
    return false;
  }

  // Start background threads
  mIsRunning = true;
  mErrorThread = std::thread(&FFmpegPipeManager::ErrorReadThread, this);
  mOutputThread = std::thread(&FFmpegPipeManager::OutputReadThread, this);
  mInputThread = std::thread(&FFmpegPipeManager::InputWriteThread, this);

  Log("FFmpeg process started successfully");
  return true;
}

void FFmpegPipeManager::Stop()
{
  if (!mIsRunning)
    return;

  Log("Stopping FFmpeg processes...");
  mIsRunning = false;

  // Close write end of input pipe to signal EOF to encoder
  if (mPipes.hInputWrite != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hInputWrite);
    mPipes.hInputWrite = INVALID_HANDLE_VALUE;
  }

  // Wait briefly for graceful process exit
  bool encoderAlive = false;
  bool decoderAlive = false;

  if (mEncoderProcessInfo.hProcess != nullptr)
  {
    DWORD waitResult = WaitForSingleObject(mEncoderProcessInfo.hProcess, 2000);
    encoderAlive = (waitResult == WAIT_TIMEOUT);
  }
  if (mDecoderProcessInfo.hProcess != nullptr)
  {
    DWORD waitResult = WaitForSingleObject(mDecoderProcessInfo.hProcess, 2000);
    decoderAlive = (waitResult == WAIT_TIMEOUT);
  }

  if (encoderAlive || decoderAlive)
  {
    Log("FFmpeg processes did not exit gracefully, forcing termination");
    if (mJobObject)
    {
      CloseHandle(mJobObject);
      mJobObject = nullptr;
    }
    if (encoderAlive && mEncoderProcessInfo.hProcess)
      ::TerminateProcess(mEncoderProcessInfo.hProcess, 1);
    if (decoderAlive && mDecoderProcessInfo.hProcess)
      ::TerminateProcess(mDecoderProcessInfo.hProcess, 1);
  }

  // Join threads
  if (mInputThread.joinable()) mInputThread.join();
  if (mOutputThread.joinable()) mOutputThread.join();
  if (mErrorThread.joinable()) mErrorThread.join();

  // Clean up encoder process handles
  if (mEncoderProcessInfo.hProcess != nullptr)
  {
    CloseHandle(mEncoderProcessInfo.hProcess);
    CloseHandle(mEncoderProcessInfo.hThread);
    std::memset(&mEncoderProcessInfo, 0, sizeof(mEncoderProcessInfo));
  }
  // Clean up decoder process handles
  if (mDecoderProcessInfo.hProcess != nullptr)
  {
    CloseHandle(mDecoderProcessInfo.hProcess);
    CloseHandle(mDecoderProcessInfo.hThread);
    std::memset(&mDecoderProcessInfo, 0, sizeof(mDecoderProcessInfo));
  }

  // Close job object
  if (mJobObject)
  {
    CloseHandle(mJobObject);
    mJobObject = nullptr;
  }

  // Close intermediate pipe handles
  if (mIntermediatePipeRead != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mIntermediatePipeRead);
    mIntermediatePipeRead = INVALID_HANDLE_VALUE;
  }
  if (mIntermediatePipeWrite != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mIntermediatePipeWrite);
    mIntermediatePipeWrite = INVALID_HANDLE_VALUE;
  }

  // Close remaining pipes
  ClosePipes();

  // Clear buffers
  {
    std::lock_guard<std::mutex> lock(mOutputMutex);
    while (!mOutputFloatBuffer.empty())
      mOutputFloatBuffer.pop();
    mOutputRawBuffer.clear();
  }
  {
    std::lock_guard<std::mutex> lock(mInputMutex);
    mInputFloatBuffer.clear();
  }

  Log("FFmpeg processes stopped");
}

//==============================================================================
// Data Transfer
//==============================================================================

bool FFmpegPipeManager::WriteSamples(const float* data, size_t numSamples)
{
  if (!mIsRunning)
    return false;

  // Non-blocking: just append to input queue
  size_t totalSamples = numSamples * mConfig.channels;

  std::lock_guard<std::mutex> lock(mInputMutex);
  mInputFloatBuffer.insert(mInputFloatBuffer.end(), data, data + totalSamples);
  return true;
}

size_t FFmpegPipeManager::ReadSamples(float* data, size_t numSamples, DWORD timeout)
{
  if (!mIsRunning)
  {
    return 0;
  }

  size_t totalSamples = numSamples * mConfig.channels;
  size_t samplesRead = 0;

  std::lock_guard<std::mutex> lock(mOutputMutex);

  // Read available samples from buffer
  while (samplesRead < totalSamples && !mOutputFloatBuffer.empty())
  {
    data[samplesRead++] = mOutputFloatBuffer.front();
    mOutputFloatBuffer.pop();
  }

  return samplesRead / mConfig.channels;
}

size_t FFmpegPipeManager::AvailableOutputSamples() const
{
  std::lock_guard<std::mutex> lock(mOutputMutex);
  return mOutputFloatBuffer.size() / mConfig.channels;
}

void FFmpegPipeManager::Flush()
{
  // Close input to signal EOF
  if (mPipes.hInputWrite != INVALID_HANDLE_VALUE)
  {
    FlushFileBuffers(mPipes.hInputWrite);
  }
}

//==============================================================================
// Error Handling
//==============================================================================

void FFmpegPipeManager::SetLogCallback(std::function<void(const std::string&)> callback)
{
  mLogCallback = callback;
}

//==============================================================================
// Internal Methods - Pipe Management
//==============================================================================

bool FFmpegPipeManager::CreatePipes()
{
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  // Create stdin pipe
  if (!CreatePipe(&mPipes.hInputRead, &mPipes.hInputWrite, &sa, 0))
  {
    LogError("Failed to create input pipe");
    return false;
  }

  // Ensure write handle is not inherited
  if (!SetHandleInformation(mPipes.hInputWrite, HANDLE_FLAG_INHERIT, 0))
  {
    LogError("Failed to set input pipe handle information");
    ClosePipes();
    return false;
  }

  // Create stdout pipe
  if (!CreatePipe(&mPipes.hOutputRead, &mPipes.hOutputWrite, &sa, 0))
  {
    LogError("Failed to create output pipe");
    ClosePipes();
    return false;
  }

  // Ensure read handle is not inherited
  if (!SetHandleInformation(mPipes.hOutputRead, HANDLE_FLAG_INHERIT, 0))
  {
    LogError("Failed to set output pipe handle information");
    ClosePipes();
    return false;
  }

  // Create stderr pipe
  if (!CreatePipe(&mPipes.hErrorRead, &mPipes.hErrorWrite, &sa, 0))
  {
    LogError("Failed to create error pipe");
    ClosePipes();
    return false;
  }

  // Ensure read handle is not inherited
  if (!SetHandleInformation(mPipes.hErrorRead, HANDLE_FLAG_INHERIT, 0))
  {
    LogError("Failed to set error pipe handle information");
    ClosePipes();
    return false;
  }

  return true;
}

void FFmpegPipeManager::ClosePipes()
{
  if (mPipes.hInputRead != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hInputRead);
    mPipes.hInputRead = INVALID_HANDLE_VALUE;
  }
  if (mPipes.hInputWrite != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hInputWrite);
    mPipes.hInputWrite = INVALID_HANDLE_VALUE;
  }
  if (mPipes.hOutputRead != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hOutputRead);
    mPipes.hOutputRead = INVALID_HANDLE_VALUE;
  }
  if (mPipes.hOutputWrite != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hOutputWrite);
    mPipes.hOutputWrite = INVALID_HANDLE_VALUE;
  }
  if (mPipes.hErrorRead != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hErrorRead);
    mPipes.hErrorRead = INVALID_HANDLE_VALUE;
  }
  if (mPipes.hErrorWrite != INVALID_HANDLE_VALUE)
  {
    CloseHandle(mPipes.hErrorWrite);
    mPipes.hErrorWrite = INVALID_HANDLE_VALUE;
  }
}

//==============================================================================
// Internal Methods - Process Management
//==============================================================================

bool FFmpegPipeManager::LaunchProcesses(const Config& config)
{
  // Create intermediate pipe (encoder stdout -> decoder stdin)
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  if (!CreatePipe(&mIntermediatePipeRead, &mIntermediatePipeWrite, &sa, 0))
  {
    LogError("Failed to create intermediate pipe");
    return false;
  }

  // Build command lines
  std::string encoderCmd = BuildEncoderCommand(config);
  std::string decoderCmd = BuildDecoderCommand(config);
  Log("Encoder command: " + encoderCmd);
  Log("Decoder command: " + decoderCmd);

  // Create Job Object for process tree management
  mJobObject = CreateJobObject(nullptr, nullptr);
  if (mJobObject)
  {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(mJobObject, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
  }

  // === Launch Encoder Process ===
  // stdin=our input pipe, stdout=intermediate pipe write, stderr=our error pipe
  {
    STARTUPINFOA si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = mPipes.hInputRead;
    si.hStdOutput = mIntermediatePipeWrite;
    si.hStdError = mPipes.hErrorWrite;
    si.wShowWindow = SW_HIDE;

    std::vector<char> cmdBuf(encoderCmd.begin(), encoderCmd.end());
    cmdBuf.push_back('\0');

    BOOL success = CreateProcessA(
      nullptr, cmdBuf.data(), nullptr, nullptr,
      TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
      &si, &mEncoderProcessInfo
    );

    if (!success)
    {
      DWORD error = GetLastError();
      LogError("Failed to create encoder process (error: " + std::to_string(error) + ")");
      return false;
    }

    if (mJobObject)
      AssignProcessToJobObject(mJobObject, mEncoderProcessInfo.hProcess);
  }

  // === Launch Decoder Process ===
  // stdin=intermediate pipe read, stdout=our output pipe, stderr=our error pipe
  {
    STARTUPINFOA si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = mIntermediatePipeRead;
    si.hStdOutput = mPipes.hOutputWrite;
    si.hStdError = mPipes.hErrorWrite;
    si.wShowWindow = SW_HIDE;

    std::vector<char> cmdBuf(decoderCmd.begin(), decoderCmd.end());
    cmdBuf.push_back('\0');

    BOOL success = CreateProcessA(
      nullptr, cmdBuf.data(), nullptr, nullptr,
      TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
      &si, &mDecoderProcessInfo
    );

    if (!success)
    {
      DWORD error = GetLastError();
      LogError("Failed to create decoder process (error: " + std::to_string(error) + ")");
      // Kill encoder since decoder failed
      ::TerminateProcess(mEncoderProcessInfo.hProcess, 1);
      CloseHandle(mEncoderProcessInfo.hProcess);
      CloseHandle(mEncoderProcessInfo.hThread);
      std::memset(&mEncoderProcessInfo, 0, sizeof(mEncoderProcessInfo));
      return false;
    }

    if (mJobObject)
      AssignProcessToJobObject(mJobObject, mDecoderProcessInfo.hProcess);
  }

  // Close pipe ends that belong to child processes
  CloseHandle(mPipes.hInputRead);
  mPipes.hInputRead = INVALID_HANDLE_VALUE;
  CloseHandle(mPipes.hOutputWrite);
  mPipes.hOutputWrite = INVALID_HANDLE_VALUE;
  CloseHandle(mPipes.hErrorWrite);
  mPipes.hErrorWrite = INVALID_HANDLE_VALUE;

  // Close intermediate pipe ends that belong to children
  // Encoder owns the write end, decoder owns the read end
  CloseHandle(mIntermediatePipeWrite);
  mIntermediatePipeWrite = INVALID_HANDLE_VALUE;
  CloseHandle(mIntermediatePipeRead);
  mIntermediatePipeRead = INVALID_HANDLE_VALUE;

  return true;
}

void FFmpegPipeManager::TerminateProcesses()
{
  if (mJobObject)
  {
    CloseHandle(mJobObject);
    mJobObject = nullptr;
  }

  if (mEncoderProcessInfo.hProcess != nullptr)
  {
    DWORD exitCode;
    if (GetExitCodeProcess(mEncoderProcessInfo.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
    {
      ::TerminateProcess(mEncoderProcessInfo.hProcess, 1);
      WaitForSingleObject(mEncoderProcessInfo.hProcess, 3000);
    }
    CloseHandle(mEncoderProcessInfo.hProcess);
    CloseHandle(mEncoderProcessInfo.hThread);
    std::memset(&mEncoderProcessInfo, 0, sizeof(mEncoderProcessInfo));
  }

  if (mDecoderProcessInfo.hProcess != nullptr)
  {
    DWORD exitCode;
    if (GetExitCodeProcess(mDecoderProcessInfo.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
    {
      ::TerminateProcess(mDecoderProcessInfo.hProcess, 1);
      WaitForSingleObject(mDecoderProcessInfo.hProcess, 3000);
    }
    CloseHandle(mDecoderProcessInfo.hProcess);
    CloseHandle(mDecoderProcessInfo.hThread);
    std::memset(&mDecoderProcessInfo, 0, sizeof(mDecoderProcessInfo));
  }
}

std::string FFmpegPipeManager::BuildEncoderCommand(const Config& config) const
{
  std::string muxFormat = config.muxerFormat;
  if (muxFormat.empty())
    muxFormat = GetIntermediateFormat(config.codecName);  // Fallback

  std::ostringstream oss;
  oss << "\"" << config.ffmpegPath << "\"";
  oss << " -hide_banner -loglevel warning";
  oss << " -f s16le";
  oss << " -ar " << config.sampleRate;
  oss << " -ac " << config.channels;
  oss << " -i pipe:0";
  oss << " -c:a " << config.codecName;
  oss << " -b:a " << config.bitrate;
  if (!config.additionalArgs.empty())
    oss << " " << config.additionalArgs;
  oss << " -f " << muxFormat;
  oss << " pipe:1";

  return oss.str();
}

std::string FFmpegPipeManager::BuildDecoderCommand(const Config& config) const
{
  std::string demuxFormat = config.demuxerFormat;
  if (demuxFormat.empty())
  {
    // Fallback: derive from codec name and fix adts->aac mapping
    demuxFormat = GetIntermediateFormat(config.codecName);
    if (demuxFormat == "adts")
      demuxFormat = "aac";
  }

  std::ostringstream oss;
  oss << "\"" << config.ffmpegPath << "\"";
  oss << " -hide_banner -loglevel warning";
  oss << " -f " << demuxFormat;
  oss << " -i pipe:0";
  oss << " -f s16le";
  oss << " -ar " << config.sampleRate;
  oss << " -ac " << config.channels;
  oss << " pipe:1";

  return oss.str();
}

std::string FFmpegPipeManager::GetIntermediateFormat(const std::string& codecName) const
{
  // Map codec names to appropriate container formats
  if (codecName.find("mp3") != std::string::npos || codecName.find("lame") != std::string::npos)
    return "mp3";
  else if (codecName.find("aac") != std::string::npos)
    return "adts";
  else if (codecName.find("opus") != std::string::npos)
    return "ogg";
  else if (codecName.find("vorbis") != std::string::npos)
    return "ogg";
  else if (codecName.find("flac") != std::string::npos)
    return "flac";
  else
    return "wav";  // Default fallback
}

//==============================================================================
// Internal Methods - Background Threads
//==============================================================================

void FFmpegPipeManager::ErrorReadThread()
{
  char buffer[4096];
  DWORD bytesRead;

  while (mIsRunning)
  {
    BOOL success = ReadFile(
      mPipes.hErrorRead,
      buffer,
      sizeof(buffer) - 1,
      &bytesRead,
      nullptr
    );

    if (!success || bytesRead == 0)
    {
      DWORD error = GetLastError();
      if (error != ERROR_BROKEN_PIPE)
      {
        Log("Error pipe read failed: " + std::to_string(error));
      }
      break;
    }

    buffer[bytesRead] = '\0';
    Log("FFmpeg stderr: " + std::string(buffer));
  }
}

void FFmpegPipeManager::OutputReadThread()
{
  std::vector<uint8_t> tempBuffer(mConfig.bufferSize);
  DWORD bytesRead;

  while (mIsRunning)
  {
    BOOL success = ReadFile(
      mPipes.hOutputRead,
      tempBuffer.data(),
      static_cast<DWORD>(tempBuffer.size()),
      &bytesRead,
      nullptr
    );

    if (!success || bytesRead == 0)
    {
      DWORD error = GetLastError();
      if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA)
      {
        Log("Output pipe read failed: " + std::to_string(error));
      }
      break;
    }

    // Store raw bytes
    std::lock_guard<std::mutex> lock(mOutputMutex);
    mOutputRawBuffer.insert(mOutputRawBuffer.end(),
                           tempBuffer.begin(),
                           tempBuffer.begin() + bytesRead);

    // Convert complete S16LE samples to float
    size_t sampleSize = sizeof(int16_t) * mConfig.channels;
    size_t numCompleteSamples = mOutputRawBuffer.size() / sampleSize;

    if (numCompleteSamples > 0)
    {
      size_t bytesToProcess = numCompleteSamples * sampleSize;
      std::vector<float> floatSamples(numCompleteSamples * mConfig.channels);

      S16LEToFloat(
        reinterpret_cast<const int16_t*>(mOutputRawBuffer.data()),
        floatSamples.data(),
        numCompleteSamples * mConfig.channels
      );

      // Add to output queue
      for (float sample : floatSamples)
      {
        mOutputFloatBuffer.push(sample);
      }

      // Remove processed bytes
      mOutputRawBuffer.erase(mOutputRawBuffer.begin(),
                            mOutputRawBuffer.begin() + bytesToProcess);
    }
  }
}

void FFmpegPipeManager::InputWriteThread()
{
  std::vector<float> localBuffer;
  std::vector<int16_t> s16Buffer;

  while (mIsRunning)
  {
    // Grab data from the input queue
    {
      std::lock_guard<std::mutex> lock(mInputMutex);
      if (!mInputFloatBuffer.empty())
      {
        localBuffer.swap(mInputFloatBuffer);
        mInputFloatBuffer.clear();
      }
    }

    if (localBuffer.empty())
    {
      Sleep(1);  // Reduce CPU load when no data
      continue;
    }

    // Convert float -> S16LE
    s16Buffer.resize(localBuffer.size());
    FloatToS16LE(localBuffer.data(), s16Buffer.data(), localBuffer.size());
    localBuffer.clear();

    // Write to pipe (blocking OK - this is a worker thread)
    DWORD bytesToWrite = static_cast<DWORD>(s16Buffer.size() * sizeof(int16_t));
    DWORD bytesWritten = 0;
    DWORD offset = 0;

    while (offset < bytesToWrite && mIsRunning)
    {
      BOOL success = WriteFile(
        mPipes.hInputWrite,
        reinterpret_cast<const uint8_t*>(s16Buffer.data()) + offset,
        bytesToWrite - offset,
        &bytesWritten,
        nullptr
      );

      if (!success || bytesWritten == 0)
      {
        DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA)
        {
          Log("Input pipe broken - FFmpeg process may have terminated");
          mIsRunning = false;
        }
        break;
      }
      offset += bytesWritten;
    }
  }
}

//==============================================================================
// Internal Methods - Format Conversion
//==============================================================================

void FFmpegPipeManager::FloatToS16LE(const float* input, int16_t* output, size_t numSamples)
{
  for (size_t i = 0; i < numSamples; ++i)
  {
    float sample = input[i];

    // Clamp to [-1.0, 1.0]
    sample = std::max(-1.0f, std::min(1.0f, sample));

    // Convert to S16
    int32_t intSample = static_cast<int32_t>(sample * 32767.0f);
    output[i] = static_cast<int16_t>(intSample);
  }
}

void FFmpegPipeManager::S16LEToFloat(const int16_t* input, float* output, size_t numSamples)
{
  for (size_t i = 0; i < numSamples; ++i)
  {
    output[i] = static_cast<float>(input[i]) / 32768.0f;
  }
}

//==============================================================================
// Internal Methods - Logging
//==============================================================================

void FFmpegPipeManager::Log(const std::string& message)
{
  if (mLogCallback)
  {
    mLogCallback(message);
  }
}

void FFmpegPipeManager::LogError(const std::string& message)
{
  mLastError = message;

  DWORD error = ::GetLastError();
  std::string fullMessage = message;
  if (error != 0)
  {
    char buf[256];
    FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      buf,
      sizeof(buf),
      nullptr
    );
    fullMessage += " (Windows error: " + std::string(buf) + ")";
  }

  Log("ERROR: " + fullMessage);
}
