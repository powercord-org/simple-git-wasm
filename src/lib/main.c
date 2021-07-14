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

#include <git2/global.h>
#include <git2/clone.h>
#include <git2/submodule.h>

// libgit2 doesn't support shallow clone -- see https://github.com/libgit2/libgit2/issues/3058

#define UNUSED(x) (void)(x)

typedef struct { int init; git_submodule_update_options* options; } submodule_payload;

static int _process_submodule(git_submodule* submodule, const char* name, void* p) {
  submodule_payload* payload = (submodule_payload*) p;
  if (payload->options->version == 0) {
    git_submodule_update_options_init(payload->options, 1);
    payload->options->fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
  }

  return git_submodule_update(submodule, payload->init, payload->options);
}

static int update_submodules(git_repository* repo) {
  int ret;
  struct git_submodule_update_options submodule_options;
  submodule_payload* payload;

  payload = malloc(sizeof(submodule_payload));
  payload->init = 1;
  payload->options = &submodule_options;
  ret = git_submodule_foreach(repo, _process_submodule, payload);
  free(payload);

  return ret;
}

EMSCRIPTEN_KEEPALIVE
int clone(char* repository, char* destination) {
  int ret;
  struct git_repository* repo;
  struct git_clone_options clone_options;

  git_clone_options_init(&clone_options, 1);
  clone_options.fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;

  ret = git_clone(&repo, repository, destination, &clone_options);
  if (ret < 0) return ret;

  ret = update_submodules(repo);
  git_repository_free(repo);
  return ret;
}

EMSCRIPTEN_KEEPALIVE
int pull() {
  return -1;
}

int main() {
  git_libgit2_init();
  return 0;
}
