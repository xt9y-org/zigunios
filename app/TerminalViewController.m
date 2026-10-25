#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "shell.h"

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
    self.transcript.text = _ready
        ? @"zigunios 0.1  |  native iPad terminal\nType 'help' for available commands.\nProjects persist in Files > On My iPad > Zigunios > root.\n\n"
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
    if (!_ready) return YES;
    NSString *input = textField.text ?: @"";
    [self append:[NSString stringWithFormat:@"%@ %@\n", self.prompt.text, input]];
    int clear = 0;
    char *result = zu_shell_execute(&_shell, input.UTF8String, &clear);
    if (clear) self.transcript.text = @"";
    if (result) {
        NSString *response = [NSString stringWithUTF8String:result];
        [self append:response ?: @"(non UTF-8 output)\n"];
        free(result);
    }
    textField.text = @"";
    [self updatePrompt];
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
