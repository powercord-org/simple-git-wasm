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

## Usage
Installation:
```
[pnpm | yarn | npm] i @powercord/simple-git-wasm
```

### Clone a repository
**Note**: Submodules will be cloned as well.
```js
const sgw = require('@powercord/simple-git-wasm')

try {
  await sgw.clone('https://github.com/powercord-org/simple-git-wasm', './sgw')
} catch (e) {
  console.error('An error occurred while cloning the repository!')
}
```
### Pull a repository
**Note**: Submodules will be updated if necessary.
```js
const sgw = require('@powercord/simple-git-wasm')

try {
  await sgw.pull('./sgw')
} catch (e) {
  console.error('An error occurred while pulling the repository!')
}
```

### Check for updates (new commits)
```js
const sgw = require('@powercord/simple-git-wasm')

try {
  const updates = await sgw.listUpdates('./sgw')
  console.log(updates)
  // ~> [
  // ~>   { id: 'abcdef.....', message: 'This is the newest commit', author: 'Cynthia' },
  // ~>   { id: 'abcdef.....', message: 'This is a new commit', author: 'Cynthia' },
  // ~>   { id: 'abcdef.....', message: 'This is the oldest new commit', author: 'Cynthia' },
  // ~> ]
} catch (e) {
  console.error('An error occurred while pulling the repository!')
}
```

## Notes
The following PRs are required for this to work:
 - https://github.com/emscripten-core/emscripten/pull/14722

I (Cynthia) patched my Emscripten installation to strip things not needed but included in the final build.
 - https://github.com/emscripten-core/emscripten/issues/11805 (patch in the comments)
