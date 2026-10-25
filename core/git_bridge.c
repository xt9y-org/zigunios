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

char *zu_git_execute(void *context, ZUShell *shell, int argc, const char *const argv[])
{
    (void)context;
    Message out = {.data = calloc(ZU_GIT_OUTPUT, 1), .length = 0};
    if (!out.data) return NULL;
    if (argc < 2) {
        append(&out, "git commands: clone <https-url> [folder], status, log, branch, fetch\n");
        return out.data;
    }

    git_libgit2_init();
    const char *cmd = argv[1];
    git_repository *repo = NULL;
    int code = 0;

    if (!strcmp(cmd, "clone")) {
        if (argc < 3 || argc > 4) {
            append(&out, "usage: git clone HTTPS_URL [directory]\n");
            goto done;
        }
        const char *url = argv[2];
        if (strncmp(url, "https://", 8)) {
            append(&out, "git clone: only HTTPS repositories are supported in this build\n");
            goto done;
        }
        char name[ZU_PATH_MAX], guest[ZU_PATH_MAX], path[ZU_PATH_MAX];
        if (argc == 4) {
            if (strlen(argv[3]) >= sizeof(name)) {
                append(&out, "git clone: destination path too long\n"); goto done;
            }
            strcpy(name, argv[3]);
        } else {
            const char *base = repo_name(url);
            size_t length = strcspn(base, "?#");
            if (length > 4 && !strncmp(base + length - 4, ".git", 4)) length -= 4;
            if (length == 0 || length >= sizeof(name)) {
                append(&out, "git clone: invalid repository URL\n"); goto done;
            }
            memcpy(name, base, length);
            name[length] = 0;
        }
        if (!strcmp(name, ".") || !strcmp(name, "..") ||
            !zu_shell_resolve_path(shell, name, guest, path) || !strcmp(guest, "/")) {
            append(&out, "git clone: invalid or unsafe destination path\n"); goto done;
        }
        struct stat st;
        if (lstat(path, &st) == 0) {
            append(&out, "git clone: destination already exists: %s\n", guest); goto done;
        }
        git_clone_options options = GIT_CLONE_OPTIONS_INIT;
        append(&out, "Cloning into '%s'...\n", guest);
        code = git_clone(&repo, url, path, &options);
        if (code != 0) {
            git_failure(&out, "clone", code);
            append(&out, "A failed clone may have left a partial folder. Remove it before retrying.\n");
        } else append(&out, "Clone complete. cd %s\n", name);
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
    } else {
        append(&out, "git %s: not implemented. Available: clone, status, log, branch, fetch\n", cmd);
    }

done:
    git_repository_free(repo);
    git_libgit2_shutdown();
    return out.data;
}
