// todo: workerize -- this is just a rough draft

const { resolve } = require('path')
const { stat, mkdir } = require('fs/promises')
const libgitFactory = require('./wasm/libgit.js')

let libgitPromise
async function getLibgit () {
  if (!libgitPromise) {
    // Lazily init the wasm binary when we need it
    libgitPromise = libgitFactory()
    libgitPromise.then((libgit) => {
      libgit.FS.mkdir('/work')
      libgit.FS.chdir('/work')
    })
  }

  return libgitPromise
}

async function canClone (path) {
  try {
    const res = await stat(path)
    if (!res.isDirectory()) return false
    // todo: check if dir is empty
  } catch (e) {
    if (e.code !== 'ENOENT') return false
    mkdir(path, { recursive: true })
  }

  return true
}

async function clone (repo, path) {
  path = resolve(path)
  if (!canClone(path)) {
    // [Cynthia] This check would be normally done by Git, but considering we need to wake up workers
    // and queue tasks, best to check beforehand.
    throw new Error(`Cannot clone the repository in ${path}`)
  }

  const libgit = await getLibgit()

  // Workaround for the fact we can't run with default fs as nodefs
  // See: https://github.com/emscripten-core/emscripten/issues/7487
  const dir = Math.random().toString(16).slice(2)
  libgit.FS.mkdir(dir)
  libgit.FS.mount(libgit.FS.filesystems.NODEFS, { root: path }, dir)
  const res = await libgit.ccall('clone', 'number', [ 'string', 'string' ], [ repo, dir ], { async: true })
  libgit.FS.unmount(dir)
  return res
}

async function pull (path) {
  // todo
  // libgit.ccall('pull')
}

module.exports = {
  clone,
  pull
}
