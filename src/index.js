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
