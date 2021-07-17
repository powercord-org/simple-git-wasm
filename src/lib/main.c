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

#include <git2/global.h>
#include <git2/clone.h>
#include <git2/submodule.h>
#include <git2/signature.h>
#include <git2/repository.h>
#include <git2/remote.h>
#include <git2/stash.h>
#include <git2/oid.h>

// libgit2 doesn't support shallow clone -- see https://github.com/libgit2/libgit2/issues/3058

#define UNUSED(X) (void)(X)
#define RESOLVE(PTR, RET) MAIN_THREAD_ASYNC_EM_ASM({ WasmModule.invokeDeferred($0, $1); }, PTR, RET)

typedef struct { char* repository; char* path; int resolve_ptr; } clone_payload;
typedef struct { char* path; int force; int resolve_ptr; } pull_payload;

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
  if (is_merge) git_oid_cpy((git_oid*) payload, oid);
  return 0;
}

EM_JS(char*, get_message, (), {
  const date = new Date().toString() + ": simple-git-wasm: changes before pull";
  const size = lengthBytesUTF8(jsString) + 1;
  const ptr = _malloc(size);
  stringToUTF8(date, ptr, size);
  return ptr;
});

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

  ret = git_signature_now(&signature, "simple-git-wasm", "none@example.com");
  if (ret < 0) return ret;

  char* message = get_message();
  ret = git_stash_save(&stashid, repo, signature, message, GIT_STASH_DEFAULT);
	git_signature_free(signature);
  free(message);
  return ret;
}

static void* clone_repository(void* payload) {
  int ret = 0;
  git_repository* repo;
  git_clone_options clone_options;
  clone_payload* opts = (clone_payload*) payload;

  git_clone_options_init(&clone_options, GIT_CLONE_OPTIONS_VERSION);
  clone_options.fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;

  ret = git_clone(&repo, opts->repository, opts->path, NULL);
  if (ret < 0) goto clone_end;

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

  ret = git_repository_open(&repo, opts->path);
  if (ret < 0) goto pull_end;

  if (opts->force) {
    // [Cynthia] Design choice from Powercord here; force-pull = discard all local changes.
    // We used to reset --hard, here we stash the local changes so they aren't completely lost.
    ret = stash_changes(repo);
    if (ret < 0) goto pull_end;
  }

  // Lookup the remote
  ret = git_remote_lookup(&remote, repo, "origin");
  if (ret < 0) goto pull_end;

  // Update FETCH_HEAD
  git_fetch_options_init(&fetch_options, GIT_FETCH_OPTIONS_VERSION);
  ret = git_remote_fetch(remote, NULL, &fetch_options, NULL);
  if (ret < 0) goto pull_end;

  // Find out the object id to merge
  ret = git_repository_fetchhead_foreach(repo, _extract_oid, &oid);
  if (ret < 0) goto pull_end;

  // Retrieve current reference
  ret = git_repository_head(&ref_current, repo);
  if (ret < 0) goto pull_end;

  // Lookup target object
  ret = git_object_lookup(&target, repo, &oid, GIT_OBJECT_COMMIT);
  if (ret < 0) goto pull_end;

  // Fast-forward checkout
  git_checkout_options_init(&checkout_options, GIT_CHECKOUT_OPTIONS_VERSION);
  checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE;
  ret = git_checkout_tree(repo, target, &checkout_options);
  if (ret < 0) goto pull_end;

  // Move references
  ret = git_reference_set_target(&ref_updated, ref_current, &oid, NULL);
  if (ret < 0) goto pull_end;

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
int pull(char* path, int force, int resolve_ptr) {
  pthread_t pid;
  pull_payload* payload;

  payload = malloc(sizeof(pull_payload));
  if (payload == NULL) return -1;
  payload->path = path;
  payload->force = force;
  payload->resolve_ptr = resolve_ptr;

  pthread_create(&pid, NULL, pull_repository, (void*) payload);
  pthread_detach(pid);
  return 0;
}

int main() {
  git_libgit2_init();
  return 0;
}
