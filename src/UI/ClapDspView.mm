//
//  ClapDspView.mm
//  foo_dsp_clap
//
//  The DSP configuration popup (Cocoa). Presented by foobar2000 via the DSP
//  manager's "Configure" button. Lets the user:
//    - pick a CLAP audio-effect plugin (dropdown from ClapScanner, + Rescan),
//    - browse the plugin's presets (headless preset-discovery, + load),
//    - open the plugin's own native GUI in a window (embedded Cocoa), or fall
//      back to auto-generated parameter sliders when the plugin has no GUI.
//
//  All edits are captured as the plugin's full clap.state and pushed back to the
//  host via dsp_preset_edit_callback_v2::set_preset, so they persist and the
//  audio-chain DSP picks them up.
//
//  Modeled on the SDK's foo_sample/Mac/fooSampleDSPView.mm, built programmatically
//  (no XIB).
//

#import <Cocoa/Cocoa.h>

#include "../fb2k_sdk.h"
#include "../Core/ClapDspConfig.h"
#include "../Core/ClapConfig.h"
#include "../Clap/ClapEffectHost.h"
#include "../Clap/ClapScanner.h"
#include "../Clap/ClapPresets.h"

#include <memory>
#include <vector>

using namespace foo_clap_dsp;

@interface ClapDspView : NSViewController <NSWindowDelegate> {
    std::unique_ptr<ClapEffectHost> _host;
    ClapDspSettings _settings;
    std::vector<uint8_t> _lastPushedState;
    std::vector<ClapPluginEntry> _effects;
    std::vector<ClapPreset> _presets;
    std::vector<clap_id> _paramIds;
}
@property (nonatomic) dsp_preset_edit_callback_v2::ptr callback;
@property (nonatomic, strong) NSPopUpButton* pluginPopup;
@property (nonatomic, strong) NSPopUpButton* presetPopup;
@property (nonatomic, strong) NSTextField* statusLabel;
@property (nonatomic, strong) NSWindow* pluginWindow;    // native plugin GUI
@property (nonatomic, strong) NSWindow* paramsWindow;    // generic slider fallback
@property (nonatomic, strong) NSTimer* pollTimer;
@end

@implementation ClapDspView

- (void)loadView {
    // Built with Auto Layout and pinned to the top-leading corner so the controls
    // stay visible regardless of how the DSP-manager host sizes the panel (a fixed
    // absolute layout landed the top controls off-screen on some hosts).
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 440)];

    self.pluginPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect];
    self.pluginPopup.target = self;
    self.pluginPopup.action = @selector(onPluginChanged:);
    NSButton* rescan = [self buttonWithTitle:@"Rescan" action:@selector(onRescan:)];
    NSStackView* row1 = [self rowWithLabel:@"Plugin:" field:self.pluginPopup trailing:rescan];

    self.presetPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect];
    self.presetPopup.target = self;
    self.presetPopup.action = @selector(onPresetChanged:);
    NSButton* loadList = [self buttonWithTitle:@"Load list" action:@selector(onLoadPresets:)];
    NSStackView* row2 = [self rowWithLabel:@"Preset:" field:self.presetPopup trailing:loadList];

    NSButton* gui = [self buttonWithTitle:@"Open Plugin GUI" action:@selector(onOpenGui:)];
    gui.controlSize = NSControlSizeRegular;

    self.statusLabel = [self labelWithText:@""];
    self.statusLabel.textColor = [NSColor secondaryLabelColor];
    self.statusLabel.lineBreakMode = NSLineBreakByWordWrapping;
    [self.statusLabel.cell setWraps:YES];

    NSStackView* header = [NSStackView stackViewWithViews:@[ row1, row2, gui, self.statusLabel ]];
    header.orientation = NSUserInterfaceLayoutOrientationVertical;
    header.alignment = NSLayoutAttributeLeading;
    header.spacing = 8;
    header.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:header];

    // Compact dialog: just the picker rows. The plugin's own GUI (or the generic
    // parameter sliders, for plugins without one) opens in a separate window.
    [NSLayoutConstraint activateConstraints:@[
        [header.topAnchor constraintEqualToAnchor:root.topAnchor constant:16],
        [header.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [header.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
        [header.bottomAnchor constraintLessThanOrEqualToAnchor:root.bottomAnchor constant:-16],
        [root.widthAnchor constraintGreaterThanOrEqualToConstant:460],
    ]];

    self.view = root;
    self.preferredContentSize = NSMakeSize(480, 188);
}

// A "[Label] [field...........] [button]" row as a horizontal stack.
- (NSStackView*)rowWithLabel:(NSString*)text field:(NSView*)field trailing:(NSView*)trailing {
    NSTextField* label = [self labelWithText:text];
    [label setContentHuggingPriority:NSLayoutPriorityDefaultHigh
                      forOrientation:NSLayoutConstraintOrientationHorizontal];
    [field setContentHuggingPriority:NSLayoutPriorityDefaultLow
                     forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* row = [NSStackView stackViewWithViews:@[ label, field, trailing ]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.spacing = 8;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    return row;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // Current settings from the host's preset.
    dsp_preset_impl preset;
    _callback->get_preset(preset);
    decodeSettings(preset, _settings);
    _lastPushedState = _settings.state;

    [self rebuildPluginPopup];
    if (_settings.hasPlugin()) {
        [self createHost];
    }
    [self refreshStatus];
}

- (void)viewDidAppear {
    [super viewDidAppear];
    self.pollTimer = [NSTimer scheduledTimerWithTimeInterval:0.4
                                                      target:self
                                                    selector:@selector(pollState)
                                                    userInfo:nil
                                                     repeats:YES];
}

- (void)viewWillDisappear {
    [super viewWillDisappear];
    [self.pollTimer invalidate];
    self.pollTimer = nil;
    [self closePluginWindow];
    [self closeParamsWindow];
    _host.reset();
}

// --- helpers ---------------------------------------------------------------

- (NSTextField*)labelWithText:(NSString*)text {
    NSTextField* l = [NSTextField labelWithString:text ?: @""];
    l.translatesAutoresizingMaskIntoConstraints = NO;
    return l;
}

- (NSButton*)buttonWithTitle:(NSString*)title action:(SEL)action {
    NSButton* b = [[NSButton alloc] initWithFrame:NSZeroRect];
    b.title = title;
    b.bezelStyle = NSBezelStyleRounded;
    b.target = self;
    b.action = action;
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

- (void)rebuildPluginPopup {
    const auto& fx = clapEffects(false);
    _effects.assign(fx.begin(), fx.end());
    [self.pluginPopup removeAllItems];
    [self.pluginPopup addItemWithTitle:@"(none — pass audio through)"];
    NSInteger selected = 0;
    for (size_t i = 0; i < _effects.size(); ++i) {
        NSString* title = [NSString stringWithUTF8String:_effects[i].name.c_str()];
        [self.pluginPopup addItemWithTitle:title ?: @"(unnamed)"];
        if (_effects[i].path == _settings.pluginPath && _effects[i].id == _settings.pluginId)
            selected = (NSInteger)(i + 1);
    }
    [self.pluginPopup selectItemAtIndex:selected];
}

- (void)createHost {
    _host = std::make_unique<ClapEffectHost>();
    if (!_host->load(_settings.pluginPath, _settings.pluginId)) {
        _host.reset();
        return;
    }
    if (!_settings.state.empty())
        _host->loadState(_settings.state.data(), _settings.state.size());
    // Activate so param edits can be flushed (silent processing) for the
    // generic-UI path; the native GUI edits the plugin's params directly.
    _host->activate(48000.0, 512);
}

- (void)pushSettings {
    dsp_preset_impl preset;
    encodeSettings(_settings, preset);
    _callback->set_preset(preset);
}

- (void)captureStateAndPush {
    if (_host && _host->isLoaded()) {
        std::vector<uint8_t> st;
        if (_host->saveState(st)) { _settings.state = st; _lastPushedState = st; }
    }
    [self pushSettings];
}

- (void)pollState {
    if (!_host || !_host->isLoaded()) return;
    std::vector<uint8_t> st;
    if (_host->saveState(st) && st != _lastPushedState) {
        _settings.state = st;
        _lastPushedState = st;
        [self pushSettings];
    }
}

- (void)refreshStatus {
    NSString* msg;
    if (!_settings.hasPlugin()) {
        msg = @"No plugin selected — audio passes through unchanged.";
    } else if (!_host || !_host->isLoaded()) {
        msg = @"Failed to load the selected plugin (see foobar2000 console).";
    } else if (_host->guiSupportsEmbeddedCocoa()) {
        msg = @"Ready. Open Plugin GUI for the plugin's own interface.";
    } else {
        msg = @"This plugin has no embeddable GUI — using parameter sliders.";
    }
    self.statusLabel.stringValue = msg;
}

// --- actions ---------------------------------------------------------------

- (void)onRescan:(id)sender {
    self.statusLabel.stringValue = @"Rescanning… (raises memory until restart)";
    clapEffects(true);
    [self rebuildPluginPopup];
    [self refreshStatus];
}

- (void)onPluginChanged:(id)sender {
    [self closePluginWindow];
    _host.reset();

    NSInteger idx = self.pluginPopup.indexOfSelectedItem;
    _settings.presetName.clear();
    _settings.state.clear();
    _lastPushedState.clear();
    [self.presetPopup removeAllItems];
    _presets.clear();

    if (idx <= 0 || (size_t)(idx - 1) >= _effects.size()) {
        _settings.pluginPath.clear();
        _settings.pluginId.clear();
    } else {
        const ClapPluginEntry& e = _effects[idx - 1];
        _settings.pluginPath = e.path;
        _settings.pluginId = e.id;
        clap_dsp_config::setConfigString(clap_dsp_config::kKeyLastPluginPath, e.path.c_str());
        clap_dsp_config::setConfigString(clap_dsp_config::kKeyLastPluginId, e.id.c_str());
        [self createHost];
    }
    [self captureStateAndPush];
    [self refreshStatus];
}

- (void)onLoadPresets:(id)sender {
    if (!_settings.hasPlugin()) return;
    self.statusLabel.stringValue = @"Loading preset list…";
    ClapPresetList list = discoverClapPresets(_settings.pluginPath, _settings.pluginId);
    _presets = list.presets;
    [self.presetPopup removeAllItems];
    [self.presetPopup addItemWithTitle:@"(current)"];
    for (const auto& p : _presets) {
        NSString* title = [NSString stringWithUTF8String:p.name.c_str()];
        [self.presetPopup addItemWithTitle:title ?: @"(unnamed)"];
    }
    if (!list.supported)
        self.statusLabel.stringValue = @"This plugin exposes no host-readable presets.";
    else
        self.statusLabel.stringValue = [NSString stringWithFormat:@"%lu presets.",
                                        (unsigned long)_presets.size()];
}

- (void)onPresetChanged:(id)sender {
    NSInteger i = self.presetPopup.indexOfSelectedItem;
    if (i <= 0 || (size_t)(i - 1) >= _presets.size() || !_host) return;
    const ClapPreset& p = _presets[i - 1];
    if (_host->loadPreset(p.locationKind, p.location, p.loadKey)) {
        _settings.presetName = p.name;
        [self captureStateAndPush];
    } else {
        self.statusLabel.stringValue = @"Plugin rejected that preset.";
    }
}

- (void)onOpenGui:(id)sender {
    if (!_host || !_host->isLoaded()) return;
    if (_host->guiSupportsEmbeddedCocoa()) {
        [self openNativeGuiWindow];
    } else {
        [self openGenericParamsWindow];
    }
}

// --- native GUI window -----------------------------------------------------

- (void)openNativeGuiWindow {
    if (self.pluginWindow) {
        [self.pluginWindow makeKeyAndOrderFront:nil];
        return;
    }
    NSRect frame = NSMakeRect(0, 0, 640, 400);
    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    self.pluginWindow = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:mask
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    self.pluginWindow.releasedWhenClosed = NO;
    self.pluginWindow.delegate = self;
    self.pluginWindow.title = [NSString stringWithUTF8String:_host->displayName().c_str()] ?: @"CLAP Plugin";

    NSView* container = self.pluginWindow.contentView;
    uint32_t w = 0, h = 0;
    if (!_host->guiCreateEmbedded((__bridge void*)container, &w, &h)) {
        self.statusLabel.stringValue = @"Plugin GUI creation failed.";
        self.pluginWindow = nil;
        return;
    }
    if (w > 0 && h > 0) [self.pluginWindow setContentSize:NSMakeSize(w, h)];

    __weak ClapDspView* weakSelf = self;
    _host->setResizeCallback([weakSelf](uint32_t nw, uint32_t nh) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf.pluginWindow setContentSize:NSMakeSize(nw, nh)];
        });
    });

    [self.pluginWindow center];
    [self.pluginWindow makeKeyAndOrderFront:nil];
}

- (void)closePluginWindow {
    if (!self.pluginWindow) return;
    if (_host) _host->guiDestroy();
    self.pluginWindow.delegate = nil;
    [self.pluginWindow close];
    self.pluginWindow = nil;
}

- (void)windowWillClose:(NSNotification*)notification {
    if (notification.object == self.pluginWindow) {
        if (_host) _host->guiDestroy();
        [self captureStateAndPush];   // persist whatever the GUI left us with
        self.pluginWindow.delegate = nil;
        self.pluginWindow = nil;
    } else if (notification.object == self.paramsWindow) {
        [self captureStateAndPush];
        self.paramsWindow.delegate = nil;
        self.paramsWindow = nil;
    }
}

// --- generic parameter UI (fallback, plugins without an embeddable GUI) -----

- (void)openGenericParamsWindow {
    if (self.paramsWindow) {
        [self.paramsWindow makeKeyAndOrderFront:nil];
        return;
    }
    uint32_t n = _host->paramCount();
    _paramIds.clear();

    const CGFloat rowH = 44, docW = 430;
    NSView* doc = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, docW, MAX(1u, n) * rowH)];
    CGFloat y = (CGFloat)n * rowH - rowH;
    for (uint32_t i = 0; i < n; ++i) {
        ClapParamInfo info;
        if (!_host->paramInfoByIndex(i, info)) continue;
        _paramIds.push_back(info.id);
        NSInteger tag = (NSInteger)(_paramIds.size() - 1);

        NSTextField* name = [NSTextField labelWithString:
                             [NSString stringWithUTF8String:info.name.c_str()] ?: @"?"];
        name.frame = NSMakeRect(6, y + 20, docW - 12, 18);
        [doc addSubview:name];

        NSSlider* slider = [[NSSlider alloc] initWithFrame:NSMakeRect(6, y, docW - 12, 20)];
        slider.minValue = info.minValue;
        slider.maxValue = info.maxValue;
        slider.doubleValue = _host->paramValue(info.id);
        slider.tag = tag;
        slider.target = self;
        slider.action = @selector(onSliderMoved:);
        [doc addSubview:slider];

        y -= rowH;
    }

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, docW + 20, 460)];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSNoBorder;
    scroll.documentView = doc;

    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    self.paramsWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, docW + 20, 460)
                                                    styleMask:mask
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    self.paramsWindow.releasedWhenClosed = NO;
    self.paramsWindow.delegate = self;
    self.paramsWindow.title = [NSString stringWithFormat:@"%s — Parameters",
                               _host->displayName().c_str()];
    self.paramsWindow.contentView = scroll;
    [self.paramsWindow center];
    [self.paramsWindow makeKeyAndOrderFront:nil];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%u parameters.", n];
}

- (void)closeParamsWindow {
    if (!self.paramsWindow) return;
    self.paramsWindow.delegate = nil;
    [self.paramsWindow close];
    self.paramsWindow = nil;
}

- (void)onSliderMoved:(NSSlider*)sender {
    NSInteger tag = sender.tag;
    if (tag < 0 || (size_t)tag >= _paramIds.size() || !_host) return;
    _host->setParamAndFlush(_paramIds[tag], sender.doubleValue);
    // pollState (timer) will capture + push the resulting state.
}

@end

// Entry point called from ClapDsp.cpp (g_show_config_popup).
service_ptr ConfigureClapDsp(fb2k::hwnd_t parent, dsp_preset_edit_callback_v2::ptr callback) {
    (void)parent;
    ClapDspView* view = [ClapDspView new];
    view.callback = callback;
    return fb2k::wrapNSObject(view);
}
