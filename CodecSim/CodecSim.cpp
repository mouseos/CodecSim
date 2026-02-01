#include "CodecSim.h"
#include "IPlug_include_in_plug_src.h"
#include "CodecProcessor.h"
#include "CodecRegistry.h"

#if IPLUG_EDITOR
#include "IControls.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include <debugapi.h>
#include <cstdio>

static void DebugLogCodecSim(const std::string& msg)
{
  FILE* f = fopen("D:\\ffmpeg_debug.log", "a");
  if (f) {
    fprintf(f, "[CodecSim] %s\n", msg.c_str());
    fflush(f);
    fclose(f);
  }
  OutputDebugStringA(("[CodecSim] " + msg + "\n").c_str());
}
#else
static void DebugLogCodecSim(const std::string& msg) { (void)msg; }
#endif

//==============================================================================
// Color Definitions
//==============================================================================
namespace Colors
{
  const IColor Background(255, 30, 30, 30);
  const IColor Panel(255, 45, 45, 45);
  const IColor PanelBorder(255, 80, 80, 80);
  const IColor AccentBlue(255, 100, 180, 255);
  const IColor AccentBlueDark(255, 50, 120, 200);
  const IColor TextWhite(255, 255, 255, 255);
  const IColor TextGray(255, 200, 200, 200);
  const IColor SliderHandle(255, 255, 255, 255);
}

//==============================================================================
// UI Layout Constants
//==============================================================================
namespace Layout
{
  constexpr float Padding = 10.f;
  constexpr float TitleBarHeight = 40.f;
  constexpr float MainPanelWidth = 280.f;
  constexpr float DetailPanelWidth = 290.f;
  constexpr float SliderHeight = 30.f;
  constexpr float LabelHeight = 20.f;
  constexpr float ControlSpacing = 15.f;
  constexpr float SectionSpacing = 25.f;
  constexpr float CodecSelectorHeight = 35.f;
}

//==============================================================================
// Bitrate & Sample Rate Presets
//==============================================================================
static const int kBitratePresets[] = {32, 48, 64, 96, 128, 160, 192, 256, 320};
static const int kNumBitratePresets = sizeof(kBitratePresets) / sizeof(kBitratePresets[0]);
// Index kNumBitratePresets = "Other" (custom input)

static const int kSampleRatePresets[] = {8000, 16000, 22050, 32000, 44100, 48000, 88200, 96000};
static const int kNumSampleRatePresets = sizeof(kSampleRatePresets) / sizeof(kSampleRatePresets[0]);

//==============================================================================
// Spinner Overlay Control (full-screen overlay + centered rotating arc)
//==============================================================================
class SpinnerOverlayControl : public IControl
{
public:
  SpinnerOverlayControl(const IRECT& bounds,
    const IColor& overlayColor = IColor(120, 0, 0, 0),
    const IColor& arcColor = IColor(255, 100, 180, 255),
    float arcRadius = 24.f, float thickness = 4.f)
  : IControl(bounds)
  , mOverlayColor(overlayColor)
  , mArcColor(arcColor)
  , mArcRadius(arcRadius)
  , mThickness(thickness)
  {
    mIgnoreMouse = false; // block mouse input to controls behind
    Hide(true);
  }

  void Draw(IGraphics& g) override
  {
    // Semi-transparent dark overlay
    g.FillRect(mOverlayColor, mRECT);

    // Centered spinning arc
    float cx = mRECT.MW();
    float cy = mRECT.MH();
    float angle = static_cast<float>(GetAnimationProgress()) * 360.f;
    g.DrawArc(mArcColor, cx, cy, mArcRadius, angle, angle + 270.f, nullptr, mThickness);

    // "Loading..." text below spinner
    IRECT textRect(cx - 60.f, cy + mArcRadius + 8.f, cx + 60.f, cy + mArcRadius + 28.f);
    g.DrawText(IText(13.f, IColor(200, 255, 255, 255), "Roboto-Regular", EAlign::Center), "Loading...", textRect);
  }

  void OnEndAnimation() override
  {
    // Restart animation loop while visible
    if (!IsHidden())
      SetAnimation([](IControl* pCaller) { pCaller->SetDirty(false); }, 800);
  }

  void StartSpinning()
  {
    Hide(false);
    SetAnimation([](IControl* pCaller) { pCaller->SetDirty(false); }, 800);
  }

  void StopSpinning()
  {
    SetAnimation(nullptr);
    Hide(true);
  }

private:
  IColor mOverlayColor;
  IColor mArcColor;
  float mArcRadius;
  float mThickness;
};

//==============================================================================
// Constructor
//==============================================================================
CodecSim::CodecSim(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mCurrentCodecIndex(0)
, mSampleRate(48000)
, mNumChannels(2)
, mLatencySamples(0)
{
  DebugLogCodecSim("Constructor - START");

  // Detect available codecs from ffmpeg
  CodecRegistry::Instance().DetectAvailable(FFmpegPipeManager::ResolveFFmpegPath());

  auto availableCodecs = CodecRegistry::Instance().GetAvailable();
  int numAvailable = static_cast<int>(availableCodecs.size());
  DebugLogCodecSim("Available codecs: " + std::to_string(numAvailable));

  if (numAvailable == 0)
  {
    DebugLogCodecSim("WARNING: No codecs available! Is ffmpeg in PATH?");
    numAvailable = 1;
  }

  // Initialize codec selection parameter
  GetParam(kParamCodec)->InitEnum("Codec", 0, numAvailable);
  for (int i = 0; i < static_cast<int>(availableCodecs.size()); i++)
    GetParam(kParamCodec)->SetDisplayText(i, availableCodecs[i]->displayName.c_str());

  // Bitrate preset selector
  GetParam(kParamBitrate)->InitEnum("Bitrate", 4, kNumBitratePresets + 1);
  for (int i = 0; i < kNumBitratePresets; i++)
  {
    char label[32];
    snprintf(label, sizeof(label), "%d kbps", kBitratePresets[i]);
    GetParam(kParamBitrate)->SetDisplayText(i, label);
  }
  GetParam(kParamBitrate)->SetDisplayText(kNumBitratePresets, "Other");

  // Custom bitrate (used when "Other" is selected)
  GetParam(kParamBitrateCustom)->InitInt("Bitrate (Custom)", 128, 8, 640, "kbps");

  // Initialize bitrate presets for the default codec
  UpdateBitrateForCodec(0);

  // Initialize option values for the default codec
  {
    const CodecInfo* defaultInfo = CodecRegistry::Instance().GetAvailableByIndex(0);
    if (defaultInfo)
    {
      for (const auto& opt : defaultInfo->options)
        mCodecOptionValues[opt.key] = opt.defaultValue;
    }
  }

  // Sample rate selection
  GetParam(kParamSampleRate)->InitEnum("Sample Rate", 5, kNumSampleRatePresets);
  for (int i = 0; i < kNumSampleRatePresets; i++)
  {
    char label[32];
    if (kSampleRatePresets[i] >= 1000)
      snprintf(label, sizeof(label), "%g kHz", kSampleRatePresets[i] / 1000.0);
    else
      snprintf(label, sizeof(label), "%d Hz", kSampleRatePresets[i]);
    GetParam(kParamSampleRate)->SetDisplayText(i, label);
  }

  // Start/Stop toggle
  GetParam(kParamEnabled)->InitBool("Enabled", false);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (!pGraphics) return;

    const IRECT bounds = pGraphics->GetBounds();

    if (pGraphics->NControls()) {
      pGraphics->GetBackgroundControl()->SetTargetAndDrawRECTs(bounds);
      return;
    }

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->AttachPanelBackground(Colors::Background);

    // Styles
    const IVStyle sliderStyle = IVStyle({
      IColor(255, 45, 45, 45),
      IColor(255, 70, 70, 70),
      IColor(255, 100, 180, 255),
      IColor(255, 90, 90, 90),
      IColor(255, 130, 200, 255),
      IColor(255, 30, 30, 30),
      Colors::TextWhite,
      Colors::TextWhite,
      Colors::TextWhite
    }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.2f);

    const IVStyle tabStyle = IVStyle({
      IColor(255, 55, 55, 55),
      IColor(255, 75, 75, 75),
      IColor(255, 100, 180, 255),
      IColor(255, 90, 90, 90),
      IColor(255, 100, 100, 100),
      IColor(255, 30, 30, 30),
      Colors::TextWhite,
      Colors::TextWhite,
      Colors::TextWhite
    }).WithLabelText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.1f);

    //==========================================================================
    // Title Bar
    //==========================================================================
    const IRECT titleBarBounds = bounds.GetFromTop(Layout::TitleBarHeight);
    const IRECT titleTextBounds = titleBarBounds.GetFromLeft(200.f).GetPadded(-Layout::Padding);

    pGraphics->AttachControl(
      new ITextControl(titleTextBounds, "CodecSim",
        IText(24.f, Colors::TextWhite, "Roboto-Regular", EAlign::Near, EVAlign::Middle)),
      kCtrlTagTitle
    );

    //==========================================================================
    // Main Panel (Left Side)
    //==========================================================================
    const IRECT contentArea = bounds.GetReducedFromTop(Layout::TitleBarHeight).GetPadded(-Layout::Padding);
    const IRECT mainPanelBounds = contentArea.GetFromLeft(Layout::MainPanelWidth);

    pGraphics->AttachControl(new IPanelControl(mainPanelBounds, Colors::Panel));

    const IRECT mainPanelInner = mainPanelBounds.GetPadded(-Layout::Padding);
    float yPos = mainPanelInner.T;

    // Section: Codec Selection
    const IRECT codecLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(codecLabelBounds, "Codec",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    // Build codec name list for selector
    auto codecList = CodecRegistry::Instance().GetAvailable();
    std::vector<const char*> codecNames;
    for (const auto* c : codecList)
      codecNames.push_back(c->displayName.c_str());

    if (codecNames.empty())
      codecNames.push_back("(none)");

    const IRECT codecSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);

    if (codecNames.size() <= 4)
    {
      pGraphics->AttachControl(
        new IVTabSwitchControl(codecSelectorBounds, kParamCodec, codecNames, "", tabStyle),
        kCtrlTagCodecSelector
      );
    }
    else
    {
      // For more than 4 codecs, use a dropdown menu
      pGraphics->AttachControl(
        new IVMenuButtonControl(codecSelectorBounds, kParamCodec, "", tabStyle),
        kCtrlTagCodecSelector
      );
    }
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Bitrate
    const IRECT bitrateLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(bitrateLabelBounds, "Bitrate",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)),
      kCtrlTagBitrateLabel);
    yPos += Layout::LabelHeight + 5.f;

    const IRECT bitrateSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);
    pGraphics->AttachControl(
      new IVMenuButtonControl(bitrateSelectorBounds, kParamBitrate, "", tabStyle),
      kCtrlTagBitrateSelector
    );
    yPos += Layout::CodecSelectorHeight + 5.f;

    // Custom bitrate input (visible when "Other" is selected)
    const IRECT bitrateCustomBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::SliderHeight);
    pGraphics->AttachControl(
      new IVNumberBoxControl(bitrateCustomBounds, kParamBitrateCustom, nullptr, "", sliderStyle, true, 128.0, 8.0, 640.0, "%.0f kbps"),
      kCtrlTagBitrateCustom
    );
    yPos += Layout::SliderHeight + Layout::SectionSpacing;

    // Section: Sample Rate
    const IRECT sampleRateLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(sampleRateLabelBounds, "Sample Rate",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    const IRECT sampleRateSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);
    pGraphics->AttachControl(
      new IVMenuButtonControl(sampleRateSelectorBounds, kParamSampleRate, "", tabStyle),
      kCtrlTagSampleRateSelector
    );
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Start/Stop Button
    const IRECT startStopBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight + 5.f);
    pGraphics->AttachControl(
      new IVToggleControl(startStopBounds, kParamEnabled, "", tabStyle, "Start", "Stop"),
      kCtrlTagStartStopButton
    );

    //==========================================================================
    // Detail Settings Panel (Right Side) - Tabbed: Options / Log
    //==========================================================================
    const IRECT detailPanelBounds = contentArea.GetFromRight(Layout::DetailPanelWidth);
    pGraphics->AttachControl(new IPanelControl(detailPanelBounds, Colors::Panel));

    const IRECT detailPanelInner = detailPanelBounds.GetPadded(-Layout::Padding);

    // Tab switch at top of panel
    constexpr float kTabHeight = 28.f;
    const IRECT tabBounds = IRECT(detailPanelInner.L, detailPanelInner.T,
                                   detailPanelInner.R, detailPanelInner.T + kTabHeight);

    auto* pTabSwitch = new IVTabSwitchControl(tabBounds, kNoParameter,
      {"Options", "Log"}, "", tabStyle);
    pTabSwitch->SetValue(1.0); // Default to Log tab
    pGraphics->AttachControl(pTabSwitch, kCtrlTagDetailTabSwitch);

    // Content area below tabs
    const IRECT tabContentBounds = IRECT(detailPanelInner.L, detailPanelInner.T + kTabHeight + 5.f,
                                          detailPanelInner.R, detailPanelInner.B);

    // --- Options tab: pre-allocate 5 label slots ---
    constexpr float kOptLabelH = 16.f;
    constexpr float kOptControlH = 28.f;
    constexpr float kOptSpacing = 6.f;
    constexpr float kOptBlockH = kOptLabelH + kOptControlH + kOptSpacing;

    for (int i = 0; i < 5; i++)
    {
      float yTop = tabContentBounds.T + i * kOptBlockH;
      IRECT labelBounds(tabContentBounds.L, yTop, tabContentBounds.R, yTop + kOptLabelH);
      auto* pLabel = new ITextControl(labelBounds, "",
        IText(11.f, Colors::TextGray, "Roboto-Regular", EAlign::Near));
      pLabel->Hide(true);
      pGraphics->AttachControl(pLabel, kCtrlTagOptionLabel0 + i * 2);

      // Placeholder control for each slot (will be dynamically replaced)
      IRECT ctrlBounds(tabContentBounds.L, yTop + kOptLabelH + 2.f,
                        tabContentBounds.R, yTop + kOptLabelH + 2.f + kOptControlH);
      auto* pPlaceholder = new ITextControl(ctrlBounds, "",
        IText(11.f, Colors::TextGray, "Roboto-Regular"));
      pPlaceholder->Hide(true);
      pGraphics->AttachControl(pPlaceholder, kCtrlTagOptionControl0 + i * 2);
    }

    // "No additional options" text
    auto* pNoOpts = new ITextControl(tabContentBounds, "No additional options",
      IText(12.f, Colors::TextGray, "Roboto-Regular", EAlign::Center, EVAlign::Middle));
    pNoOpts->Hide(true);
    pGraphics->AttachControl(pNoOpts, kCtrlTagNoOptionsText);

    // --- Log tab ---
    pGraphics->AttachControl(
      new IMultiLineTextControl(tabContentBounds, "Press Start to begin...",
        IText(10.f, Colors::TextGray, "Roboto-Regular", EAlign::Near, EVAlign::Top)),
      kCtrlTagLogDisplay
    );

    //==========================================================================
    // Loading Overlay (full-screen, rendered last = on top of everything)
    //==========================================================================
    pGraphics->AttachControl(
      new SpinnerOverlayControl(bounds, IColor(120, 0, 0, 0), Colors::AccentBlue, 28.f, 4.f),
      kCtrlTagSpinner
    );

    // Initialize options UI for the default codec
    UpdateOptionsForCodec(0);
  };
#endif

  // Log detected codecs
  for (const auto* c : availableCodecs)
    AddLogMessage("Detected: " + c->displayName + " (" + c->encoderName + ")");

  mConstructed = true;
  DebugLogCodecSim("Constructor - END");
}

CodecSim::~CodecSim()
{
  if (mInitThread.joinable())
    mInitThread.join();
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);
  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }
}

// Helper: get effective bitrate from preset or custom input
int CodecSim::GetEffectiveBitrate()
{
  int presetIdx = GetParam(kParamBitrate)->Int();
  int numPresets = static_cast<int>(mCurrentBitratePresets.size());
  if (numPresets == 0)
    return 128; // fallback
  if (mCurrentCodecHasOther && presetIdx >= numPresets)
    return GetParam(kParamBitrateCustom)->Int();
  if (presetIdx < numPresets)
    return mCurrentBitratePresets[presetIdx];
  return mCurrentBitratePresets[0];
}

void CodecSim::OnReset()
{
  DebugLogCodecSim("OnReset START");

  int sampleRateIndex = GetParam(kParamSampleRate)->Int();
  if (sampleRateIndex >= 0 && sampleRateIndex < kNumSampleRatePresets)
    mSampleRate = kSampleRatePresets[sampleRateIndex];
  else
    mSampleRate = 48000;
  mNumChannels = 2;

  bool enabled = GetParam(kParamEnabled)->Bool();
  if (enabled)
  {
    std::lock_guard<std::recursive_mutex> lock(mCodecMutex);
    InitializeCodec(mCurrentCodecIndex);
  }

  DebugLogCodecSim("OnReset END");
}

void CodecSim::OnParamChange(int paramIdx)
{
  if (!mConstructed) return;

  DebugLogCodecSim("OnParamChange: paramIdx=" + std::to_string(paramIdx));
  switch (paramIdx) {
    case kParamCodec:
    {
      mCurrentCodecIndex = GetParam(kParamCodec)->Int();
      const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
      if (info)
        AddLogMessage("Codec: " + info->displayName + ". Press Start to apply.");
      UpdateBitrateForCodec(mCurrentCodecIndex);
      UpdateOptionsForCodec(mCurrentCodecIndex);
    }
    break;
    case kParamBitrate:
    {
      int presetIdx = GetParam(kParamBitrate)->Int();
      int numPresets = static_cast<int>(mCurrentBitratePresets.size());
      if (mCurrentCodecHasOther && presetIdx >= numPresets)
        AddLogMessage("Bitrate: Other (custom). Press Start to apply.");
      else if (presetIdx < numPresets)
        AddLogMessage("Bitrate: " + std::to_string(mCurrentBitratePresets[presetIdx]) + " kbps. Press Start to apply.");
    }
    break;
    case kParamBitrateCustom:
    {
      int customBitrate = GetParam(kParamBitrateCustom)->Int();
      AddLogMessage("Custom bitrate: " + std::to_string(customBitrate) + " kbps. Press Start to apply.");
    }
    break;
    case kParamSampleRate:
    {
      int sampleRateIndex = GetParam(kParamSampleRate)->Int();
      if (sampleRateIndex >= 0 && sampleRateIndex < kNumSampleRatePresets)
        mSampleRate = kSampleRatePresets[sampleRateIndex];
      else
        mSampleRate = 48000;
      AddLogMessage("Sample rate: " + std::to_string(mSampleRate) + " Hz. Press Start to apply.");
    }
    break;
    case kParamEnabled:
    {
      bool enabled = GetParam(kParamEnabled)->Bool();
      mEnabled.store(enabled, std::memory_order_relaxed);
      DebugLogCodecSim("OnParamChange: Enabled=" + std::to_string(enabled));

      // Join any previous background thread
      if (mInitThread.joinable())
        mInitThread.join();

      mInitializing.store(true);

      // Start spinner immediately (don't wait for OnIdle)
      if (GetUI())
      {
        if (IControl* pSpinner = GetUI()->GetControlWithTag(kCtrlTagSpinner))
          dynamic_cast<SpinnerOverlayControl*>(pSpinner)->StartSpinning();
      }

      if (enabled)
      {
        mCurrentCodecIndex = GetParam(kParamCodec)->Int();
        AddLogMessage("Starting codec...");
        mInitThread = std::thread([this, codecIdx = mCurrentCodecIndex]() {
          InitializeCodec(codecIdx);
          mInitializing.store(false);
        });
      }
      else
      {
        AddLogMessage("Stopping codec...");
        mInitThread = std::thread([this]() {
          StopCodec();
          mInitializing.store(false);
        });
      }
    }
    break;
    default:
      break;
  }
}

void CodecSim::OnIdle()
{
  if (GetUI())
  {
    bool isRunning = GetParam(kParamEnabled)->Bool();

    // Disable all settings controls while codec is running
    if (IControl* pCodec = GetUI()->GetControlWithTag(kCtrlTagCodecSelector))
      pCodec->SetDisabled(isRunning);
    if (IControl* pBitrate = GetUI()->GetControlWithTag(kCtrlTagBitrateSelector))
      pBitrate->SetDisabled(isRunning);
    if (IControl* pSampleRate = GetUI()->GetControlWithTag(kCtrlTagSampleRateSelector))
      pSampleRate->SetDisabled(isRunning);

    // Show/hide loading spinner during initialization
    if (IControl* pSpinner = GetUI()->GetControlWithTag(kCtrlTagSpinner))
    {
      bool initializing = mInitializing.load();
      if (initializing && pSpinner->IsHidden())
        dynamic_cast<SpinnerOverlayControl*>(pSpinner)->StartSpinning();
      else if (!initializing && !pSpinner->IsHidden())
        dynamic_cast<SpinnerOverlayControl*>(pSpinner)->StopSpinning();
    }

    // Handle detail panel tab switching
    if (IControl* pTabSwitch = GetUI()->GetControlWithTag(kCtrlTagDetailTabSwitch))
    {
      int tabIdx = static_cast<int>(pTabSwitch->GetValue() * 1.0 + 0.5); // 0=Options, 1=Log
      if (tabIdx != mDetailTabIndex)
        SetDetailTab(tabIdx);
    }

    // Hide/show bitrate controls based on codec type
    bool hideBitrate = mCurrentCodecIsLossless;
    if (IControl* pBitrateLabel = GetUI()->GetControlWithTag(kCtrlTagBitrateLabel))
      pBitrateLabel->Hide(hideBitrate);
    if (IControl* pBitrate = GetUI()->GetControlWithTag(kCtrlTagBitrateSelector))
      pBitrate->Hide(hideBitrate);

    // Show/hide custom bitrate input
    if (IControl* pCustomBitrate = GetUI()->GetControlWithTag(kCtrlTagBitrateCustom))
    {
      int numPresets = static_cast<int>(mCurrentBitratePresets.size());
      bool isOther = mCurrentCodecHasOther && (GetParam(kParamBitrate)->Int() >= numPresets);
      pCustomBitrate->Hide(hideBitrate || !isOther);
      pCustomBitrate->SetDisabled(hideBitrate || !isOther || isRunning);
    }

    if (IControl* pLogCtrl = GetUI()->GetControlWithTag(kCtrlTagLogDisplay))
    {
      std::string logText;
      {
        std::lock_guard<std::mutex> lock(mLogMutex);
        for (const auto& line : mLogMessages)
          logText += line + "\n";
      }
      if (!logText.empty())
      {
        pLogCtrl->As<IMultiLineTextControl>()->SetStr(logText.c_str());
        pLogCtrl->SetDirty(false);
      }
    }
  }
}

void CodecSim::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nChans = 2;

  if (!mEnabled.load(std::memory_order_relaxed))
  {
    for (int s = 0; s < nFrames; s++)
      for (int c = 0; c < nChans; c++)
        outputs[c][s] = 0.0;
    return;
  }

  std::unique_lock<std::recursive_mutex> lock(mCodecMutex, std::try_to_lock);

  if (!lock.owns_lock() || !mCodecProcessor || !mCodecProcessor->IsInitialized())
  {
    for (int s = 0; s < nFrames; s++)
      for (int c = 0; c < nChans; c++)
        outputs[c][s] = 0.0;
    return;
  }

  float* inBuf = mInterleavedInput.data();
  float* outBuf = mInterleavedOutput.data();

  for (int s = 0; s < nFrames; s++)
  {
    inBuf[s * 2]     = static_cast<float>(inputs[0][s]);
    inBuf[s * 2 + 1] = static_cast<float>(inputs[1][s]);
  }

  int processedSamples = mCodecProcessor->Process(inBuf, nFrames, outBuf, nFrames);

  for (int s = 0; s < nFrames; s++)
  {
    if (s < processedSamples)
    {
      outputs[0][s] = static_cast<sample>(outBuf[s * 2]);
      outputs[1][s] = static_cast<sample>(outBuf[s * 2 + 1]);
    }
    else
    {
      outputs[0][s] = 0.0;
      outputs[1][s] = 0.0;
    }
  }
}

void CodecSim::InitializeCodec(int codecIndex)
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);

  if (mIsInitializing) {
    DebugLogCodecSim("InitializeCodec SKIPPED (re-entrant)");
    return;
  }
  mIsInitializing = true;

  DebugLogCodecSim("InitializeCodec START: index=" + std::to_string(codecIndex));

  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }

  const CodecInfo* codecInfo = CodecRegistry::Instance().GetAvailableByIndex(codecIndex);
  if (!codecInfo)
  {
    AddLogMessage("ERROR: Invalid codec index " + std::to_string(codecIndex));
    mIsInitializing = false;
    return;
  }

  auto processor = std::make_unique<GenericCodecProcessor>(*codecInfo);

  // Connect log callback
  processor->SetLogCallback([this](const std::string& msg) {
    AddLogMessage("[ffmpeg] " + msg);
  });

  // Set bitrate from UI
  int bitrateKbps = GetEffectiveBitrate();
  if (!codecInfo->isLossless)
    processor->SetBitrate(bitrateKbps);

  // Apply codec-specific options
  processor->SetAdditionalArgs(BuildCurrentAdditionalArgs());

  // Initialize (launches ffmpeg processes)
  if (processor->Initialize(mSampleRate, mNumChannels))
  {
    int latency = processor->GetLatencySamples();
    SetLatency(latency);
    mLatencySamples.store(latency);

    const int maxFrames = 8192;
    mInterleavedInput.resize(maxFrames * mNumChannels);
    mInterleavedOutput.resize(maxFrames * mNumChannels);

    AddLogMessage("Started: " + codecInfo->displayName +
                 " @ " + (codecInfo->isLossless ? "lossless" : std::to_string(bitrateKbps) + "kbps") +
                 ", " + std::to_string(mSampleRate) + "Hz");
  }
  else
  {
    AddLogMessage("ERROR: Failed to start " + codecInfo->displayName);
  }

  mCodecProcessor = std::move(processor);
  mIsInitializing = false;
  DebugLogCodecSim("InitializeCodec END");
}

void CodecSim::StopCodec()
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);

  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }

  AddLogMessage("Codec stopped.");
}

void CodecSim::UpdateBitrateForCodec(int codecIndex)
{
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(codecIndex);
  if (!info) return;

  mCurrentBitratePresets.clear();
  mCurrentCodecIsLossless = info->isLossless;
  mCurrentCodecHasOther = false;

  if (info->isLossless)
  {
    // Lossless codec: no bitrate control needed
    // UI hiding handled in OnIdle
    DebugLogCodecSim("UpdateBitrateForCodec: " + info->displayName + " is lossless, hiding bitrate");
  }
  else if (info->minBitrate == info->maxBitrate)
  {
    // Fixed bitrate: single option only
    mCurrentBitratePresets.push_back(info->minBitrate);
    DebugLogCodecSim("UpdateBitrateForCodec: " + info->displayName + " fixed at " + std::to_string(info->minBitrate) + " kbps");
  }
  else
  {
    // Variable bitrate: filter global presets to valid range
    for (int i = 0; i < kNumBitratePresets; i++)
    {
      if (kBitratePresets[i] >= info->minBitrate && kBitratePresets[i] <= info->maxBitrate)
        mCurrentBitratePresets.push_back(kBitratePresets[i]);
    }
    // Ensure default bitrate is in the list
    bool hasDefault = false;
    for (int bp : mCurrentBitratePresets)
    {
      if (bp == info->defaultBitrate) { hasDefault = true; break; }
    }
    if (!hasDefault && info->defaultBitrate > 0)
    {
      mCurrentBitratePresets.push_back(info->defaultBitrate);
      std::sort(mCurrentBitratePresets.begin(), mCurrentBitratePresets.end());
    }
    mCurrentCodecHasOther = true;
    DebugLogCodecSim("UpdateBitrateForCodec: " + info->displayName + " range " +
                     std::to_string(info->minBitrate) + "-" + std::to_string(info->maxBitrate) +
                     " kbps, " + std::to_string(mCurrentBitratePresets.size()) + " presets");
  }

  // Update the bitrate parameter
  IParam* pBitrate = GetParam(kParamBitrate);
  pBitrate->ClearDisplayTexts();

  int numPresets = static_cast<int>(mCurrentBitratePresets.size());
  int totalEntries = mCurrentCodecHasOther ? numPresets + 1 : std::max(numPresets, 1);

  // Find default preset index
  int defaultIdx = 0;
  for (int i = 0; i < numPresets; i++)
  {
    if (mCurrentBitratePresets[i] == info->defaultBitrate)
    {
      defaultIdx = i;
      break;
    }
  }

  if (mCurrentCodecIsLossless)
  {
    // For lossless: single dummy entry (UI will be hidden anyway)
    pBitrate->InitEnum("Bitrate", 0, 1);
    pBitrate->SetDisplayText(0, "N/A");
  }
  else
  {
    pBitrate->InitEnum("Bitrate", defaultIdx, totalEntries);
    for (int i = 0; i < numPresets; i++)
    {
      char label[32];
      snprintf(label, sizeof(label), "%d kbps", mCurrentBitratePresets[i]);
      pBitrate->SetDisplayText(i, label);
    }
    if (mCurrentCodecHasOther)
      pBitrate->SetDisplayText(numPresets, "Other");
  }

  // Update custom bitrate range
  if (!info->isLossless)
  {
    GetParam(kParamBitrateCustom)->InitInt("Bitrate (Custom)",
      info->defaultBitrate, info->minBitrate, info->maxBitrate, "kbps");
  }

  // Refresh UI controls - force IVMenuButtonControl to update its button text
  if (GetUI())
  {
    if (IControl* pCtrl = GetUI()->GetControlWithTag(kCtrlTagBitrateSelector))
    {
      double normVal = pBitrate->ToNormalized(static_cast<double>(defaultIdx));
      // SetValueFromUserInput has an equality guard, so set a different value first
      pCtrl->SetValue(1.0 - normVal, 0);
      pCtrl->SetValueFromUserInput(normVal, 0);
    }
  }
}

void CodecSim::SetDetailTab(int tabIndex)
{
  mDetailTabIndex = tabIndex;
  if (!GetUI()) return;

  bool showOptions = (tabIndex == 0);
  bool showLog = (tabIndex == 1);

  // Get current codec options count
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
  int numOptions = info ? static_cast<int>(info->options.size()) : 0;

  // Show/hide option labels and controls
  for (int i = 0; i < 5; i++)
  {
    bool showSlot = showOptions && (i < numOptions);
    if (IControl* p = GetUI()->GetControlWithTag(kCtrlTagOptionLabel0 + i * 2))
      p->Hide(!showSlot);
    if (IControl* p = GetUI()->GetControlWithTag(kCtrlTagOptionControl0 + i * 2))
      p->Hide(!showSlot);
  }

  // Show "no options" text if Options tab and zero options
  if (IControl* p = GetUI()->GetControlWithTag(kCtrlTagNoOptionsText))
    p->Hide(!showOptions || numOptions > 0);

  // Show/hide log
  if (IControl* p = GetUI()->GetControlWithTag(kCtrlTagLogDisplay))
    p->Hide(!showLog);
}

void CodecSim::UpdateOptionsForCodec(int codecIndex)
{
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(codecIndex);
  if (!info) return;

  // Reset option values to defaults
  mCodecOptionValues.clear();
  for (const auto& opt : info->options)
    mCodecOptionValues[opt.key] = opt.defaultValue;

  if (!GetUI()) return;
  IGraphics* pGraphics = GetUI();

  // Calculate content bounds (same as layout)
  const IRECT bounds = pGraphics->GetBounds();
  const IRECT contentArea = bounds.GetReducedFromTop(Layout::TitleBarHeight).GetPadded(-Layout::Padding);
  const IRECT detailPanelBounds = contentArea.GetFromRight(Layout::DetailPanelWidth);
  const IRECT detailPanelInner = detailPanelBounds.GetPadded(-Layout::Padding);
  constexpr float kTabHeight = 28.f;
  const IRECT tabContentBounds = IRECT(detailPanelInner.L, detailPanelInner.T + kTabHeight + 5.f,
                                        detailPanelInner.R, detailPanelInner.B);
  constexpr float kOptLabelH = 16.f;
  constexpr float kOptControlH = 28.f;
  constexpr float kOptSpacing = 6.f;
  constexpr float kOptBlockH = kOptLabelH + kOptControlH + kOptSpacing;

  int numOptions = static_cast<int>(info->options.size());
  if (numOptions > 5) numOptions = 5;

  for (int i = 0; i < 5; i++)
  {
    int labelTag = kCtrlTagOptionLabel0 + i * 2;
    int ctrlTag = kCtrlTagOptionControl0 + i * 2;

    if (i < numOptions)
    {
      const auto& opt = info->options[i];

      // Update label text
      if (IControl* pLabel = pGraphics->GetControlWithTag(labelTag))
        pLabel->As<ITextControl>()->SetStr(opt.label.c_str());

      // Remove old control and create new one
      // For compound controls (IContainerBase like IVNumberBoxControl),
      // children must be removed first or they remain as orphans in IGraphics
      if (IControl* pOld = pGraphics->GetControlWithTag(ctrlTag))
      {
        if (auto* pContainer = dynamic_cast<IContainerBase*>(pOld))
        {
          while (pContainer->NChildren() > 0)
            pContainer->RemoveChildControl(pContainer->GetChild(pContainer->NChildren() - 1));
        }
        pGraphics->RemoveControl(pOld);
      }

      float yTop = tabContentBounds.T + i * kOptBlockH;
      IRECT ctrlBounds(tabContentBounds.L, yTop + kOptLabelH + 2.f,
                        tabContentBounds.R, yTop + kOptLabelH + 2.f + kOptControlH);

      // Build the style inline (need tabStyle equivalent)
      const IVStyle optStyle = IVStyle({
        IColor(255, 55, 55, 55),
        IColor(255, 75, 75, 75),
        IColor(255, 100, 180, 255),
        IColor(255, 90, 90, 90),
        IColor(255, 100, 100, 100),
        IColor(255, 30, 30, 30),
        IColor(255, 255, 255, 255),
        IColor(255, 255, 255, 255),
        IColor(255, 255, 255, 255)
      }).WithLabelText(IText(11.f, IColor(255, 255, 255, 255), "Roboto-Regular"))
        .WithValueText(IText(11.f, IColor(255, 255, 255, 255), "Roboto-Regular"))
        .WithDrawFrame(true)
        .WithRoundness(0.1f);

      IControl* pNewCtrl = nullptr;

      switch (opt.type)
      {
        case CodecOptionType::Toggle:
        {
          auto* pToggle = new IVToggleControl(ctrlBounds, kNoParameter, "", optStyle, "Off", "On");
          pToggle->SetValue(opt.defaultValue != 0 ? 1.0 : 0.0);
          pToggle->SetActionFunction([this, key = opt.key](IControl* pCaller) {
            mCodecOptionValues[key] = pCaller->GetValue() > 0.5 ? 1 : 0;
          });
          pNewCtrl = pToggle;
          break;
        }
        case CodecOptionType::Choice:
        {
          if (static_cast<int>(opt.choices.size()) <= 4)
          {
            // Tab switch for few choices
            std::vector<const char*> labels;
            for (const auto& c : opt.choices)
              labels.push_back(c.label.c_str());

            auto* pTabs = new IVTabSwitchControl(ctrlBounds, kNoParameter, labels, "", optStyle);
            if (!opt.choices.empty())
              pTabs->SetValue(static_cast<double>(opt.defaultValue) / static_cast<double>(std::max(1, (int)opt.choices.size() - 1)));
            pTabs->SetActionFunction([this, key = opt.key](IControl* pCaller) {
              mCodecOptionValues[key] = pCaller->As<ISwitchControlBase>()->GetSelectedIdx();
            });
            pNewCtrl = pTabs;
          }
          else
          {
            // Dropdown menu for many choices (IVButtonControl + IPopupMenu)
            auto choices = opt.choices;
            std::string defaultLabel = (opt.defaultValue >= 0 && opt.defaultValue < static_cast<int>(choices.size()))
              ? choices[opt.defaultValue].label : choices[0].label;

            auto* pButton = new IVButtonControl(ctrlBounds,
              [this, key = opt.key, choices](IControl* pCaller) {
                IPopupMenu menu;
                for (const auto& c : choices)
                  menu.AddItem(new IPopupMenu::Item(c.label.c_str()));

                menu.SetFunction([this, key, pCaller](IPopupMenu* pMenu) {
                  int idx = pMenu->GetChosenItemIdx();
                  if (idx >= 0) {
                    mCodecOptionValues[key] = idx;
                    pCaller->As<IVectorBase>()->SetLabelStr(pMenu->GetChosenItem()->GetText());
                    pCaller->SetDirty(false);
                  }
                });

                pCaller->GetUI()->CreatePopupMenu(*pCaller, menu, pCaller->GetRECT());
              },
              defaultLabel.c_str(), optStyle, true, false);
            pNewCtrl = pButton;
          }
          break;
        }
        case CodecOptionType::IntRange:
        {
          const IVStyle numStyle = IVStyle({
            IColor(255, 45, 45, 45),
            IColor(255, 70, 70, 70),
            IColor(255, 100, 180, 255),
            IColor(255, 90, 90, 90),
            IColor(255, 130, 200, 255),
            IColor(255, 30, 30, 30),
            IColor(255, 255, 255, 255),
            IColor(255, 255, 255, 255),
            IColor(255, 255, 255, 255)
          }).WithLabelText(IText(11.f, IColor(255, 255, 255, 255), "Roboto-Regular"))
            .WithValueText(IText(11.f, IColor(255, 255, 255, 255), "Roboto-Regular"))
            .WithDrawFrame(true)
            .WithRoundness(0.2f);

          auto* pNum = new IVNumberBoxControl(ctrlBounds, kNoParameter, nullptr, "", numStyle,
            true, opt.defaultValue, opt.minValue, opt.maxValue);
          pNum->SetActionFunction([this, key = opt.key](IControl* pCaller) {
            double normVal = pCaller->GetValue();
            auto* pNB = pCaller->As<IVNumberBoxControl>();
            // Map normalized value back to integer range
            int minV = static_cast<int>(pNB->GetRealValue()); // approximate
            mCodecOptionValues[key] = minV;
          });
          pNewCtrl = pNum;
          break;
        }
      }

      if (pNewCtrl)
      {
        bool showSlot = (mDetailTabIndex == 0);
        pNewCtrl->Hide(!showSlot);
        pGraphics->AttachControl(pNewCtrl, ctrlTag);
      }
    }
    else
    {
      // Hide unused slots
      if (IControl* pLabel = pGraphics->GetControlWithTag(labelTag))
        pLabel->Hide(true);
      if (IControl* pCtrl = pGraphics->GetControlWithTag(ctrlTag))
        pCtrl->Hide(true);
    }
  }

  // Update tab visibility
  SetDetailTab(mDetailTabIndex);
}

std::string CodecSim::BuildCurrentAdditionalArgs()
{
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
  if (!info) return "";

  std::string result = info->additionalArgs;

  for (const auto& opt : info->options)
  {
    auto it = mCodecOptionValues.find(opt.key);
    int val = (it != mCodecOptionValues.end()) ? it->second : opt.defaultValue;

    switch (opt.type)
    {
      case CodecOptionType::Toggle:
        result += " " + opt.argName + " " + std::to_string(val);
        break;
      case CodecOptionType::Choice:
        if (val >= 0 && val < static_cast<int>(opt.choices.size()) && !opt.choices[val].argValue.empty())
          result += " " + opt.argName + " " + opt.choices[val].argValue;
        break;
      case CodecOptionType::IntRange:
        result += " " + opt.argName + " " + std::to_string(val);
        break;
    }
  }

  return result;
}

void CodecSim::AddLogMessage(const std::string& msg)
{
  std::lock_guard<std::mutex> lock(mLogMutex);
  mLogMessages.push_back(msg);
  while (static_cast<int>(mLogMessages.size()) > kMaxLogLines)
    mLogMessages.erase(mLogMessages.begin());
}
