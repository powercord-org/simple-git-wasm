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

const { existsSync } = require('fs')
const { mkdir } = require('fs/promises')

// This is needed because of issues with NODERAWFS in emscripten
// See: https://github.com/emscripten-core/emscripten/issues/7487
async function setupFs (path) {
  if (!existsSync(path)) await mkdir(path)

  const dir = Math.random().toString(16).slice(2)
  FS.mkdir(dir)
  FS.mount(NODEFS, { root: path }, dir)
  return dir
}

async function awaitWorker () {
  while (!PThread.unusedWorkers.length) {
    await new Promise((resolve) => setImmediate(resolve))
  }
}

async function clone (repo, path) {
  path = await setupFs(path)
  await awaitWorker()

  // Alloc stuff
  const repoPtr = allocString(repo)
  const pathPtr = allocString(path)
  const [ promise, deferredPtr ] = allocDeferred()
  function freeResources () {
    _free(repoPtr)
    _free(pathPtr)
    freeDeferred(deferredPtr)
    FS.unmount(path)
    FS.rmdir(path)
  }

  // Invoke our function -- note: unless an error occurred, it is unsafe to free resources before the promise resolves
  let res = _clone(repoPtr, pathPtr, deferredPtr)
  if (res < 0) {
    freeResources()
    throw new Error('Failed to initialize git thread')
  }

  res = await promise
  freeResources()
  return res
}

async function pull (path, force = false) {
  path = await setupFs(path)
  await awaitWorker()

  // Alloc stuff
  const pathPtr = allocString(path)
  const [ promise, deferredPtr ] = allocDeferred()
  function freeResources () {
    _free(pathPtr)
    freeDeferred(deferredPtr)
    FS.unmount(path)
    FS.rmdir(path)
  }

  // Invoke our function -- note: unless an error occurred, it is unsafe to free resources before the promise resolves
  let res = _pull(pathPtr, force, deferredPtr)
  if (res < 0) {
    freeResources()
    throw new Error('Failed to initialize git thread')
  }

  res = await promise
  freeResources()
  return res
}

async function listUpdates (path) {
  path = await setupFs(path)

  // Alloc stuff
  const pathPtr = allocString(path)
  const [ promise, deferredPtr ] = allocDeferred()
  const [ ret, retPtr ] = allocArray()
  function freeResources () {
    _free(pathPtr)
    freeDeferred(deferredPtr)
    freeArray(retPtr)
    FS.unmount(path)
    FS.rmdir(path)
  }

  // Invoke our function -- note: unless an error occurred, it is unsafe to free resources before the promise resolves
  let res = _list_updates(pathPtr, retPtr, deferredPtr)
  if (res < 0) {
    freeResources()
    throw new Error('Failed to initialize git thread')
  }

  res = await promise
  freeResources()
  return res == 0 ? ret : null
}

Module['clone'] = clone
Module['pull'] = pull
Module['listUpdates'] = listUpdates
