#import <Foundation/Foundation.h>
#include "cbuild_bridge.h"
#include "compiler_bridge.h"
#include "wasm_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glob.h>

// First-stage adapter for xt9y/build.c: build.c is *executed* as WebAssembly,
// using the original cbuild.h API. A constrained host runner then builds a
// single-file, C-only executable. Multi-target, C++, generated sources and
// native graphics builds deliberately report unsupported rather than faking it.

static char *reply(NSString *value)
{
    return strdup((value ?: @"c: failure\n").UTF8String);
}

static NSString *path_for(ZUShell *shell, NSString *value)
{
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, value.UTF8String, guest, host)) return nil;
    return [NSString stringWithUTF8String:host];
}

static NSString *quote_for_c(NSString *path)
{
    return [[path stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"]
              stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
}

static char *create_project(ZUShell *shell)
{
    NSString *buildfile = path_for(shell, @"build.c");
    NSString *mainfile = path_for(shell, @"main.c");
    if (!buildfile || !mainfile) return reply(@"c init: invalid directory\n");
    NSFileManager *fm = NSFileManager.defaultManager;
    if ([fm fileExistsAtPath:buildfile] || [fm fileExistsAtPath:mainfile])
        return reply(@"c init: build.c or main.c already exists; refusing overwrite\n");
    NSString *build = @"#include <cbuild.h>\n"
        @"void build(C_Build *b) {\n"
        @"    C_Target *app = c_executable(b, \"app\");\n"
        @"    c_sources(app, \"main.c\");\n"
        @"}\n";
    NSString *main = @"#include <stdio.h>\n"
        @"int main(void) { puts(\"Hello from Zigunios C!\"); return 0; }\n";
    NSError *error = nil;
    if (![build writeToFile:buildfile atomically:YES encoding:NSUTF8StringEncoding error:&error])
        return reply([NSString stringWithFormat:@"c init: %@\n", error.localizedDescription]);
    if (![main writeToFile:mainfile atomically:YES encoding:NSUTF8StringEncoding error:&error])
        return reply([NSString stringWithFormat:@"c init: %@\n", error.localizedDescription]);
    return reply(@"c init: generated build.c + main.c\nNext: c build run\n");
}

static NSString *compile_file(ZUShell *shell, NSString *source, NSString *output)
{
    const char *argv[] = {
        "cc", source.UTF8String, "-o", output.UTF8String
    };
    char *response = zu_compiler_execute(NULL, shell, 4, argv);
    NSString *result = response ? [NSString stringWithUTF8String:response] : @"cc failed\n";
    free(response);
    return result ?: @"cc returned non-UTF-8 output\n";
}

char *zu_cbuild_execute(void *context, ZUShell *shell, int argc, const char *const argv[])
{
    (void)context;
    if (!shell || argc < 2) return reply(@"c init | c build [run] | c run | c inspect\n"
        "C target support: one .c source, wasm32-wasi only.\n");
    if (!strcmp(argv[1], "init") && argc == 2) return create_project(shell);
    BOOL execute = !strcmp(argv[1], "run") ||
        (!strcmp(argv[1], "build") && argc == 3 && !strcmp(argv[2], "run"));
    if (strcmp(argv[1], "build") && strcmp(argv[1], "run"))
        return reply(@"c: unsupported command. Try c init or c build run\n");
    if (argc > (execute && !strcmp(argv[1], "build") ? 3 : 2))
        return reply(@"usage: c build [run]\n");

    NSString *build = path_for(shell, @"build.c");
    if (!build || ![NSFileManager.defaultManager fileExistsAtPath:build])
        return reply(@"c: build.c not found; run c init or cd into a project\n");

    // Use a distinct, temporary WASI program per invocation.
    NSString *uid = NSUUID.UUID.UUIDString;
    NSString *wrapperName = [NSString stringWithFormat:@"/tmp/cbuild-%@.c", uid];
    NSString *moduleName = [NSString stringWithFormat:@"/tmp/cbuild-%@.wasm", uid];
    NSString *wrapperHost = path_for(shell, wrapperName);
    NSString *moduleHost = path_for(shell, moduleName);
    if (!wrapperHost || !moduleHost) return reply(@"c: failed to allocate temporary workspace\n");

    NSString *code = [NSString stringWithFormat:
        @"#include <cbuild.h>\n"
         "#include \"%@\"\n"
         "static C_Build state;\n"
         "int main(void) {\n"
         "    state.default_target = -1;\n"
         "    build(&state);\n"
         "    if (!state.target_count) { puts(\"CBUILD_ERROR no-target\"); return 1; }\n"
         "    int idx = state.default_target >= 0 ? state.default_target : 0;\n"
         "    C_Target *t = &state.targets[idx];\n"
         "    printf(\"CBUILD_TARGET\\t%%s\\t%%d\\n\", t->name, (int)t->kind);\n"
         "    for (size_t i=0; i<t->sources.count; i++)\n"
         "        printf(\"CBUILD_SOURCE\\t%%s\\n\", t->sources.items[i]);\n"
         "    for (size_t i=0; i<t->includes.count; i++)\n"
         "        printf(\"CBUILD_INCLUDE\\t%%s\\n\", t->includes.items[i]);\n"
         "    for (size_t i=0; i<t->ldflags.count; i++)\n"
         "        printf(\"CBUILD_LINK\\t%%s\\n\", t->ldflags.items[i]);\n"
         "    printf(\"CBUILD_DONE\\n\"); return 0;\n"
         "}\n", quote_for_c(build)];

    NSError *error = nil;
    if (![code writeToFile:wrapperHost atomically:YES encoding:NSUTF8StringEncoding error:&error])
        return reply([NSString stringWithFormat:@"c: cannot generate build wrapper: %@\n",
                      error.localizedDescription]);
    NSString *compilerReport = compile_file(shell, wrapperName, moduleName);
    if (![NSFileManager.defaultManager fileExistsAtPath:moduleHost]) {
        [NSFileManager.defaultManager removeItemAtPath:wrapperHost error:nil];
        return reply([NSString stringWithFormat:@"c: could not compile build.c:\n%@", compilerReport]);
    }

    const char *wasmArgv[] = {"wasm", moduleName.UTF8String};
    char *manifestText = zu_wasm_execute(NULL, shell, 2, wasmArgv);
    NSString *manifest = manifestText ? [NSString stringWithUTF8String:manifestText] : nil;
    free(manifestText);
    [NSFileManager.defaultManager removeItemAtPath:wrapperHost error:nil];
    [NSFileManager.defaultManager removeItemAtPath:moduleHost error:nil];

    if (!manifest || ![manifest containsString:@"CBUILD_DONE"] ||
        ![manifest containsString:@"[wasm exit 0]"])
        return reply([NSString stringWithFormat:@"c: build.c evaluation failed:\n%@\n",
                      manifest ?: @"no output"]);

    NSString *name = nil, *src = nil;
    int kind = -1, sources = 0;
    NSMutableArray<NSString *> *unsupported = NSMutableArray.new;
    for (NSString *line in [manifest componentsSeparatedByString:@"\n"]) {
        if ([line hasPrefix:@"CBUILD_TARGET\t"]) {
            NSArray *parts = [line componentsSeparatedByString:@"\t"];
            if (parts.count == 3) { name = parts[1]; kind = [parts[2] intValue]; }
        } else if ([line hasPrefix:@"CBUILD_SOURCE\t"]) {
            sources++;
            src = [line substringFromIndex:14];
        } else if ([line hasPrefix:@"CBUILD_LINK\t"]) {
            [unsupported addObject:[line substringFromIndex:12]];
        }
    }
    if (!name || kind != 0 || sources != 1 || unsupported.count ||
        ![src.pathExtension isEqualToString:@"c"] || [src containsString:@"*"]) {
        return reply([NSString stringWithFormat:
            @"c: evaluated build.c, target=%@, type=%d, sources=%d, static links=%lu\n"
             "c: unsupported target. Current runner builds one direct .c source, no external archives or C++.\n"
             "HorseEngine.cpp needs the later multi-library + C++ + rendering stages.\n",
            name ?: @"?", kind, sources, (unsigned long)unsupported.count]);
    }
    NSString *target = [name stringByAppendingPathExtension:@"wasm"];
    NSString *outputPath = path_for(shell, target);
    NSString *sourcePath = path_for(shell, src);
    if (!outputPath || !sourcePath || ![NSFileManager.defaultManager fileExistsAtPath:sourcePath])
        return reply(@"c: target source file missing or unsafe\n");
    NSString *buildReport = compile_file(shell, src, target);
    if (![NSFileManager.defaultManager fileExistsAtPath:outputPath])
        return reply([NSString stringWithFormat:@"c: compilation failed:\n%@", buildReport]);
    NSMutableString *response = [NSMutableString stringWithFormat:
        @"c: built target %@ as %@ (wasm32-wasi)\n", name, target];
    if (execute) {
        const char *runArgv[] = {"wasm", target.UTF8String};
        char *runText = zu_wasm_execute(NULL, shell, 2, runArgv);
        if (runText) { [response appendString:@(runText)]; free(runText); }
    }
    return reply(response);
}
