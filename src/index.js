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

function setupFs (libgit, path) {
  const dir = Math.random().toString(16).slice(2)
  libgit.FS.mkdir(dir)
  libgit.FS.mount(libgit.FS.filesystems.NODEFS, { root: path }, dir)
  return dir
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
    // [Cynthia] This check would be normally done by Git, but considering
    // we need to wake up workers and queue tasks, best to check beforehand.
    throw new Error(`Cannot clone the repository in ${path}`)
  }

  const libgit = await getLibgit()
  const dir = setupFs(libgit, path)

  // Alloc stuff
  const repoPtr = libgit.allocString(repo)
  const pathPtr = libgit.allocString(dir)
  const [ promise, deferredPtr ] = libgit.allocDeferred()
  function freeResources () {
    libgit._free(repoPtr)
    libgit._free(pathPtr)
    libgit.freeDeferred(deferredPtr)
    libgit.FS.unmount(dir)
  }

  // Invoke our function, once we have a worker ready to handle the new thread
  // Note: unless an error occurred, it is unsafe to free resources before the promise resolves
  while (!libgit.PThread.unusedWorkers.length) await new Promise((resolve) => setImmediate(resolve))
  let res = libgit.ccall('clone', 'number', [ 'number', 'number', 'number' ], [ repoPtr, pathPtr, deferredPtr ])
  if (res < 0) {
    freeResources()
    throw new Error('Failed to initialize git thread')
  }

  res = await promise
  freeResources()
  return res
}

// todo: re-write this to the new pull function signature and stuff
async function pull (path, force = false) {
  // todo: verify specified path is a git repo
  path = resolve(path)
  const libgit = await getLibgit()

  const dir = setupFs(libgit, path)
  const res = await libgit.ccall('pull', 'number', [ 'string', 'number' ], [ dir, force ], { async: true })
  libgit.FS.unmount(dir)
  return res
}

module.exports = {
  clone,
  pull
}
