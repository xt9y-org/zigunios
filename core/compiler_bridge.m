#import <Foundation/Foundation.h>
#include "compiler_bridge.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// This uses the open-source a-Shell iOS Clang and LLD command frameworks.
// The compiler runs as a signed native framework already bundled in the app.
// Produced WebAssembly is interpreted separately by the existing WASM3 runtime.

typedef int (*IOSCommand)(const char *);
typedef NSError *(*AddCommandList)(NSString *);
typedef int (*SetMiniRoot)(NSURL *);
typedef int (*SetAllowedPaths)(NSArray<NSString *> *);

static pthread_mutex_t compiler_lock = PTHREAD_MUTEX_INITIALIZER;
static IOSCommand ios_command = NULL;
static char compiler_error[512];

static char *result(const char *text)
{
    return strdup(text ? text : "cc: failed\n");
}

static NSString *bundle_framework(NSString *name)
{
    NSString *root = [[NSBundle mainBundle] privateFrameworksPath];
    if (!root) root = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Frameworks"];
    return [[root stringByAppendingPathComponent:[name stringByAppendingString:@".framework"]]
        stringByAppendingPathComponent:name];
}

static BOOL prepare_compiler(ZUShell *shell)
{
    if (ios_command) return YES;
    NSString *system = bundle_framework(@"ios_system");
    NSString *clang = bundle_framework(@"clang");
    NSString *linker = bundle_framework(@"lld");
    NSString *llvm = bundle_framework(@"libLLVM");
    NSFileManager *fs = [NSFileManager defaultManager];
    for (NSString *path in @[system, clang, linker, llvm]) {
        if (![fs fileExistsAtPath:path]) {
            snprintf(compiler_error, sizeof(compiler_error),
                     "cc: iPad compiler framework missing: %s\n", path.lastPathComponent.UTF8String);
            return NO;
        }
    }
    // libLLVM must be loaded before Clang/LLD; dyld resolves their dependencies.
    if (!dlopen(llvm.UTF8String, RTLD_NOW | RTLD_GLOBAL) ||
        !dlopen(system.UTF8String, RTLD_NOW | RTLD_GLOBAL)) {
        snprintf(compiler_error, sizeof(compiler_error),
                 "cc: unable to load LLVM/ios_system: %s\n", dlerror() ?: "unknown error");
        return NO;
    }
    ios_command = (IOSCommand)dlsym(RTLD_DEFAULT, "ios_system");
    AddCommandList add = (AddCommandList)dlsym(RTLD_DEFAULT, "addCommandList");
    SetMiniRoot mini = (SetMiniRoot)dlsym(RTLD_DEFAULT, "ios_setMiniRootURL");
    SetAllowedPaths allow = (SetAllowedPaths)dlsym(RTLD_DEFAULT, "ios_setAllowedPaths");
    if (!ios_command || !add || !mini) {
        ios_command = NULL;
        snprintf(compiler_error, sizeof(compiler_error), "cc: iOS command bridge symbols unavailable\n");
        return NO;
    }
    NSDictionary *commands = @{
        @"clang": @[clang, @"main", @"", @"file"],
        @"wasm-ld": @[linker, @"main", @"", @"file"]
    };
    NSString *plist = [NSTemporaryDirectory() stringByAppendingPathComponent:@"zigunios-compiler-commands.plist"];
    if (![commands writeToFile:plist atomically:YES]) {
        ios_command = NULL;
        snprintf(compiler_error, sizeof(compiler_error), "cc: unable to register compiler\n");
        return NO;
    }
    NSError *error = add(plist);
    if (error) {
        ios_command = NULL;
        snprintf(compiler_error, sizeof(compiler_error), "cc: command registration: %s\n",
                 error.localizedDescription.UTF8String);
        return NO;
    }
    mini([NSURL fileURLWithPath:[NSString stringWithUTF8String:shell->root] isDirectory:YES]);
    if (allow) allow(@[[NSString stringWithUTF8String:shell->root], [[NSBundle mainBundle] bundlePath], NSTemporaryDirectory()]);
    return YES;
}

static NSString *quote(NSString *path)
{
    NSMutableString *result = [NSMutableString stringWithString:@"\""];
    for (NSUInteger i = 0; i < path.length; i++) {
        unichar c = [path characterAtIndex:i];
        if (c == '\"' || c == '\\') [result appendString:@"\\"];
        [result appendFormat:@"%C", c];
    }
    [result appendString:@"\""];
    return result;
}

static NSString *invoke(NSString *command)
{
    NSString *log = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"zu-cc-%@.log", NSUUID.UUID.UUIDString]];
    NSString *err = [log stringByAppendingString:@".err"];
    NSString *line = [NSString stringWithFormat:@"%@ > %@ 2> %@",
                      command, quote(log), quote(err)];
    int code = ios_command(line.UTF8String);
    NSString *stdoutText = [NSString stringWithContentsOfFile:log encoding:NSUTF8StringEncoding error:nil] ?: @"";
    NSString *stderrText = [NSString stringWithContentsOfFile:err encoding:NSUTF8StringEncoding error:nil] ?: @"";
    [[NSFileManager defaultManager] removeItemAtPath:log error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:err error:nil];
    NSMutableString *text = [NSMutableString stringWithFormat:@"%@%@", stdoutText, stderrText];
    if (code) [text appendFormat:@"cc: command returned %d\n", code];
    return text;
}

static NSString *resolve(ZUShell *shell, NSString *path)
{
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, path.UTF8String, guest, host)) return nil;
    return [NSString stringWithUTF8String:host];
}

char *zu_compiler_execute(void *context, ZUShell *shell, int argc, const char *const argv[])
{
    (void)context;
    if (argc < 1 || !shell) return result("cc: missing input\n");
    if (argc >= 2 && !strcmp(argv[1], "--help"))
        return result("cc source.c [-o file.wasm]  compile C into WASI WebAssembly\n"
                      "cc --version              query on-device Clang\n"
                      "cc --doctor               report compiler components\n"
                      "Run an output with: wasm file.wasm\n"
                      "Source files must be inside the Zigunios workspace.\n");

    pthread_mutex_lock(&compiler_lock);
    @autoreleasepool {
        if (!prepare_compiler(shell)) {
            char *answer = result(compiler_error);
            pthread_mutex_unlock(&compiler_lock);
            return answer;
        }
        if (argc >= 2 && !strcmp(argv[1], "--doctor")) {
            pthread_mutex_unlock(&compiler_lock);
            return result("cc: native Clang 14 and LLD are bundled\n"
                          "target: wasm32-wasi; runtime: wasm3 (interpreter, no JIT)\n"
                          "Note: this is Clang, not a Zig binary.\n");
        }
        if (argc >= 2 && !strcmp(argv[1], "--version")) {
            NSString *output = invoke(@"clang --version");
            char *answer = result(output.UTF8String);
            pthread_mutex_unlock(&compiler_lock);
            return answer;
        }

        NSString *source = nil;
        NSString *output = nil;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-o") && i + 1 < argc) {
                output = [NSString stringWithUTF8String:argv[++i]];
            } else if (argv[i][0] == '-') {
                pthread_mutex_unlock(&compiler_lock);
                return result("cc: unsupported option (try cc --help)\n");
            } else if (!source) {
                source = [NSString stringWithUTF8String:argv[i]];
            } else {
                pthread_mutex_unlock(&compiler_lock);
                return result("cc: currently accepts one C source file\n");
            }
        }
        if (!source || ![source.pathExtension isEqualToString:@"c"]) {
            pthread_mutex_unlock(&compiler_lock);
            return result("cc: expected a .c source file\n");
        }
        NSString *inputPath = resolve(shell, source);
        if (!inputPath || ![[NSFileManager defaultManager] fileExistsAtPath:inputPath]) {
            pthread_mutex_unlock(&compiler_lock);
            return result("cc: source file missing or unsafe\n");
        }
        if (!output) output = [source.stringByDeletingPathExtension stringByAppendingPathExtension:@"wasm"];
        NSString *outputPath = resolve(shell, output);
        if (!outputPath || [outputPath isEqualToString:[NSString stringWithUTF8String:shell->root]]) {
            pthread_mutex_unlock(&compiler_lock);
            return result("cc: output location invalid\n");
        }
        NSString *sysroot = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"wasi-sysroot"];
        NSString *startup = [sysroot stringByAppendingPathComponent:@"lib/wasm32-wasi/crt1.o"];
        NSString *stdlib = [sysroot stringByAppendingPathComponent:@"lib/wasm32-wasi"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:startup]) {
            pthread_mutex_unlock(&compiler_lock);
            return result("cc: WASI sysroot missing (crt1.o)\n");
        }
        NSString *object = [NSTemporaryDirectory() stringByAppendingPathComponent:
            [NSString stringWithFormat:@"zu-cc-%@.o", NSUUID.UUID.UUIDString]];
        NSString *resources = [[NSBundle mainBundle] resourcePath];
        NSString *compile = [NSString stringWithFormat:
            @"clang --target=wasm32-wasi -std=c11 -O0 -c %@ -o %@ --sysroot=%@ -I %@ -isystem %@",
            quote(inputPath), quote(object), quote(sysroot), quote(resources),
            quote([resources stringByAppendingPathComponent:@"clang-include"])];
        NSString *messages = invoke(compile);
        BOOL good = [[NSFileManager defaultManager] fileExistsAtPath:object];
        if (good) {
            NSString *link = [NSString stringWithFormat:
                @"wasm-ld -o %@ %@ %@ -L%@ -lc",
                quote(outputPath), quote(startup), quote(object), quote(stdlib)];
            NSString *linkMessages = invoke(link);
            messages = [messages stringByAppendingString:linkMessages];
            good = [[NSFileManager defaultManager] fileExistsAtPath:outputPath];
        }
        [[NSFileManager defaultManager] removeItemAtPath:object error:nil];
        if (good)
            messages = [messages stringByAppendingFormat:@"cc: built %@\n", output];
        else
            messages = [messages stringByAppendingString:@"cc: build failed\n"];
        char *answer = result(messages.UTF8String);
        pthread_mutex_unlock(&compiler_lock);
        return answer;
    }
}
