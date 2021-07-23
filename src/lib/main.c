/*
 * Copyright (c) 2021 Cynthia K. Rey, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <emscripten.h>
#include <pthread.h>
#include <time.h>

#include <git2/global.h>
#include <git2/clone.h>
#include <git2/submodule.h>
#include <git2/signature.h>
#include <git2/repository.h>
#include <git2/remote.h>
#include <git2/stash.h>
#include <git2/oid.h>
#include <git2/revwalk.h>
#include <git2/commit.h>

// libgit2 doesn't support shallow clone -- see https://github.com/libgit2/libgit2/issues/3058
// note: a patch to libgit is necessary to get clone to work -- see https://github.com/libgit2/libgit2/pull/5935

#define UNUSED(X) (void)(X)
#define RESOLVE(PTR, RET) MAIN_THREAD_EM_ASM({ invokeDeferred($0, $1); }, PTR, RET)

// Funni macros
#define ERR_CHECK_RET(FN) ret = FN; if (ret < 0) return ret
#define ERR_CHECK_LAB(FN, L) ret = FN; if (ret < 0) goto L
#define ERR_CHECK_GET(A, B, C, ...) C
#define ERR_CHECK(...) ERR_CHECK_GET(__VA_ARGS__, ERR_CHECK_LAB, ERR_CHECK_RET)(__VA_ARGS__)

typedef struct { char* repository; char* path; int resolve_ptr; } clone_payload;
typedef struct { char* path; int skip_fetch; int force; int resolve_ptr; } pull_payload;
typedef struct { char* path; int ret_ptr; int resolve_ptr; } list_updates_payload;

static int _process_submodule(git_submodule* submodule, const char* name, void* payload) {
  git_submodule_update_options* options = (git_submodule_update_options*) payload;
  if (options->version == 0) {
    git_submodule_update_options_init(options, GIT_SUBMODULE_UPDATE_OPTIONS_VERSION);
    options->fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
  }

  return git_submodule_update(submodule, 1, options);
}

static int _extract_oid(const char* ref_name, const char* remote_url, const git_oid* oid, unsigned int is_merge, void* payload) {
  UNUSED(ref_name);
  UNUSED(remote_url);
  if (is_merge) {
    git_oid_cpy((git_oid*) payload, oid);
    return 1;
  }
  return 0;
}

static int update_submodules(git_repository* repo) {
  int ret = 0;
  git_submodule_update_options submodule_options;
  ret = git_submodule_foreach(repo, _process_submodule, &submodule_options);
  return ret;
}

static int stash_changes(git_repository* repo) {
  // Code from this function pretty much taken from libgit2's example stash.c.
  int ret = 0;
  git_signature* signature;
  git_oid stashid;

  ERR_CHECK(git_signature_now(&signature, "simple-git-wasm", "none@example.com"));

  // Get message
  time_t t = time(NULL);
  struct tm* tm = localtime(&t);
  char* message = malloc(64);
  strftime(message, 64, "[%d/%m/%y %H:%M:%S] simple-git-wasm: changes before pull", tm);

  // Stash changes
  ret = git_stash_save(&stashid, repo, signature, message, GIT_STASH_DEFAULT);
	git_signature_free(signature);
  free(message);
  return ret;
}

static void get_short_commit_message(const git_commit* commit, char* buf) {
  const char* msg = git_commit_message_raw(commit);
  int i;

  for (i = 0; i < 70; i++) {
    if (msg[i] == '\n') break;
    buf[i] = msg[i];
  }

  if (i == 70 && msg[i] != '\n') {
    buf[i++] = (char) 226;
    buf[i++] = (char) 128;
    buf[i++] = (char) 166;
  }

  buf[i] = '\0';
}

static void* clone_repository(void* payload) {
  int ret = 0;
  git_repository* repo;
  git_clone_options clone_options;
  clone_payload* opts = (clone_payload*) payload;

  git_clone_options_init(&clone_options, GIT_CLONE_OPTIONS_VERSION);
  clone_options.fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;

  ERR_CHECK(git_clone(&repo, opts->repository, opts->path, NULL), clone_end);
  ret = update_submodules(repo);
clone_end:
  git_repository_free(repo);
  RESOLVE(opts->resolve_ptr, ret);
  free(payload);
  return NULL;
}

static void* pull_repository(void* payload) {
  int ret = 0;
  git_repository* repo;
  git_remote* remote;
  git_reference* ref_current;
  git_reference* ref_updated;
	git_object* target;

  git_oid oid;
  git_fetch_options fetch_options;
  git_checkout_options checkout_options;
  pull_payload* opts = (pull_payload*) payload;

  ERR_CHECK(git_repository_open(&repo, opts->path), pull_end);

  if (opts->force) {
    // [Cynthia] Design choice from Powercord here; force-pull = discard all local changes.
    // We used to reset --hard, here we stash the local changes so they aren't completely lost.
    ERR_CHECK(stash_changes(repo), pull_end);
  }

  // Lookup the remote
  ERR_CHECK(git_remote_lookup(&remote, repo, "origin"), pull_end);

  if (opts->skip_fetch == 0) {
    // Update FETCH_HEAD
    git_fetch_options_init(&fetch_options, GIT_FETCH_OPTIONS_VERSION);
    ERR_CHECK(git_remote_fetch(remote, NULL, &fetch_options, NULL), pull_end);
  }

  // Find out the object id to merge
  ERR_CHECK(git_repository_fetchhead_foreach(repo, _extract_oid, &oid), pull_end);

  // Retrieve current reference
  ERR_CHECK(git_repository_head(&ref_current, repo), pull_end);

  // Lookup target object
  ERR_CHECK(git_object_lookup(&target, repo, &oid, GIT_OBJECT_COMMIT), pull_end);

  // Fast-forward checkout
  git_checkout_options_init(&checkout_options, GIT_CHECKOUT_OPTIONS_VERSION);
  checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE;
  ERR_CHECK(git_checkout_tree(repo, target, &checkout_options), pull_end);

  // Move references
  ERR_CHECK(git_reference_set_target(&ref_updated, ref_current, &oid, NULL), pull_end);

  // Update submodules
  ret = update_submodules(repo);
pull_end:
  // Cleanup & return
  git_reference_free(ref_current);
  git_reference_free(ref_updated);
  git_object_free(target);
  git_remote_free(remote);
  git_repository_free(repo);
  RESOLVE(opts->resolve_ptr, ret);
  free(payload);
  return NULL;
}

void* list_repository_updates(void* payload) {
  int ret = 0;
  int entries;
  git_repository* repo;
  git_remote* remote;
  git_revwalk* walker;
  git_oid local_oid;
  git_oid remote_oid;

  git_fetch_options fetch_options;
  list_updates_payload* opts = (list_updates_payload*) payload;

  ERR_CHECK(git_repository_open(&repo, opts->path), list_end);

  // Lookup the remote
  ERR_CHECK(git_remote_lookup(&remote, repo, "origin"), list_end);

  // Update FETCH_HEAD
  git_fetch_options_init(&fetch_options, GIT_FETCH_OPTIONS_VERSION);
  ERR_CHECK(git_remote_fetch(remote, NULL, &fetch_options, NULL), list_end);

  // Lookup OIDs
  ERR_CHECK(git_reference_name_to_id(&local_oid, repo, "HEAD"), list_end);
  ERR_CHECK(git_repository_fetchhead_foreach(repo, _extract_oid, &remote_oid), list_end);

  // Setup walker
  ERR_CHECK(git_revwalk_new(&walker, repo), list_end);
  ERR_CHECK(git_revwalk_push(walker, &remote_oid), list_end);
  ERR_CHECK(git_revwalk_sorting(walker, GIT_SORT_TIME & GIT_SORT_REVERSE), list_end);

  // Walk through commits
  git_oid oid;
  git_commit* commit;
  while (git_revwalk_next(&oid, walker) == 0) {
    if (git_oid_cmp(&oid, &local_oid) == 0) break;

    ERR_CHECK(git_commit_lookup(&commit, repo, &oid), list_end);

    char* message = malloc(74);
    get_short_commit_message(commit, message);

    char* author;
    author = git_commit_author(commit)->name;

    MAIN_THREAD_EM_ASM({ arrayPush($0, UTF8ToString($1), UTF8ToString($2)) }, opts->ret_ptr, message, author);
    git_commit_free(commit);
    free(message);
  }

list_end:
  // Cleanup & return
  git_revwalk_free(walker);
  git_remote_free(remote);
  git_repository_free(repo);
  RESOLVE(opts->resolve_ptr, ret);
  free(payload);
  return NULL;
}

EMSCRIPTEN_KEEPALIVE
int clone(char* repository, char* path, int resolve_ptr) {
  pthread_t pid;
  clone_payload* payload;

  payload = malloc(sizeof(clone_payload));
  if (payload == NULL) return -1;
  payload->repository = repository;
  payload->path = path;
  payload->resolve_ptr = resolve_ptr;

  pthread_create(&pid, NULL, clone_repository, (void*) payload);
  pthread_detach(pid);
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int pull(char* path, int skip_fetch, int force, int resolve_ptr) {
  pthread_t pid;
  pull_payload* payload;

  payload = malloc(sizeof(pull_payload));
  if (payload == NULL) return -1;
  payload->path = path;
  payload->skip_fetch = skip_fetch;
  payload->force = force;
  payload->resolve_ptr = resolve_ptr;

  pthread_create(&pid, NULL, pull_repository, (void*) payload);
  pthread_detach(pid);
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int list_updates(char* path, int ret_ptr, int resolve_ptr) {
  pthread_t pid;
  list_updates_payload* payload;

  payload = malloc(sizeof(list_updates_payload));
  if (payload == NULL) return -1;
  payload->path = path;
  payload->ret_ptr = ret_ptr;
  payload->resolve_ptr = resolve_ptr;

  pthread_create(&pid, NULL, list_repository_updates, (void*) payload);
  pthread_detach(pid);
  return 0;
}

int main() {
  git_libgit2_init();
  return 0;
}
