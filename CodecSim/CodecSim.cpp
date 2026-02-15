#include "CodecSim.h"
#include "IPlug_include_in_plug_src.h"
#include "CodecProcessor.h"
#include "CodecRegistry.h"
#include <chrono>
#include <cstdlib>

#if IPLUG_EDITOR
#include "IControls.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
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
  constexpr float SectionSpacing = 18.f;
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
// Dropdown Arrow Overlay (draws a small ▼ triangle, transparent and non-interactive)
//==============================================================================
class DropdownArrowControl : public IControl
{
public:
  DropdownArrowControl(const IRECT& bounds, const IColor& color = IColor(180, 200, 200, 200))
    : IControl(bounds)
    , mColor(color)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    float cx = mRECT.MW();
    float cy = mRECT.MH();
    float hw = 4.f;
    float hh = 3.f;
    g.FillTriangle(mColor, cx - hw, cy - hh, cx + hw, cy - hh, cx, cy + hh);
  }

private:
  IColor mColor;
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

  // Pre-allocate interleaved buffers (ensures valid even before codec init)
  const int maxFrames = 8192;
  mInterleavedInput.resize(maxFrames * 2, 0.f);
  mInterleavedOutput.resize(maxFrames * 2, 0.f);

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

  // Initialize codec selection parameter with defaults
  mCurrentCodecIndex = 0;
  GetParam(kParamCodec)->InitEnum("Codec", 0, numAvailable);
  for (int i = 0; i < static_cast<int>(availableCodecs.size()); i++)
    GetParam(kParamCodec)->SetDisplayText(i, availableCodecs[i]->displayName.c_str());

  // Bitrate preset selector (temporary init, will be re-initialized by UpdateBitrateForCodec)
  GetParam(kParamBitrate)->InitEnum("Bitrate", 4, kNumBitratePresets + 1);
  for (int i = 0; i < kNumBitratePresets; i++)
  {
    char label[32];
    snprintf(label, sizeof(label), "%d kbps", kBitratePresets[i]);
    GetParam(kParamBitrate)->SetDisplayText(i, label);
  }
  GetParam(kParamBitrate)->SetDisplayText(kNumBitratePresets, "Other");

  // Custom bitrate
  GetParam(kParamBitrateCustom)->InitInt("Bitrate (Custom)", 128, 8, 640, "kbps");

  // Initialize bitrate presets for default codec
  UpdateBitrateForCodec(0);

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

  // Enabled parameter (kept for state compatibility, always true)
  GetParam(kParamEnabled)->InitBool("Enabled", true);

  // Load standalone state (for VST3 the host handles state via SerializeState/UnserializeState)
  LoadStandaloneState();

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

    // Selector/dropdown style - "input field" look (distinct from action buttons)
    const IVStyle selectorStyle = IVStyle({
      IColor(255, 38, 38, 38),        // kBG - slightly recessed
      IColor(255, 50, 50, 50),        // kFG - hover
      IColor(255, 100, 180, 255),     // kPR - accent on press
      IColor(255, 65, 65, 65),        // kFR - subtle frame
      IColor(255, 55, 55, 55),        // kHL - highlight
      IColor(255, 30, 30, 30),        // kSH - shadow
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
      new ITextControl(titleTextBounds, PLUG_NAME,
        IText(24.f, Colors::TextWhite, "Roboto-Regular", EAlign::Near, EVAlign::Middle)),
      kCtrlTagTitle
    );

    // Preset selector - flat text style (dropdown indicator via label)
    const IVStyle presetStyle = IVStyle({
      Colors::Background,              // kBG - match title bar
      IColor(255, 50, 50, 50),        // kFG - hover
      IColor(255, 60, 60, 60),        // kPR - pressed
      IColor(255, 70, 70, 70),        // kFR - frame (not drawn)
      IColor(255, 50, 50, 50),        // kHL - highlight
      Colors::Background,              // kSH - shadow
      Colors::TextGray,                // label text
      Colors::TextWhite,               // value text
      Colors::TextWhite                // extra
    }).WithLabelText(IText(12.f, Colors::TextGray, "Roboto-Regular"))
      .WithValueText(IText(12.f, Colors::TextGray, "Roboto-Regular"))
      .WithDrawFrame(false)
      .WithRoundness(0.3f);

    // Save button - flat text with subtle border (title bar integrated)
    const IVStyle saveBtnStyle = IVStyle({
      Colors::Background,              // kBG - match title bar
      IColor(255, 50, 50, 50),        // kFG - subtle hover
      IColor(255, 60, 60, 60),        // kPR - pressed
      IColor(255, 70, 70, 70),        // kFR - subtle frame
      IColor(255, 50, 50, 50),        // kHL - highlight
      Colors::Background,              // kSH - no shadow
      Colors::TextWhite,
      Colors::TextWhite,
      Colors::TextWhite
    }).WithLabelText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.3f);

    // Preset load dropdown
    const IRECT presetBounds = IRECT(titleBarBounds.R - 220.f, titleBarBounds.T + 6.f,
                                      titleBarBounds.R - 55.f, titleBarBounds.B - 6.f);
    auto* pPresetBtn = new IVButtonControl(presetBounds,
      [this](IControl* pCaller) {
        auto presets = GetUserPresetList();
        IPopupMenu menu;
        if (presets.empty())
        {
          menu.AddItem(new IPopupMenu::Item("(No presets)", IPopupMenu::Item::kDisabled));
        }
        else
        {
          for (const auto& name : presets)
            menu.AddItem(new IPopupMenu::Item(name.c_str()));
          menu.AddSeparator();
          // Delete submenu
          IPopupMenu* pDeleteMenu = new IPopupMenu();
          for (const auto& name : presets)
            pDeleteMenu->AddItem(new IPopupMenu::Item(name.c_str()));

          // SetFunction MUST be called BEFORE AddItem with submenu,
          // because AddItem inherits parent's function to the submenu.
          // When a submenu item is selected, the callback receives
          // the SUBMENU object as pMenu (not the parent menu).
          menu.SetFunction([this, pCaller, pDeleteMenu](IPopupMenu* pMenu) {
            if (!pMenu) return;
            auto* pItem = pMenu->GetChosenItem();
            if (!pItem) return;

            if (pMenu == pDeleteMenu)
            {
              // Delete with confirmation
              std::string name = pItem->GetText();
#ifdef _WIN32
              std::string msg = "Delete preset \"" + name + "\"?";
              int ret = MessageBoxA(NULL, msg.c_str(), PLUG_NAME, MB_YESNO | MB_ICONQUESTION);
              if (ret != IDYES) return;
#endif
              DeleteUserPreset(name);
              AddLogMessage("Deleted preset: " + name);
              return;
            }

            // Normal preset load
            std::string chosenText = pItem->GetText();
            auto presets = GetUserPresetList();
            for (const auto& name : presets)
            {
              if (name == chosenText)
              {
                LoadUserPreset(name);
                pCaller->As<IVectorBase>()->SetLabelStr(name.c_str());
                pCaller->SetDirty(false);
                return;
              }
            }
          });

          // MUST use AddItem(const char*, IPopupMenu*) overload (not AddItem(Item*))
          // because only this overload inherits parent's SetFunction to the submenu.
          menu.AddItem("Delete...", pDeleteMenu);
        }
        pCaller->GetUI()->CreatePopupMenu(*pCaller, menu, pCaller->GetRECT());
      },
      "Presets", presetStyle, true, false);
    pGraphics->AttachControl(pPresetBtn, kCtrlTagPresetSelector);
    // Arrow indicator for preset dropdown
    pGraphics->AttachControl(new DropdownArrowControl(
      IRECT(presetBounds.R - 22.f, presetBounds.T + 4.f,
            presetBounds.R - 8.f, presetBounds.B - 4.f)));

    // Save button
    const IRECT saveBtnBounds = IRECT(titleBarBounds.R - 50.f, titleBarBounds.T + 6.f,
                                       titleBarBounds.R - Layout::Padding, titleBarBounds.B - 6.f);
    auto* pSaveBtn = new IVButtonControl(saveBtnBounds,
      [this](IControl* pCaller) {
        // Show text entry for preset name
        if (IControl* pEntry = pCaller->GetUI()->GetControlWithTag(kCtrlTagPresetNameEntry))
        {
          pEntry->Hide(false);
          pEntry->SetDirty(false);
          pCaller->GetUI()->CreateTextEntry(*pEntry,
            IText(14.f, Colors::TextWhite, "Roboto-Regular"),
            pEntry->GetRECT(), "My Preset");
        }
      },
      "Save", saveBtnStyle, true, false);
    pGraphics->AttachControl(pSaveBtn, kCtrlTagPresetSaveButton);

    // Hidden text control for preset name entry (receives OnTextEntryCompletion)
    {
      const IRECT entryBounds = IRECT(presetBounds.L, presetBounds.T, presetBounds.R, presetBounds.B);
      class PresetNameEntryControl : public ITextControl
      {
      public:
        PresetNameEntryControl(const IRECT& bounds, CodecSim* pPlugin)
          : ITextControl(bounds, "", IText(14.f, IColor(255, 255, 255, 255), "Roboto-Regular"))
          , mPlugin(pPlugin)
        {
          Hide(true);
        }

        void OnTextEntryCompletion(const char* str, int valIdx) override
        {
          Hide(true);
          if (str && str[0])
          {
            std::string name(str);
            mPlugin->SaveUserPreset(name);
            mPlugin->AddLogMessage("Saved preset: " + name);
            // Update preset selector label
            if (GetUI())
            {
              if (IControl* pBtn = GetUI()->GetControlWithTag(kCtrlTagPresetSelector))
              {
                pBtn->As<IVectorBase>()->SetLabelStr(name.c_str());
                pBtn->SetDirty(false);
              }
            }
          }
        }
      private:
        CodecSim* mPlugin;
      };
      pGraphics->AttachControl(new PresetNameEntryControl(entryBounds, this), kCtrlTagPresetNameEntry);
    }

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
        new IVMenuButtonControl(codecSelectorBounds, kParamCodec, "", selectorStyle),
        kCtrlTagCodecSelector
      );
      // Arrow indicator for codec dropdown
      pGraphics->AttachControl(new DropdownArrowControl(
        IRECT(codecSelectorBounds.R - 25.f, codecSelectorBounds.T + 5.f,
              codecSelectorBounds.R - 8.f, codecSelectorBounds.B - 5.f)));
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
      new IVMenuButtonControl(bitrateSelectorBounds, kParamBitrate, "", selectorStyle),
      kCtrlTagBitrateSelector
    );
    // Arrow indicator for bitrate dropdown
    pGraphics->AttachControl(new DropdownArrowControl(
      IRECT(bitrateSelectorBounds.R - 25.f, bitrateSelectorBounds.T + 5.f,
            bitrateSelectorBounds.R - 8.f, bitrateSelectorBounds.B - 5.f)));
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
      new IVMenuButtonControl(sampleRateSelectorBounds, kParamSampleRate, "", selectorStyle),
      kCtrlTagSampleRateSelector
    );
    // Arrow indicator for sample rate dropdown
    pGraphics->AttachControl(new DropdownArrowControl(
      IRECT(sampleRateSelectorBounds.R - 25.f, sampleRateSelectorBounds.T + 5.f,
            sampleRateSelectorBounds.R - 8.f, sampleRateSelectorBounds.B - 5.f)));
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Apply Button
    const float applyHeight = Layout::CodecSelectorHeight;
    const IRECT applyBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + applyHeight);

    const IVStyle applyBtnStyle = IVStyle({
      IColor(255, 30, 100, 60),          // kBG - green bg
      IColor(255, 50, 140, 80),          // kFG - hover
      IColor(255, 40, 120, 70),          // kPR - pressed
      IColor(255, 60, 160, 90),          // kFR - frame
      IColor(255, 50, 140, 80),          // kHL - highlight
      IColor(0, 0, 0, 0),               // kSH - no shadow
      Colors::TextWhite, Colors::TextWhite, Colors::TextWhite
    }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithShowLabel(true).WithDrawFrame(true).WithDrawShadows(false).WithRoundness(4.f);

    auto* pApplyBtn = new IVButtonControl(applyBounds,
      [this](IControl* pCaller) {
        ApplyCodecSettings();
      },
      "Apply", applyBtnStyle, true, false);
    pGraphics->AttachControl(pApplyBtn, kCtrlTagApplyButton);

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
    pTabSwitch->SetValue(0.0); // Default to Options tab
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

    // Initialize options UI for the current codec (may have been restored from saved state)
    UpdateOptionsForCodec(mCurrentCodecIndex);
  };
#endif

  // Log detected codecs
  for (const auto* c : availableCodecs)
    AddLogMessage("Detected: " + c->displayName + " (" + c->encoderName + ")");

  mConstructed = true;

  DebugLogCodecSim("Host detected: " + std::to_string(static_cast<int>(GetHost())));

  // Auto-initialize codec with default settings
  ApplyCodecSettings();

  DebugLogCodecSim("Constructor - END");
}

CodecSim::~CodecSim()
{
  SaveStandaloneState();
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
  static int sResetCount = 0;
  DebugLogCodecSim("OnReset #" + std::to_string(++sResetCount));

  int sampleRateIndex = GetParam(kParamSampleRate)->Int();
  if (sampleRateIndex >= 0 && sampleRateIndex < kNumSampleRatePresets)
    mSampleRate = kSampleRatePresets[sampleRateIndex];
  else
    mSampleRate = 48000;
  mNumChannels = 2;

  // Note: Do NOT call InitializeCodec here.
  // OnReset is called by the host on playback start, sample rate change, and
  // after SetLatency triggers a restart. Calling InitializeCodec here causes
  // recursive re-initialization (SetLatency -> OnReset -> InitializeCodec -> SetLatency).
  // Codec init is handled by the Apply button (ApplyCodecSettings) and auto-init in constructor.

}

void CodecSim::OnParamChange(int paramIdx)
{
  if (!mConstructed) return;

  DebugLogCodecSim("OnParamChange: paramIdx=" + std::to_string(paramIdx));
  switch (paramIdx) {
    case kParamCodec:
    {
      int newCodecIndex = GetParam(kParamCodec)->Int();
      // Only trigger update if codec ACTUALLY changed.
      // Audacity re-sends all params after any value change, which would cause
      // an infinite loop: UpdateBitrateForCodec → host resync → OnParamChange(kParamCodec) → repeat.
      if (newCodecIndex != mCurrentCodecIndex)
      {
#ifdef CODECSIM_TRIAL
        // Trial: only MP3 is allowed
        const CodecInfo* trialCheck = CodecRegistry::Instance().GetAvailableByIndex(newCodecIndex);
        if (trialCheck && trialCheck->id != "mp3")
        {
          int mp3Index = CodecRegistry::Instance().GetAvailableIndexById("mp3");
          if (mp3Index < 0) mp3Index = 0;
          GetParam(kParamCodec)->Set(mp3Index);
          SendParameterValueFromDelegate(kParamCodec,
            GetParam(kParamCodec)->ToNormalized(static_cast<double>(mp3Index)), false);
          mCurrentCodecIndex = mp3Index;
          mPendingCodecUpdate.store(true);
          if (!mTrialDialogShown)
          {
            mTrialDialogShown = true;
#ifdef _WIN32
            int ret = MessageBoxA(NULL,
              "This is the trial version of CodecSim.\n"
              "Only MP3 codec is available.\n\n"
              "To unlock all codecs, please purchase the full version.\n\n"
              "Open the store page?",
              "CodecSim Trial", MB_YESNO | MB_ICONINFORMATION);
            if (ret == IDYES)
              ShellExecuteA(NULL, "open", "https://mousesoft.booth.pm/", NULL, NULL, SW_SHOWNORMAL);
#endif
          }
          break;
        }
#endif
        mCurrentCodecIndex = newCodecIndex;
        const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
        if (info)
          AddLogMessage("Codec: " + info->displayName + ". Press Apply.");
        // Defer UpdateBitrateForCodec/UpdateOptionsForCodec to OnIdle (UI thread).
        mPendingCodecUpdate.store(true);
      }
    }
    break;
    case kParamBitrate:
    {
      int presetIdx = GetParam(kParamBitrate)->Int();
      double normVal = GetParam(kParamBitrate)->GetNormalized();
      int numPresets = static_cast<int>(mCurrentBitratePresets.size());
      int nDisplayTexts = GetParam(kParamBitrate)->NDisplayTexts();
      if (mCurrentCodecHasOther && presetIdx >= numPresets)
        AddLogMessage("Bitrate: Other (custom). Press Apply.");
      else if (presetIdx < numPresets)
        AddLogMessage("Bitrate: " + std::to_string(mCurrentBitratePresets[presetIdx]) + " kbps. Press Apply.");
    }
    break;
    case kParamBitrateCustom:
    {
      int customBitrate = GetParam(kParamBitrateCustom)->Int();
      AddLogMessage("Custom bitrate: " + std::to_string(customBitrate) + " kbps. Press Apply.");
    }
    break;
    case kParamSampleRate:
    {
      int sampleRateIndex = GetParam(kParamSampleRate)->Int();
      if (sampleRateIndex >= 0 && sampleRateIndex < kNumSampleRatePresets)
        mSampleRate = kSampleRatePresets[sampleRateIndex];
      else
        mSampleRate = 48000;
      AddLogMessage("Sample rate: " + std::to_string(mSampleRate) + " Hz");
    }
    break;
    case kParamEnabled:
      // No longer used - codec is always active. Apply button handles re-init.
      break;
    default:
      break;
  }

  // Mark pending changes for codec/bitrate/samplerate
  if (paramIdx == kParamCodec || paramIdx == kParamBitrate ||
      paramIdx == kParamBitrateCustom || paramIdx == kParamSampleRate)
  {
    mPendingApply.store(true);
  }

  // Save state on parameter change (standalone persistence)
  if (paramIdx != kParamEnabled && mConstructed)
    SaveStandaloneState();
}

void CodecSim::OnIdle()
{
  // Cache GetUI() once - it becomes nullptr when editor is closed/being recreated
  IGraphics* pUI = GetUI();
  if (!pUI)
  {
    mLastApplyButtonState = -1; // Reset tracking when editor closes
    return;
  }

  // Handle deferred codec update (from OnParamChange on host thread)
  if (mPendingCodecUpdate.exchange(false))
  {
    UpdateBitrateForCodec(mCurrentCodecIndex);
    UpdateOptionsForCodec(mCurrentCodecIndex);
    mLastBitrateDisplayStr.clear(); // Force display text refresh
  }

  // Update Apply button appearance based on pending changes (only when state changes)
  {
    bool pending = mPendingApply.load();
    bool initializing = mInitializing.load();
    int desiredState = (pending && !initializing) ? 1 : (pending ? -1 : 0);

    if (desiredState != -1 && desiredState != mLastApplyButtonState)
    {
      if (IControl* pApplyBtn = pUI->GetControlWithTag(kCtrlTagApplyButton))
      {
        auto* pBtn = dynamic_cast<IVButtonControl*>(pApplyBtn);
        if (pBtn)
        {
          if (desiredState == 1)
          {
            // Orange: unapplied changes exist
            const IVStyle pendingStyle = IVStyle({
              IColor(255, 180, 120, 30),
              IColor(255, 210, 150, 50),
              IColor(255, 160, 100, 20),
              IColor(255, 220, 160, 60),
              IColor(255, 200, 140, 40),
              IColor(0, 0, 0, 0),
              Colors::TextWhite, Colors::TextWhite, Colors::TextWhite
            }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
              .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
              .WithShowLabel(true).WithDrawFrame(true).WithDrawShadows(false).WithRoundness(4.f);
            pBtn->SetStyle(pendingStyle);
            pBtn->SetLabelStr("Apply *");
          }
          else
          {
            // Green: all changes applied
            const IVStyle appliedStyle = IVStyle({
              IColor(255, 30, 100, 60),
              IColor(255, 50, 140, 80),
              IColor(255, 40, 120, 70),
              IColor(255, 60, 160, 90),
              IColor(255, 50, 140, 80),
              IColor(0, 0, 0, 0),
              Colors::TextWhite, Colors::TextWhite, Colors::TextWhite
            }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
              .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
              .WithShowLabel(true).WithDrawFrame(true).WithDrawShadows(false).WithRoundness(4.f);
            pBtn->SetStyle(appliedStyle);
            pBtn->SetLabelStr("Apply");
          }
          pApplyBtn->SetDirty(false);
          mLastApplyButtonState = desiredState;
        }
      }
    }
  }

  // Show/hide loading spinner during initialization
  if (IControl* pSpinner = pUI->GetControlWithTag(kCtrlTagSpinner))
  {
    auto* pSpinnerCtrl = dynamic_cast<SpinnerOverlayControl*>(pSpinner);
    if (pSpinnerCtrl)
    {
      bool initializing = mInitializing.load();
      if (initializing && pSpinner->IsHidden())
        pSpinnerCtrl->StartSpinning();
      else if (!initializing && !pSpinner->IsHidden())
        pSpinnerCtrl->StopSpinning();
    }
  }

  // Handle detail panel tab switching
  if (IControl* pTabSwitch = pUI->GetControlWithTag(kCtrlTagDetailTabSwitch))
  {
    int tabIdx = static_cast<int>(pTabSwitch->GetValue() * 1.0 + 0.5); // 0=Options, 1=Log
    if (tabIdx != mDetailTabIndex)
      SetDetailTab(tabIdx);
  }

  // Hide/show bitrate controls based on codec type
  bool hideBitrate = mCurrentCodecIsLossless;
  if (IControl* pBitrateLabel = pUI->GetControlWithTag(kCtrlTagBitrateLabel))
    pBitrateLabel->Hide(hideBitrate);
  if (IControl* pBitrateCtrl = pUI->GetControlWithTag(kCtrlTagBitrateSelector))
  {
    pBitrateCtrl->Hide(hideBitrate);

    // Force-sync the IVMenuButtonControl's displayed text with the parameter value.
    // Only update when the display text actually changes (avoid redundant SetValueStr
    // which can cause visual oscillation or host feedback loops).
    if (!hideBitrate)
    {
      IParam* pBitrateParam = GetParam(kParamBitrate);
      int bitrateIdx = pBitrateParam->Int();
      int maxIdx = pBitrateParam->NDisplayTexts();
      double paramNorm = pBitrateParam->GetNormalized();
      double ctrlValue = pBitrateCtrl->GetValue();

      // Bounds check: ensure the parameter index is within the enum range
      if (bitrateIdx >= 0 && bitrateIdx < maxIdx)
      {
        WDL_String str;
        pBitrateParam->GetDisplay(str);
        const char* newText = str.Get();

        // Only update the button text if it actually changed
        if (mLastBitrateDisplayStr != newText)
        {
          mLastBitrateDisplayStr = newText;
          if (auto* pContainer = dynamic_cast<IContainerBase*>(pBitrateCtrl))
          {
            if (pContainer->NChildren() > 0)
            {
              if (auto* pChildBtn = dynamic_cast<IVectorBase*>(pContainer->GetChild(0)))
              {
                pChildBtn->SetValueStr(newText);
              }
            }
          }
        }
      }
      else
      {
        // Out of bounds - skip display update
      }
    }
  }

  // Show/hide custom bitrate input
  if (IControl* pCustomBitrate = pUI->GetControlWithTag(kCtrlTagBitrateCustom))
  {
    int numPresets = static_cast<int>(mCurrentBitratePresets.size());
    bool isOther = mCurrentCodecHasOther && (GetParam(kParamBitrate)->Int() >= numPresets);
    pCustomBitrate->Hide(hideBitrate || !isOther);
  }

  if (IControl* pLogCtrl = pUI->GetControlWithTag(kCtrlTagLogDisplay))
  {
    std::string logText;
    {
      std::lock_guard<std::mutex> lock(mLogMutex);
      for (const auto& line : mLogMessages)
        logText += line + "\n";
    }
    if (!logText.empty())
    {
      auto* pMultiLine = dynamic_cast<IMultiLineTextControl*>(pLogCtrl);
      if (pMultiLine)
      {
        pMultiLine->SetStr(logText.c_str());
        pLogCtrl->SetDirty(false);
      }
    }
  }
}

void CodecSim::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nOutChans = NOutChansConnected();
  const int nInChans = NInChansConnected();

  // Early diagnostic logging (first 50 calls)
  static int sPBCount = 0;
  const bool earlyLog = (sPBCount < 50);
  if (earlyLog)
  {
    sPBCount++;
    DebugLogCodecSim("PB#" + std::to_string(sPBCount) +
                     " nF=" + std::to_string(nFrames) +
                     " nIn=" + std::to_string(nInChans) +
                     " nOut=" + std::to_string(nOutChans) +
                     " proc=" + std::to_string(mCodecProcessor ? (mCodecProcessor->IsInitialized() ? 1 : 0) : -1) +
                     " deque=" + std::to_string(mDecodedBuffer.size()));
  }

  // Safety: clear all output channels first
  for (int c = 0; c < nOutChans; c++)
    for (int s = 0; s < nFrames; s++)
      outputs[c][s] = 0.0;

  std::unique_lock<std::recursive_mutex> lock(mCodecMutex, std::try_to_lock);

  if (!lock.owns_lock())
  {
    if (earlyLog) DebugLogCodecSim("  SKIP: lock failed");
    return;
  }
  if (!mCodecProcessor)
  {
    if (earlyLog) DebugLogCodecSim("  SKIP: no processor");
    return;
  }
  if (!mCodecProcessor->IsInitialized())
  {
    if (earlyLog) DebugLogCodecSim("  SKIP: not initialized");
    return;
  }

  // Clamp nFrames to buffer capacity
  const int maxFrames = 8192;
  const int framesToProcess = (nFrames <= maxFrames) ? nFrames : maxFrames;

  float* inBuf = mInterleavedInput.data();
  float* outBuf = mInterleavedOutput.data();

  // Interleave input (handle mono or stereo)
  for (int s = 0; s < framesToProcess; s++)
  {
    inBuf[s * 2]     = (nInChans > 0) ? static_cast<float>(inputs[0][s]) : 0.f;
    inBuf[s * 2 + 1] = (nInChans > 1) ? static_cast<float>(inputs[1][s]) : inBuf[s * 2];
  }

  // Write input to codec and drain all available decoded samples
  int decodedFrames = mCodecProcessor->Process(inBuf, framesToProcess, outBuf, maxFrames);

  // Accumulate decoded samples into buffer (absorbs bursty pipeline)
  for (int i = 0; i < decodedFrames * 2; i++)
    mDecodedBuffer.push_back(outBuf[i]);

  // Output from accumulation buffer (partial output: output whatever is available)
  const size_t availablePairs = mDecodedBuffer.size() / 2;
  const size_t framesToOutput = std::min(availablePairs, static_cast<size_t>(framesToProcess));

  for (size_t s = 0; s < framesToOutput; s++)
  {
    float L = mDecodedBuffer.front(); mDecodedBuffer.pop_front();
    float R = mDecodedBuffer.front(); mDecodedBuffer.pop_front();

    if (nOutChans > 0) outputs[0][s] = static_cast<sample>(L);
    if (nOutChans > 1) outputs[1][s] = static_cast<sample>(R);
  }
  // Remaining samples (framesToOutput..framesToProcess) stay zeroed from the initial clear

  // Early diagnostic: log actual output values
  if (earlyLog && framesToOutput > 0 && nOutChans > 0)
  {
    DebugLogCodecSim("  OUT: frames=" + std::to_string(framesToOutput) +
                     " L[0]=" + std::to_string(outputs[0][0]) +
                     (nOutChans > 1 ? " R[0]=" + std::to_string(outputs[1][0]) : ""));
  }

  // Debug: periodic logging of buffer state (every ~1 second at 48kHz/64 block size)
  static int dbgCounter = 0;
  if (++dbgCounter >= 750)
  {
    dbgCounter = 0;
    DebugLogCodecSim("ProcessBlock: nFrames=" + std::to_string(nFrames) +
                     " decoded=" + std::to_string(decodedFrames) +
                     " bufSize=" + std::to_string(mDecodedBuffer.size() / 2) +
                     " output=" + std::to_string(framesToOutput));
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

  mDecodedBuffer.clear();

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
    if (latency != mLatencySamples.load())
    {
      SetLatency(latency);
      mLatencySamples.store(latency);
    }

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

  mDecodedBuffer.clear();

  AddLogMessage("Codec stopped.");
}

void CodecSim::ApplyCodecSettings()
{
  mPendingApply.store(false);
  DebugLogCodecSim("ApplyCodecSettings called");

  // Cancel previous init wait and join thread quickly
  mCancelInit.store(true);
  if (mInitThread.joinable())
    mInitThread.join();
  mCancelInit.store(false);

  mInitializing.store(true);

  // Start spinner immediately
  if (GetUI())
  {
    if (IControl* pSpinner = GetUI()->GetControlWithTag(kCtrlTagSpinner))
      dynamic_cast<SpinnerOverlayControl*>(pSpinner)->StartSpinning();
  }

  mCurrentCodecIndex = GetParam(kParamCodec)->Int();

#ifdef CODECSIM_TRIAL
  {
    const CodecInfo* checkInfo = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
    if (checkInfo && checkInfo->id != "mp3")
    {
      int mp3Index = CodecRegistry::Instance().GetAvailableIndexById("mp3");
      if (mp3Index >= 0)
      {
        mCurrentCodecIndex = mp3Index;
        GetParam(kParamCodec)->Set(mp3Index);
        SendParameterValueFromDelegate(kParamCodec,
          GetParam(kParamCodec)->ToNormalized(static_cast<double>(mp3Index)), false);
        UpdateBitrateForCodec(mp3Index);
      }
    }
  }
#endif

  AddLogMessage("Applying codec settings...");

  mInitThread = std::thread([this, codecIdx = mCurrentCodecIndex]() {
    InitializeCodec(codecIdx);
    // Wait for first decoded audio output (cancellable)
    auto start = std::chrono::steady_clock::now();
    while (!mCancelInit.load())
    {
      {
        std::lock_guard<std::recursive_mutex> lock(mCodecMutex);
        if (mCodecProcessor && mCodecProcessor->HasFirstAudioArrived())
          break;
      }
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > std::chrono::seconds(5)) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    mInitializing.store(false);
  });
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

  // Notify the host of the new default bitrate value.
  // We use SendParameterValueFromDelegate to properly update the host's cached value.
  // Without this, the host would keep resending its old cached bitrate value.
  // The infinite loop is prevented by the guard in OnParamChange(kParamCodec)
  // which checks `newCodecIndex != mCurrentCodecIndex`.
  {
    double normVal = pBitrate->ToNormalized(static_cast<double>(defaultIdx));
    SendParameterValueFromDelegate(kParamBitrate, normVal, false);
  }
}

void CodecSim::SetDetailTab(int tabIndex)
{
  mDetailTabIndex = tabIndex;
  IGraphics* pUI = GetUI();
  if (!pUI) return;

  bool showOptions = (tabIndex == 0);
  bool showLog = (tabIndex == 1);

  // Get current codec options count
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
  int numOptions = info ? static_cast<int>(info->options.size()) : 0;

  // Show/hide option labels and controls
  for (int i = 0; i < 5; i++)
  {
    bool showSlot = showOptions && (i < numOptions);
    if (IControl* p = pUI->GetControlWithTag(kCtrlTagOptionLabel0 + i * 2))
      p->Hide(!showSlot);
    if (IControl* p = pUI->GetControlWithTag(kCtrlTagOptionControl0 + i * 2))
      p->Hide(!showSlot);
  }

  // Show "no options" text if Options tab and zero options
  if (IControl* p = pUI->GetControlWithTag(kCtrlTagNoOptionsText))
    p->Hide(!showOptions || numOptions > 0);

  // Show/hide log
  if (IControl* p = pUI->GetControlWithTag(kCtrlTagLogDisplay))
    p->Hide(!showLog);
}

void CodecSim::UpdateOptionsForCodec(int codecIndex)
{
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(codecIndex);
  if (!info) return;

  // Set defaults only for keys not already present (preserves loaded/saved values)
  std::map<std::string, int> newValues;
  for (const auto& opt : info->options)
  {
    auto it = mCodecOptionValues.find(opt.key);
    newValues[opt.key] = (it != mCodecOptionValues.end()) ? it->second : opt.defaultValue;
  }
  mCodecOptionValues = newValues;

  IGraphics* pGraphics = GetUI();
  if (!pGraphics) return;

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
      {
        if (auto* pText = dynamic_cast<ITextControl*>(pLabel))
          pText->SetStr(opt.label.c_str());
      }

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
              if (auto* pSwitch = dynamic_cast<ISwitchControlBase*>(pCaller))
                mCodecOptionValues[key] = pSwitch->GetSelectedIdx();
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
                    if (auto* pVec = dynamic_cast<IVectorBase*>(pCaller))
                      pVec->SetLabelStr(pMenu->GetChosenItem()->GetText());
                    pCaller->SetDirty(false);
                  }
                });

                if (IGraphics* pCallerUI = pCaller->GetUI())
                  pCallerUI->CreatePopupMenu(*pCaller, menu, pCaller->GetRECT());
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
            if (auto* pNB = dynamic_cast<IVNumberBoxControl*>(pCaller))
            {
              // Map normalized value back to integer range
              int val = static_cast<int>(pNB->GetRealValue());
              mCodecOptionValues[key] = val;
            }
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

  // Ensure spinner overlay stays on top (AttachControl appends to end of draw list)
  if (IControl* pOldSpinner = pGraphics->GetControlWithTag(kCtrlTagSpinner))
    pGraphics->RemoveControl(pOldSpinner);
  pGraphics->AttachControl(
    new SpinnerOverlayControl(pGraphics->GetBounds(), IColor(120, 0, 0, 0), Colors::AccentBlue, 28.f, 4.f),
    kCtrlTagSpinner
  );

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

//==============================================================================
// State Serialization
//==============================================================================

static constexpr int kStateMagic = 0x43534D31; // 'CSM1'
static constexpr int kStateVersion = 1;

bool CodecSim::SerializeState(IByteChunk& chunk) const
{
  DebugLogCodecSim("SerializeState");

  // 1. Serialize all parameters first (standard iPlug2 pattern)
  if (!SerializeParams(chunk))
    return false;

  // 2. Append custom data after params with magic marker
  int magic = kStateMagic;
  int version = kStateVersion;
  chunk.Put(&magic);
  chunk.Put(&version);

  // Codec ID (for robust codec identification across different machines)
  const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
  std::string codecId = info ? info->id : "mp3";
  chunk.PutStr(codecId.c_str());

  // Semantic values (not param indices, for robustness against dynamic param changes)
  int bitrateKbps = const_cast<CodecSim*>(this)->GetEffectiveBitrate();
  chunk.Put(&bitrateKbps);

  int sampleRateIdx = GetParam(kParamSampleRate)->Int();
  int sampleRateHz = (sampleRateIdx >= 0 && sampleRateIdx < kNumSampleRatePresets)
    ? kSampleRatePresets[sampleRateIdx] : 48000;
  chunk.Put(&sampleRateHz);

  // Codec option values
  int numOptions = static_cast<int>(mCodecOptionValues.size());
  chunk.Put(&numOptions);
  for (const auto& kv : mCodecOptionValues)
  {
    chunk.PutStr(kv.first.c_str());
    int val = kv.second;
    chunk.Put(&val);
  }

  // UI state
  chunk.Put(&mDetailTabIndex);

  return true;
}

int CodecSim::UnserializeState(const IByteChunk& chunk, int startPos)
{
  DebugLogCodecSim("UnserializeState");

  // 1. Unserialize parameters
  int pos = UnserializeParams(chunk, startPos);
  if (pos < 0) return pos;

  // 2. Check for custom data (magic marker)
  if (pos + static_cast<int>(sizeof(int)) > chunk.Size())
  {
    DebugLogCodecSim("UnserializeState: no custom data (legacy/preset-only chunk)");
    // Legacy chunk (e.g., MakePreset param-only) - use defaults
    mCurrentCodecIndex = GetParam(kParamCodec)->Int();
    return pos;
  }

  int magic = 0;
  int tempPos = chunk.Get(&magic, pos);
  if (tempPos < 0 || magic != kStateMagic)
  {
    DebugLogCodecSim("UnserializeState: magic mismatch, using param values only");
    mCurrentCodecIndex = GetParam(kParamCodec)->Int();
    return pos;
  }
  pos = tempPos;

  int version = 0;
  pos = chunk.Get(&version, pos);
  if (pos < 0) return pos;

  DebugLogCodecSim("UnserializeState: version=" + std::to_string(version));

  // Read codec ID and find correct index
  WDL_String codecIdStr;
  pos = chunk.GetStr(codecIdStr, pos);
  if (pos < 0) return pos;

  std::string codecId = codecIdStr.Get();
  DebugLogCodecSim("UnserializeState: codecId=" + codecId);

  // Find codec by ID in available list
  auto available = CodecRegistry::Instance().GetAvailable();
  int codecIndex = 0;
  for (int i = 0; i < static_cast<int>(available.size()); i++)
  {
    if (available[i]->id == codecId) { codecIndex = i; break; }
  }
  mCurrentCodecIndex = codecIndex;

#ifdef CODECSIM_TRIAL
  {
    const CodecInfo* restoredInfo = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
    if (restoredInfo && restoredInfo->id != "mp3")
    {
      int mp3Index = CodecRegistry::Instance().GetAvailableIndexById("mp3");
      mCurrentCodecIndex = (mp3Index >= 0) ? mp3Index : 0;
    }
  }
#endif

  GetParam(kParamCodec)->Set(mCurrentCodecIndex);

  // Read semantic bitrate
  int bitrateKbps = 128;
  pos = chunk.Get(&bitrateKbps, pos);
  if (pos < 0) return pos;

  // Rebuild bitrate presets for this codec and find matching index
  UpdateBitrateForCodec(codecIndex);
  int bitrateIdx = 0;
  for (int i = 0; i < static_cast<int>(mCurrentBitratePresets.size()); i++)
  {
    if (mCurrentBitratePresets[i] == bitrateKbps) { bitrateIdx = i; break; }
  }
  // If no exact match and "Other" is available, use custom
  if (bitrateIdx == 0 && !mCurrentBitratePresets.empty() && mCurrentBitratePresets[0] != bitrateKbps && mCurrentCodecHasOther)
  {
    bitrateIdx = static_cast<int>(mCurrentBitratePresets.size()); // "Other" index
    GetParam(kParamBitrateCustom)->Set(bitrateKbps);
  }
  GetParam(kParamBitrate)->Set(bitrateIdx);

  // Read semantic sample rate
  int sampleRateHz = 48000;
  pos = chunk.Get(&sampleRateHz, pos);
  if (pos < 0) return pos;

  // Find matching sample rate index
  int sampleRateIdx = 5; // default to 48kHz
  for (int i = 0; i < kNumSampleRatePresets; i++)
  {
    if (kSampleRatePresets[i] == sampleRateHz) { sampleRateIdx = i; break; }
  }
  GetParam(kParamSampleRate)->Set(sampleRateIdx);
  mSampleRate = sampleRateHz;

  // Read codec option values
  int numOptions = 0;
  pos = chunk.Get(&numOptions, pos);
  if (pos < 0) return pos;

  mCodecOptionValues.clear();
  for (int i = 0; i < numOptions; i++)
  {
    WDL_String keyStr;
    pos = chunk.GetStr(keyStr, pos);
    if (pos < 0) return pos;
    int val = 0;
    pos = chunk.Get(&val, pos);
    if (pos < 0) return pos;
    mCodecOptionValues[keyStr.Get()] = val;
  }

  // Read UI state
  if (pos + static_cast<int>(sizeof(int)) <= chunk.Size())
  {
    pos = chunk.Get(&mDetailTabIndex, pos);
  }

  // Keep enabled (codec is always active)
  GetParam(kParamEnabled)->Set(1);

  DebugLogCodecSim("UnserializeState: restored codec=" + codecId +
                   " bitrate=" + std::to_string(bitrateKbps) +
                   " sampleRate=" + std::to_string(sampleRateHz));
  return pos;
}

void CodecSim::OnRestoreState()
{
  DebugLogCodecSim("OnRestoreState");

  // Update UI controls to reflect restored state
  SendCurrentParamValuesFromDelegate();

  if (GetUI())
  {
    // Rebuild options UI for the restored codec
    UpdateOptionsForCodec(mCurrentCodecIndex);

    // Refresh bitrate selector display
    if (IControl* pCtrl = GetUI()->GetControlWithTag(kCtrlTagBitrateSelector))
    {
      double normVal = GetParam(kParamBitrate)->ToNormalized(static_cast<double>(GetParam(kParamBitrate)->Int()));
      pCtrl->SetValue(1.0 - normVal, 0);
      pCtrl->SetValueFromUserInput(normVal, 0);
    }

    // Refresh tab state
    SetDetailTab(mDetailTabIndex);
  }
}

//==============================================================================
// Standalone State Persistence (file-based, using IByteChunk)
//==============================================================================

std::string CodecSim::GetAppDataPath()
{
#ifdef _WIN32
  const char* appdata = getenv("APPDATA");
  if (appdata)
  {
#ifdef CODECSIM_TRIAL
    return std::string(appdata) + "\\CodecSimTrial\\";
#else
    return std::string(appdata) + "\\CodecSim\\";
#endif
  }
  return ".\\";
#else
  const char* home = getenv("HOME");
  if (home)
  {
#ifdef CODECSIM_TRIAL
    return std::string(home) + "/Library/Application Support/CodecSimTrial/";
#else
    return std::string(home) + "/Library/Application Support/CodecSim/";
#endif
  }
  return "./";
#endif
}

void CodecSim::SaveStandaloneState()
{
  std::string dir = GetAppDataPath();
  std::string path = dir + "state.dat";
  DebugLogCodecSim("SaveStandaloneState to " + path);

  // Ensure directory exists
#ifdef _WIN32
  CreateDirectoryA(dir.c_str(), NULL);
#else
  // mkdir -p equivalent
  system(("mkdir -p \"" + dir + "\"").c_str());
#endif

  IByteChunk chunk;
  if (!SerializeState(chunk))
  {
    DebugLogCodecSim("SaveStandaloneState: SerializeState failed");
    return;
  }

  FILE* f = fopen(path.c_str(), "wb");
  if (f)
  {
    fwrite(chunk.GetData(), 1, chunk.Size(), f);
    fclose(f);
    DebugLogCodecSim("SaveStandaloneState: wrote " + std::to_string(chunk.Size()) + " bytes");
  }
  else
  {
    DebugLogCodecSim("SaveStandaloneState: failed to open file");
  }
}

void CodecSim::LoadStandaloneState()
{
  std::string path = GetAppDataPath() + "state.dat";
  DebugLogCodecSim("LoadStandaloneState from " + path);

  FILE* f = fopen(path.c_str(), "rb");
  if (!f)
  {
    DebugLogCodecSim("LoadStandaloneState: no saved state file");
    return;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0)
  {
    fclose(f);
    return;
  }

  IByteChunk chunk;
  chunk.Resize(static_cast<int>(size));
  fread(chunk.GetData(), 1, size, f);
  fclose(f);

  DebugLogCodecSim("LoadStandaloneState: read " + std::to_string(size) + " bytes");

  int result = UnserializeState(chunk, 0);
  if (result < 0)
    DebugLogCodecSim("LoadStandaloneState: UnserializeState failed");
  else
    DebugLogCodecSim("LoadStandaloneState: restored successfully");
}

//==============================================================================
// User Preset Management (file-based)
//==============================================================================

std::string CodecSim::GetPresetsDir()
{
  return GetAppDataPath() + "presets"
#ifdef _WIN32
    + "\\";
#else
    + "/";
#endif
}

std::vector<std::string> CodecSim::GetUserPresetList()
{
  std::vector<std::string> result;
  std::string dir = GetPresetsDir();

#ifdef _WIN32
  WIN32_FIND_DATAA fd;
  std::string pattern = dir + "*.preset";
  HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
  if (hFind != INVALID_HANDLE_VALUE)
  {
    do
    {
      std::string fname = fd.cFileName;
      // Remove .preset extension
      if (fname.size() > 7)
        fname = fname.substr(0, fname.size() - 7);
      result.push_back(fname);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
  }
  std::sort(result.begin(), result.end());
#endif

  return result;
}

void CodecSim::SaveUserPreset(const std::string& name)
{
  std::string dir = GetPresetsDir();
  DebugLogCodecSim("SaveUserPreset: " + name);

#ifdef _WIN32
  CreateDirectoryA(GetAppDataPath().c_str(), NULL);
  CreateDirectoryA(dir.c_str(), NULL);
#endif

  IByteChunk chunk;
  if (!SerializeState(chunk))
  {
    DebugLogCodecSim("SaveUserPreset: SerializeState failed");
    return;
  }

  std::string path = dir + name + ".preset";
  FILE* f = fopen(path.c_str(), "wb");
  if (f)
  {
    fwrite(chunk.GetData(), 1, chunk.Size(), f);
    fclose(f);
    DebugLogCodecSim("SaveUserPreset: saved " + std::to_string(chunk.Size()) + " bytes to " + path);
  }
}

void CodecSim::LoadUserPreset(const std::string& name)
{
  std::string path = GetPresetsDir() + name + ".preset";
  DebugLogCodecSim("LoadUserPreset: " + path);

  FILE* f = fopen(path.c_str(), "rb");
  if (!f)
  {
    DebugLogCodecSim("LoadUserPreset: file not found");
    return;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) { fclose(f); return; }

  IByteChunk chunk;
  chunk.Resize(static_cast<int>(size));
  fread(chunk.GetData(), 1, size, f);
  fclose(f);

  int result = UnserializeState(chunk, 0);
  if (result >= 0)
  {
    DebugLogCodecSim("LoadUserPreset: restored successfully");
    // Update UI
    if (GetUI())
    {
      SendCurrentParamValuesFromDelegate();
      UpdateBitrateForCodec(mCurrentCodecIndex);
      UpdateOptionsForCodec(mCurrentCodecIndex);
      if (IControl* pBR = GetUI()->GetControlWithTag(kCtrlTagBitrateSelector))
      {
        double nv = GetParam(kParamBitrate)->ToNormalized(static_cast<double>(GetParam(kParamBitrate)->Int()));
        pBR->SetValue(1.0 - nv, 0);
        pBR->SetValueFromUserInput(nv, 0);
      }
      SetDetailTab(mDetailTabIndex);
    }
    SaveStandaloneState();
    AddLogMessage("Loaded preset: " + name);
  }
}

void CodecSim::DeleteUserPreset(const std::string& name)
{
  std::string path = GetPresetsDir() + name + ".preset";
  DebugLogCodecSim("DeleteUserPreset: " + path);
#ifdef _WIN32
  DeleteFileA(path.c_str());
#else
  remove(path.c_str());
#endif
}
