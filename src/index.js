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

const { resolve } = require('path')
const libgitFactory = require('./wasm/libgit.js')
const config = require('./config.js')

let libgitPromise
async function getLibgit () {
  // Lazily init the wasm binary when we need it
  if (!libgitPromise) libgitPromise = libgitFactory()
  return libgitPromise
}

async function clone (repo, path) {
  const libgit = await getLibgit()
  path = await libgit.mount(resolve(path))
  try {
    await libgit.clone(repo, path)
  } finally {
    libgit.umount(path)
  }
}

async function pull (path, skipFetch = false, force = false) {
  const libgit = await getLibgit()
  path = await libgit.mount(resolve(path))
  try {
    await libgit.pull(path, skipFetch, force)
  } finally {
    libgit.umount(path)
  }
}

async function listUpdates (path) {
  const libgit = await getLibgit()
  path = await libgit.mount(resolve(path))
  let res
  try {
    res = await libgit.listUpdates(path)
  } finally {
    libgit.umount(path)
  }

  const updates = []
  for (let i = 0; i < res.length;) {
    updates.push({
      id: res[i++],
      message: res[i++],
      author: res[i++],
    })
  }

  return updates
}

module.exports = {
  clone: clone,
  pull: pull,
  listUpdates: listUpdates,
  ...config,
}
