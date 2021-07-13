# `simple-git-wasm`
A (relatively) small node library to clone and pull git repositories in a standalone manner thanks to
[libgit2](https://github.com/libgit2/libgit2), powered by [WebAssembly](https://webassembly.org/) and
[Emscripten](https://emscripten.org).

Made to be used as part of the [Powercord](https://powercord.dev) built-in updater. Despite the set purpose, the lib
can be used by anyone who wishes to if it fits their use case.

## Why?
Not happy with the solutions I had. [isomorphic-git](https://github.com/isomorphic-git/isomorphic-git) is a HEAVY
beast, and [wasm-git](https://github.com/petersalomonsen/wasm-git) is too raw for me, me like some syntax sugar.
Makes my software sweeter. It also doesn't support things it cannot support because of its target (I only care
about Node, so I can make different design choices).
