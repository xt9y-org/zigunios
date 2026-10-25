#define _POSIX_C_SOURCE 200809L
#include "git_bridge.h"

#include <git2.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ZU_GIT_OUTPUT 32768

typedef struct {
    char *data;
    size_t length;
} Message;

static void append(Message *m, const char *format, ...)
{
    if (m->length >= ZU_GIT_OUTPUT - 1) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(m->data + m->length, ZU_GIT_OUTPUT - m->length, format, args);
    va_end(args);
    if (n > 0) {
        size_t remaining = ZU_GIT_OUTPUT - m->length - 1;
        m->length += (size_t)n < remaining ? (size_t)n : remaining;
    }
}

static void git_failure(Message *m, const char *action, int code)
{
    const git_error *error = git_error_last();
    append(m, "git %s: %s (code %d)\n", action,
           error && error->message ? error->message : "operation failed", code);
}

static int repo_here(ZUShell *shell, git_repository **repo, Message *out)
{
    char guest[ZU_PATH_MAX], path[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, ".", guest, path)) {
        append(out, "git: invalid current directory\n");
        return -1;
    }
    int code = git_repository_open_ext(repo, path, GIT_REPOSITORY_OPEN_CROSS_FS, shell->root);
    if (code != 0) {
        append(out, "fatal: not a git repository (or any parent directory)\n");
        return code;
    }
    return 0;
}

static const char *repo_name(const char *url)
{
    const char *last = strrchr(url, '/');
    return last ? last + 1 : url;
}


/* libgit2 remains inside the app process; no executable Git binary is spawned. */
typedef struct {
    Message *out;
    int depth;
    unsigned count;
} SubmoduleContext;

static int update_submodules(git_repository *repository, Message *out, int depth);

static int on_submodule(git_submodule *submodule, const char *name, void *payload)
{
    SubmoduleContext *ctx = payload;
    if (++ctx->count > 256) {
        append(ctx->out, "git submodule: too many submodules\n");
        return -1;
    }
    git_submodule_update_options options = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
    // Fetch the exact gitlink commit, even if no current branch points to it.
    int code = git_submodule_update(submodule, 1, &options);
    if (code != 0) {
        git_failure(ctx->out, name, code);
        return code;
    }
    append(ctx->out, "Submodule ready: %s\n", name);
    git_repository *nested = NULL;
    if (git_submodule_open(&nested, submodule) == 0) {
        code = update_submodules(nested, ctx->out, ctx->depth + 1);
        git_repository_free(nested);
        if (code) return code;
    }
    return 0;
}

static int update_submodules(git_repository *repository, Message *out, int depth)
{
    if (depth > 8) {
        append(out, "git submodule: recursion limit reached\n");
        return -1;
    }
    SubmoduleContext context = {.out = out, .depth = depth, .count = 0};
    return git_submodule_foreach(repository, on_submodule, &context);
}

static int switch_ref(git_repository *repo, const char *refname, Message *out)
{
    char target[ZU_PATH_MAX];
    git_reference *reference = NULL;
    git_reference *branch = NULL;
    git_object *commit = NULL;
    int code = 0;
    if (snprintf(target, sizeof(target), "refs/remotes/origin/%s", refname) < (int)sizeof(target))
        code = git_reference_lookup(&reference, repo, target);
    else code = -1;

    if (code == 0) {
        // Preserve the upstream tracking branch on a normal --branch checkout.
        const git_oid *oid = git_reference_target(reference);
        if (!oid) { code = -1; goto cleanup; }
        code = git_object_lookup(&commit, repo, oid, GIT_OBJECT_COMMIT);
        if (code) goto cleanup;
        // A shallow clone may already have created this local branch.
        code = git_branch_lookup(&branch, repo, refname, GIT_BRANCH_LOCAL);
        if (code == GIT_ENOTFOUND) {
            code = git_branch_create(&branch, repo, refname, (git_commit *)commit, 0);
        }
        if (code) goto cleanup;
        code = git_repository_set_head(repo, git_reference_name(branch));
    } else {
        git_reference_free(reference);
        reference = NULL;
        if (snprintf(target, sizeof(target), "refs/tags/%s", refname) >= (int)sizeof(target)) {
            append(out, "git: tag name too long\n"); return -1;
        }
        // A shallow clone of the default branch does not necessarily fetch a tag.
        git_remote *remote = NULL;
        git_fetch_options fetch = GIT_FETCH_OPTIONS_INIT;
        fetch.depth = 1;
        if (git_remote_lookup(&remote, repo, "origin") == 0) {
            char spec[ZU_PATH_MAX];
            if (snprintf(spec, sizeof(spec), "+refs/tags/%s:refs/tags/%s",
                         refname, refname) < (int)sizeof(spec)) {
                char *items[] = {spec};
                git_strarray refs = {.strings = items, .count = 1};
                (void)git_remote_fetch(remote, &refs, &fetch, NULL);
            }
        }
        git_remote_free(remote);
        code = git_reference_lookup(&reference, repo, target);
        if (code != 0) {
            append(out, "git: branch or tag not found: %s\n", refname);
            goto cleanup;
        }
        code = git_reference_peel(&commit, reference, GIT_OBJECT_COMMIT);
        if (code) goto cleanup;
        code = git_repository_set_head_detached(repo, git_object_id(commit));
    }
    if (!code) {
        git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
        options.checkout_strategy = GIT_CHECKOUT_SAFE;
        code = git_checkout_head(repo, &options);
    }
cleanup:
    git_object_free(commit);
    git_reference_free(reference);
    git_reference_free(branch);
    return code;
}

char *zu_git_execute(void *context, ZUShell *shell, int argc, const char *const argv[])
{
    (void)context;
    Message out = {.data = calloc(ZU_GIT_OUTPUT, 1), .length = 0};
    if (!out.data) return NULL;
    if (argc < 2) {
        append(&out, "git: clone [--depth 1] [--branch vX] [--recurse-submodules], submodule update --init --recursive, status, log, branch, fetch, diff, config, add, commit\n");
        return out.data;
    }

    git_libgit2_init();
    const char *cmd = argv[1];
    git_repository *repo = NULL;
    int code = 0;

    if (!strcmp(cmd, "clone")) {
        const char *url = NULL, *destination = NULL, *branch = NULL;
        int depth = 0, recursive = 0;
        for (int i = 2; i < argc; i++) {
            const char *arg = argv[i];
            if (!strcmp(arg, "--depth") && i + 1 < argc) {
                depth = atoi(argv[++i]);
            } else if (!strncmp(arg, "--depth=", 8)) {
                depth = atoi(arg + 8);
            } else if ((!strcmp(arg, "--branch") || !strcmp(arg, "-b")) && i + 1 < argc) {
                branch = argv[++i];
            } else if (!strncmp(arg, "--branch=", 9)) {
                branch = arg + 9;
            } else if (!strcmp(arg, "--recurse-submodules") || !strcmp(arg, "--recursive")) {
                recursive = 1;
            } else if (arg[0] == '-') {
                append(&out, "git clone: unsupported option %s\n", arg);
                goto done;
            } else if (!url) url = arg;
            else if (!destination) destination = arg;
            else { append(&out, "git clone: too many paths\n"); goto done; }
        }
        if (!url || strncmp(url, "https://", 8)) {
            append(&out, "usage: git clone [--depth N] [--branch TAG] [--recurse-submodules] HTTPS_URL [directory]\n");
            goto done;
        }
        if (depth < 0 || depth > 5000) {
            append(&out, "git clone: depth must be in range 1..5000\n"); goto done;
        }
        char name[ZU_PATH_MAX], guest[ZU_PATH_MAX], path[ZU_PATH_MAX];
        if (destination) {
            if (strlen(destination) >= sizeof(name)) {
                append(&out, "git clone: destination too long\n"); goto done;
            }
            strcpy(name, destination);
        } else {
            const char *base = repo_name(url);
            size_t length = strcspn(base, "?#");
            if (length > 4 && !strncmp(base + length - 4, ".git", 4)) length -= 4;
            if (!length || length >= sizeof(name)) {
                append(&out, "git clone: invalid repository URL\n"); goto done;
            }
            memcpy(name, base, length);
            name[length] = 0;
        }
        if (!zu_shell_resolve_path(shell, name, guest, path) || !strcmp(guest, "/")) {
            append(&out, "git clone: unsafe destination\n"); goto done;
        }
        struct stat st;
        if (lstat(path, &st) == 0) {
            append(&out, "git clone: destination exists: %s\n", guest); goto done;
        }
        git_clone_options options = GIT_CLONE_OPTIONS_INIT;
        options.fetch_opts.depth = depth;
        append(&out, "Cloning into '%s'...\n", guest);
        code = git_clone(&repo, url, path, &options);
        if (code == 0 && branch) code = switch_ref(repo, branch, &out);
        if (code == 0 && recursive) code = update_submodules(repo, &out, 0);
        if (code) {
            git_failure(&out, "clone", code);
            append(&out, "Clone incomplete. Check the directory before retrying.\n");
        } else {
            append(&out, "Clone ready: %s\n", guest);
        }
    } else if (!strcmp(cmd, "submodule")) {
        if (argc < 3 || strcmp(argv[2], "update")) {
            append(&out, "usage: git submodule update --init --recursive\n"); goto done;
        }
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--init") && strcmp(argv[i], "--recursive")) {
                append(&out, "git submodule: unsupported option %s\n", argv[i]); goto done;
            }
        }
        if (repo_here(shell, &repo, &out)) goto done;
        code = update_submodules(repo, &out, 0);
        if (code) git_failure(&out, "submodule update", code);
    } else if (!strcmp(cmd, "-C")) {
        if (argc < 6 || strcmp(argv[3], "submodule") || strcmp(argv[4], "update")) {
            append(&out, "usage: git -C DIR submodule update --init --recursive\n"); goto done;
        }
        for (int i = 5; i < argc; ++i) {
            if (strcmp(argv[i], "--init") && strcmp(argv[i], "--recursive")) {
                append(&out, "git submodule: unsupported option %s\n", argv[i]); goto done;
            }
        }
        char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
        if (!zu_shell_resolve_path(shell, argv[2], guest, host)) {
            append(&out, "git -C: unsafe path\n"); goto done;
        }
        code = git_repository_open_ext(&repo, host, GIT_REPOSITORY_OPEN_CROSS_FS, shell->root);
        if (code) { git_failure(&out, "-C", code); goto done; }
        code = update_submodules(repo, &out, 0);
        if (code) git_failure(&out, "submodule update", code);
    } else if (!strcmp(cmd, "status")) {
        if (argc != 2) { append(&out, "usage: git status\n"); goto done; }
        if (repo_here(shell, &repo, &out)) goto done;
        git_status_options options = GIT_STATUS_OPTIONS_INIT;
        options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
        options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
        git_status_list *list = NULL;
        code = git_status_list_new(&list, repo, &options);
        if (code) git_failure(&out, "status", code);
        else {
            size_t count = git_status_list_entrycount(list);
            if (!count) append(&out, "Working tree clean\n");
            for (size_t i = 0; i < count && out.length < ZU_GIT_OUTPUT - 512; i++) {
                const git_status_entry *entry = git_status_byindex(list, i);
                if (!entry) continue;
                const char *path = entry->head_to_index && entry->head_to_index->new_file.path
                    ? entry->head_to_index->new_file.path
                    : entry->index_to_workdir && entry->index_to_workdir->new_file.path
                    ? entry->index_to_workdir->new_file.path : "?";
                append(&out, "%c %s\n",
                       entry->status == GIT_STATUS_WT_NEW ? '?' :
                       (entry->status & GIT_STATUS_INDEX_NEW) ? 'A' :
                       (entry->status & (GIT_STATUS_WT_DELETED | GIT_STATUS_INDEX_DELETED)) ? 'D' : 'M', path);
            }
        }
        git_status_list_free(list);
    } else if (!strcmp(cmd, "log")) {
        if (argc != 2) { append(&out, "usage: git log\n"); goto done; }
        if (repo_here(shell, &repo, &out)) goto done;
        git_revwalk *walk = NULL;
        code = git_revwalk_new(&walk, repo);
        if (!code) {
            git_revwalk_sorting(walk, GIT_SORT_TIME);
            code = git_revwalk_push_head(walk);
        }
        if (code) git_failure(&out, "log", code);
        else {
            git_oid oid;
            for (int count = 0; count < 20 && git_revwalk_next(&oid, walk) == 0; count++) {
                git_commit *commit = NULL;
                if (git_commit_lookup(&commit, repo, &oid)) break;
                char sha[GIT_OID_SHA1_HEXSIZE + 1];
                git_oid_tostr(sha, sizeof(sha), &oid);
                const char *summary = git_commit_summary(commit);
                append(&out, "%.10s %s\n", sha, summary ? summary : "(no message)");
                git_commit_free(commit);
            }
        }
        git_revwalk_free(walk);
    } else if (!strcmp(cmd, "branch")) {
        if (argc != 2) { append(&out, "usage: git branch\n"); goto done; }
        if (repo_here(shell, &repo, &out)) goto done;
        git_branch_iterator *it = NULL;
        code = git_branch_iterator_new(&it, repo, GIT_BRANCH_LOCAL);
        if (code) git_failure(&out, "branch", code);
        else {
            git_reference *ref = NULL;
            git_branch_t type;
            while (git_branch_next(&ref, &type, it) == 0) {
                const char *name = NULL;
                git_branch_name(&name, ref);
                append(&out, "%s %s\n", git_branch_is_head(ref) ? "*" : " ",
                       name ? name : "(unknown)");
                git_reference_free(ref);
            }
        }
        git_branch_iterator_free(it);
    } else if (!strcmp(cmd, "fetch")) {
        if (argc != 2) { append(&out, "usage: git fetch\n"); goto done; }
        if (repo_here(shell, &repo, &out)) goto done;
        git_remote *remote = NULL;
        code = git_remote_lookup(&remote, repo, "origin");
        if (!code) code = git_remote_fetch(remote, NULL, NULL, NULL);
        if (code) git_failure(&out, "fetch", code);
        else append(&out, "Fetched from origin (working tree unchanged)\n");
        git_remote_free(remote);
    } else if (!strcmp(cmd, "diff")) {
        if (argc != 2) { append(&out, "usage: git diff\n"); goto done; }
        if (repo_here(shell, &repo, &out)) goto done;
        git_diff *diff = NULL;
        git_buf patch = GIT_BUF_INIT;
        code = git_diff_index_to_workdir(&diff, repo, NULL, NULL);
        if (!code) code = git_diff_to_buf(&patch, diff, GIT_DIFF_FORMAT_PATCH);
        if (code) git_failure(&out, "diff", code);
        else if (patch.ptr) append(&out, "%.*s%s",
            (int)(patch.size < ZU_GIT_OUTPUT - 100 ? patch.size : ZU_GIT_OUTPUT - 100),
            patch.ptr, patch.size >= ZU_GIT_OUTPUT - 100 ? "\n(diff truncated)\n" : "");
        git_buf_dispose(&patch);
        git_diff_free(diff);
    } else if (!strcmp(cmd, "config")) {
        if (argc != 4 || (strcmp(argv[2], "user.name") && strcmp(argv[2], "user.email"))) {
            append(&out, "usage: git config user.name NAME | git config user.email EMAIL\n");
            goto done;
        }
        if (repo_here(shell, &repo, &out)) goto done;
        git_config *config = NULL;
        code = git_repository_config(&config, repo);
        if (!code) code = git_config_set_string(config, argv[2], argv[3]);
        if (code) git_failure(&out, "config", code);
        else append(&out, "Saved %s in repository config\n", argv[2]);
        git_config_free(config);
    } else if (!strcmp(cmd, "add")) {
        if (argc != 3) { append(&out, "usage: git add FILE (or . for all files)\n"); goto done; }
        if (repo_here(shell, &repo, &out)) goto done;
        git_index *index = NULL;
        code = git_repository_index(&index, repo);
        if (!code) {
            if (!strcmp(argv[2], ".")) {
                // A local iPad workspace: stage all files from the current repository.
                code = git_index_add_all(index, NULL, GIT_INDEX_ADD_DEFAULT, NULL, NULL);
                if (!code) code = git_index_update_all(index, NULL, NULL, NULL);
            } else {
                char guest[ZU_PATH_MAX], path[ZU_PATH_MAX];
                if (!zu_shell_resolve_path(shell, argv[2], guest, path)) {
                    append(&out, "git add: invalid or unsafe path\n"); goto done_add;
                }
                const char *workdir = git_repository_workdir(repo);
                size_t length = workdir ? strlen(workdir) : 0;
                if (!workdir || strncmp(path, workdir, length)) {
                    append(&out, "git add: file is outside this repository\n"); goto done_add;
                }
                code = git_index_add_bypath(index, path + length);
            }
            if (!code) code = git_index_write(index);
        }
        if (code) git_failure(&out, "add", code);
        else append(&out, "Changes staged\n");
done_add:
        git_index_free(index);
    } else if (!strcmp(cmd, "commit")) {
        if (argc != 4 || strcmp(argv[2], "-m")) {
            append(&out, "usage: git commit -m \"message\"\n"); goto done;
        }
        if (repo_here(shell, &repo, &out)) goto done;
        git_index *index = NULL;
        git_tree *tree = NULL;
        git_commit *parent = NULL;
        git_signature *signature = NULL;
        git_oid tree_oid, commit_oid, parent_oid;
        code = git_repository_index(&index, repo);
        if (!code) code = git_index_write_tree(&tree_oid, index);
        if (!code) code = git_tree_lookup(&tree, repo, &tree_oid);
        if (!code) code = git_signature_default(&signature, repo);
        if (code) append(&out, "git commit: configure identity first:\n"
                              "  git config user.name \"Your Name\"\n"
                              "  git config user.email \"you@example.com\"\n");
        else {
            int have_parent = git_reference_name_to_id(&parent_oid, repo, "HEAD") == 0;
            if (have_parent) code = git_commit_lookup(&parent, repo, &parent_oid);
            if (!code) {
                const git_commit *parents[] = {parent};
                code = git_commit_create(&commit_oid, repo, "HEAD", signature, signature,
                    NULL, argv[3], tree, have_parent ? 1 : 0,
                    have_parent ? parents : NULL);
            }
            if (code) git_failure(&out, "commit", code);
            else {
                char sha[GIT_OID_SHA1_HEXSIZE + 1];
                git_oid_tostr(sha, sizeof(sha), &commit_oid);
                append(&out, "Committed %.10s %s\n", sha, argv[3]);
            }
        }
        git_signature_free(signature);
        git_commit_free(parent);
        git_tree_free(tree);
        git_index_free(index);
    } else {
        append(&out, "git %s: not implemented. Use clone, submodule, status, log, branch, fetch, diff, config, add, commit\n", cmd);
    }

done:
    git_repository_free(repo);
    git_libgit2_shutdown();
    return out.data;
}
