#include <emscripten.h>
#include <stdio.h>

#include <git2/clone.h>
#include <git2/global.h>

// libgit2 doesn't support shallow clone -- see https://github.com/libgit2/libgit2/issues/3058

EMSCRIPTEN_KEEPALIVE
int clone (char* repository, char* destination) {
  struct git_repository* repo;
  struct git_clone_options options;
  git_clone_options_init(&options, 1);
  options.fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
  return git_clone(&repo, repository, destination, &options);
}

EMSCRIPTEN_KEEPALIVE
int pull () {
  return -1;
}

int main () {
  git_libgit2_init();
  return 0;
}
