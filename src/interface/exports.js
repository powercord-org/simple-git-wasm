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

const nodeFs = require('fs')

/* eslint-disable dot-notation */
const existsSync = nodeFs['existsSync']
const mkdir = nodeFs['promises']['mkdir']
/* eslint-enable dot-notation */

async function mount (path) {
  if (!existsSync(path)) await mkdir(path)

  const dir = Math.random().toString(36).slice(2)
  FS.mkdir(dir)
  FS.mount(NODEFS, { root: path }, dir)
  return dir
}

function umount (path) {
  FS.unmount(path)
  FS.rmdir(path)
}

function free (allocated) {
  for (const a of allocated) {
    if (a[1]) {
      freeArray(a[0])
    } else {
      _free(a[0])
    }
  }
}

function makeWrapper (method, returns = false, threaded = true) {
  return async function (...rawArgs) {
    if (threaded) {
      // Wait for an available worker
      while (!PThread.unusedWorkers.length) {
        await new Promise((resolve) => setImmediate(resolve))
      }
    }

    let ret = void 0
    const args = []
    const toFree = []
    const [ promise, deferredPtr ] = threaded ? allocDeferred() : []
    for (const arg of rawArgs) {
      if (typeof arg === 'string') {
        const ptr = allocString(arg)
        toFree.push([ ptr ])
        args.push(ptr)
        continue
      }

      args.push(arg)
    }

    if (returns) {
      const [ a, ptr ] = allocArray()
      args.push(ptr)
      ret = a
    }

    let res = Module[`_${method}`](...args, deferredPtr)
    if (threaded) {
      if (res < 0) {
        free(toFree)
        throw new Error('Failed to initialize git thread')
      }

      res = await promise
    }

    free(toFree)
    if (res < 0) {
      const error = new Error(`simple-git-wasm: call to ${method} failed: error code ${res}`)
      error.code = res
      throw error
    }

    return ret
  }
}

/* eslint-disable dot-notation */
Module['mount'] = mount
Module['umount'] = umount
Module['clone'] = makeWrapper('clone')
Module['pull'] = makeWrapper('pull')
Module['listUpdates'] = makeWrapper('list_updates', true)
Module['readRepositoryMeta'] = makeWrapper('read_repository_meta', true, false)
