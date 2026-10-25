#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <MetalKit/MetalKit.h>
#import "shell.h"
#import "git_bridge.h"

// Minimal native source editor, no embedded web view or external app.
@interface ZUEditorController : UIViewController
@property (nonatomic, copy) NSString *path;
@property (nonatomic, strong) UITextView *editor;
@end

@implementation ZUEditorController

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = self.path.lastPathComponent;
    self.view.backgroundColor = [UIColor colorWithRed:0.045 green:0.061 blue:0.082 alpha:1];
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                            target:self action:@selector(closeEditor)];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemSave
                            target:self action:@selector(saveEditor)];
    self.editor = [[UITextView alloc] init];
    self.editor.translatesAutoresizingMaskIntoConstraints = NO;
    self.editor.backgroundColor = UIColor.clearColor;
    self.editor.textColor = UIColor.whiteColor;
    self.editor.tintColor = [UIColor systemGreenColor];
    self.editor.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    self.editor.autocorrectionType = UITextAutocorrectionTypeNo;
    self.editor.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.editor.smartQuotesType = UITextSmartQuotesTypeNo;
    self.editor.smartDashesType = UITextSmartDashesTypeNo;
    self.editor.smartInsertDeleteType = UITextSmartInsertDeleteTypeNo;
    NSError *error = nil;
    NSString *content = [NSString stringWithContentsOfFile:self.path
                                                  encoding:NSUTF8StringEncoding
                                                     error:&error];
    if (error && [[NSFileManager defaultManager] fileExistsAtPath:self.path]) {
        self.editor.editable = NO;
        self.editor.text = [NSString stringWithFormat:@"Unable to open file as UTF-8: %@", error.localizedDescription];
    } else {
        self.editor.text = content ?: @"";
    }
    [self.view addSubview:self.editor];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [self.editor.topAnchor constraintEqualToAnchor:safe.topAnchor constant:8],
        [self.editor.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:12],
        [self.editor.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-12],
        [self.editor.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor constant:-8]
    ]];
}

- (void)closeEditor
{
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)saveEditor
{
    if (!self.editor.editable) return;
    NSError *error = nil;
    if ([self.editor.text writeToFile:self.path
                          atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
        [self dismissViewControllerAnimated:YES completion:nil];
    } else {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Save failed"
                                       message:error.localizedDescription
                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
    }
}
@end

// A native graphics surface inside Zigunios, not a separate app/window manager.
@interface ZUMetalController : UIViewController <MTKViewDelegate>
@property (nonatomic, strong) MTKView *surface;
@property (nonatomic, strong) id<MTLCommandQueue> queue;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipeline;
@end

@implementation ZUMetalController

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Metal";
    self.view.backgroundColor = UIColor.blackColor;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                            target:self action:@selector(closePreview)];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return;
    self.surface = [[MTKView alloc] initWithFrame:CGRectZero device:device];
    self.surface.translatesAutoresizingMaskIntoConstraints = NO;
    self.surface.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.surface.clearColor = MTLClearColorMake(0.03, 0.04, 0.07, 1);
    self.surface.preferredFramesPerSecond = 60;
    self.surface.delegate = self;
    [self.view addSubview:self.surface];
    [NSLayoutConstraint activateConstraints:@[
        [self.surface.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [self.surface.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.surface.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.surface.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor]
    ]];
    self.queue = [device newCommandQueue];
    NSString *source = @"#include <metal_stdlib>\n"
        @"using namespace metal;\n"
        @"struct Out { float4 position [[position]]; float4 color; };\n"
        @"vertex Out vert(uint v [[vertex_id]], constant float &phase [[buffer(0)]]) {\n"
        @"  float2 p[3] = {float2(0.0,0.65), float2(-0.65,-0.5), float2(0.65,-0.5)};\n"
        @"  float c = cos(phase), s = sin(phase);\n"
        @"  float2 q = float2(p[v].x*c - p[v].y*s, p[v].x*s + p[v].y*c);\n"
        @"  Out o; o.position = float4(q,0,1);\n"
        @"  o.color = float4(v==0 ? 1.0 : 0.1, v==1 ? 1.0 : 0.2, v==2 ? 1.0 : 0.4, 1);\n"
        @"  return o;\n"
        @"}\n"
        @"fragment float4 frag(Out in [[stage_in]]) { return in.color; }\n";
    NSError *error = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:source options:nil error:&error];
    if (!lib) { NSLog(@"Metal shader: %@", error); return; }
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [lib newFunctionWithName:@"vert"];
    descriptor.fragmentFunction = [lib newFunctionWithName:@"frag"];
    descriptor.colorAttachments[0].pixelFormat = self.surface.colorPixelFormat;
    self.pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!self.pipeline) NSLog(@"Metal pipeline: %@", error);
}

- (void)closePreview
{
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size { (void)view; (void)size; }

- (void)drawInMTKView:(MTKView *)view
{
    if (!self.pipeline || !self.queue) return;
    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!pass || !drawable) return;
    id<MTLCommandBuffer> commands = [self.queue commandBuffer];
    id<MTLRenderCommandEncoder> render = [commands renderCommandEncoderWithDescriptor:pass];
    float phase = (float)[NSDate date].timeIntervalSinceReferenceDate;
    [render setRenderPipelineState:self.pipeline];
    [render setVertexBytes:&phase length:sizeof(phase) atIndex:0];
    [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [render endEncoding];
    [commands presentDrawable:drawable];
    [commands commit];
}

@end

@interface TerminalViewController : UIViewController <UITextFieldDelegate, UIDocumentPickerDelegate>
@property (nonatomic, strong) UITextView *transcript;
@property (nonatomic, strong) UILabel *prompt;
@property (nonatomic, strong) UITextField *input;
@end

@implementation TerminalViewController {
    ZUShell _shell;
    BOOL _ready;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.045 green:0.061 blue:0.082 alpha:1];
    UIColor *foreground = [UIColor colorWithRed:0.83 green:0.89 blue:0.94 alpha:1];
    UIColor *accent = [UIColor colorWithRed:0.52 green:0.91 blue:0.66 alpha:1];
    UIFont *mono = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];

    UILabel *title = [[UILabel alloc] init];
    title.text = @"zigunios";
    title.textColor = foreground;
    title.font = [UIFont monospacedSystemFontOfSize:19 weight:UIFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;

    UIButton *files = [UIButton buttonWithType:UIButtonTypeSystem];
    [files setTitle:@"Import files" forState:UIControlStateNormal];
    [files setTitleColor:accent forState:UIControlStateNormal];
    files.titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium];
    [files addTarget:self action:@selector(importFiles) forControlEvents:UIControlEventTouchUpInside];
    files.translatesAutoresizingMaskIntoConstraints = NO;

    self.transcript = [[UITextView alloc] init];
    self.transcript.translatesAutoresizingMaskIntoConstraints = NO;
    self.transcript.backgroundColor = UIColor.clearColor;
    self.transcript.textColor = foreground;
    self.transcript.font = mono;
    self.transcript.editable = NO;
    self.transcript.selectable = YES;
    self.transcript.alwaysBounceVertical = YES;
    self.transcript.textContainerInset = UIEdgeInsetsMake(8, 0, 8, 0);

    self.prompt = [[UILabel alloc] init];
    self.prompt.textColor = accent;
    self.prompt.font = mono;
    self.prompt.translatesAutoresizingMaskIntoConstraints = NO;
    [self.prompt setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];

    self.input = [[UITextField alloc] init];
    self.input.translatesAutoresizingMaskIntoConstraints = NO;
    self.input.textColor = foreground;
    self.input.tintColor = accent;
    self.input.font = mono;
    self.input.autocorrectionType = UITextAutocorrectionTypeNo;
    self.input.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.input.spellCheckingType = UITextSpellCheckingTypeNo;
    self.input.keyboardAppearance = UIKeyboardAppearanceDark;
    self.input.returnKeyType = UIReturnKeyGo;
    self.input.delegate = self;
    self.input.placeholder = @"enter a command";
    [self.input setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];

    UIStackView *command = [[UIStackView alloc] initWithArrangedSubviews:@[self.prompt, self.input]];
    command.axis = UILayoutConstraintAxisHorizontal;
    command.spacing = 12;
    command.alignment = UIStackViewAlignmentCenter;
    command.translatesAutoresizingMaskIntoConstraints = NO;

    [self.view addSubview:title];
    [self.view addSubview:files];
    [self.view addSubview:self.transcript];
    [self.view addSubview:command];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:safe.topAnchor constant:12],
        [title.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:18],
        [files.centerYAnchor constraintEqualToAnchor:title.centerYAnchor],
        [files.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-18],
        [self.transcript.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:16],
        [self.transcript.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:18],
        [self.transcript.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-18],
        [self.transcript.bottomAnchor constraintEqualToAnchor:command.topAnchor constant:-8],
        [command.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:18],
        [command.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-18],
        [command.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor constant:-12],
        [command.heightAnchor constraintGreaterThanOrEqualToConstant:32],
        [self.prompt.widthAnchor constraintLessThanOrEqualToConstant:230]
    ]];

    NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                               inDomains:NSUserDomainMask].firstObject;
    NSString *root = [[documents URLByAppendingPathComponent:@"root" isDirectory:YES] path];
    _ready = zu_shell_init(&_shell, root.fileSystemRepresentation);
    if (_ready) zu_shell_set_external(&_shell, zu_git_execute, NULL);
    self.transcript.text = _ready
        ? @"zigunios 0.2  |  workspace + Git\nType 'help' for available commands.\nProjects persist in Files > On My iPad > Zigunios > root.\n\n"
        : @"Failed to create the local workspace.\n";
    [self updatePrompt];
}

- (void)updatePrompt
{
    NSString *cwd = _ready ? [NSString stringWithUTF8String:zu_shell_cwd(&_shell)] : @"/";
    self.prompt.text = [NSString stringWithFormat:@"%@ $", cwd];
}

- (void)append:(NSString *)text
{
    NSString *previous = self.transcript.text ?: @"";
    self.transcript.text = [previous stringByAppendingString:text];
    NSUInteger length = self.transcript.text.length;
    if (length) [self.transcript scrollRangeToVisible:NSMakeRange(length - 1, 1)];
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
    if (!_ready || !self.input.enabled) return NO;
    NSString *command = [textField.text copy] ?: @"";
    if (!command.length) return YES;
    if ([command isEqualToString:@"metal demo"]) {
        [self append:[NSString stringWithFormat:@"%@ metal demo\n", self.prompt.text]];
        textField.text = @"";
        UINavigationController *nav = [[UINavigationController alloc]
            initWithRootViewController:[ZUMetalController new]];
        nav.modalPresentationStyle = UIModalPresentationFullScreen;
        [self presentViewController:nav animated:YES completion:nil];
        return YES;
    }
    if ([command hasPrefix:@"edit "]) {
        NSString *name = [[command substringFromIndex:5]
            stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
        if (!name.length || !zu_shell_resolve_path(&_shell, name.UTF8String, guest, host)) {
            [self append:@"edit: unsafe or missing path\n"];
        } else {
            BOOL isDirectory = NO;
            if ([[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:host]
                                                      isDirectory:&isDirectory] && isDirectory) {
                [self append:@"edit: cannot edit a directory\n"];
            } else {
                ZUEditorController *editor = [ZUEditorController new];
                editor.path = [NSString stringWithUTF8String:host];
                UINavigationController *nav = [[UINavigationController alloc]
                    initWithRootViewController:editor];
                nav.modalPresentationStyle = UIModalPresentationFullScreen;
                [self presentViewController:nav animated:YES completion:nil];
            }
        }
        textField.text = @"";
        return YES;
    }
    [self append:[NSString stringWithFormat:@"%@ %@\n", self.prompt.text, command]];
    textField.text = @"";
    self.input.enabled = NO;

    // Clone/fetch can take time: never run shell work on UIKit's thread.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        int clear = 0;
        char *result = zu_shell_execute(&self->_shell, command.UTF8String, &clear);
        NSString *response = result ? [[NSString alloc] initWithUTF8String:result] : nil;
        free(result);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (clear) self.transcript.text = @"";
            if (response.length) [self append:response];
            else if (!response) [self append:@"(command failed to return UTF-8)\n"];
            [self updatePrompt];
            self.input.enabled = YES;
            [self.input becomeFirstResponder];
        });
    });
    return YES;
}

- (void)importFiles
{
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeItem] asCopy:YES];
    picker.allowsMultipleSelection = YES;
    picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                               inDomains:NSUserDomainMask].firstObject;
    NSURL *destination = [documents URLByAppendingPathComponent:@"root/home/projects" isDirectory:YES];
    NSFileManager *manager = NSFileManager.defaultManager;
    for (NSURL *url in urls) {
        BOOL granted = [url startAccessingSecurityScopedResource];
        NSString *filename = url.lastPathComponent ?: @"imported";
        NSURL *target = [destination URLByAppendingPathComponent:filename];
        NSUInteger suffix = 2;
        while ([manager fileExistsAtPath:target.path]) {
            NSString *name = filename.stringByDeletingPathExtension;
            NSString *ext = filename.pathExtension;
            NSString *unique = [NSString stringWithFormat:@"%@-%lu%@", name,
                (unsigned long)suffix++, ext.length ? [@"." stringByAppendingString:ext] : @""];
            target = [destination URLByAppendingPathComponent:unique];
        }
        NSError *error = nil;
        BOOL copied = [manager copyItemAtURL:url toURL:target error:&error];
        if (granted) [url stopAccessingSecurityScopedResource];
        [self append:copied
            ? [NSString stringWithFormat:@"Imported into /home/projects/%@\n", target.lastPathComponent]
            : [NSString stringWithFormat:@"Import failed: %@\n", error.localizedDescription]];
    }
}

@end
