// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sync"
	"time"

	"amber/pkg"
)

// Watcher implements a basic filesystem watcher that polls /pkgfs/needs for new
// entries that amber needs to fetch.
type Watcher struct {
	d *Daemon

	pollDelay  time.Duration
	retryDelay time.Duration

	mu       sync.Mutex
	packages map[string]struct{}
	blobs    map[string]struct{}
	failures map[string]struct{}
}

// NewWatcher initializes a new Watcher
func NewWatcher(d *Daemon) *Watcher {
	return &Watcher{
		d: d,

		pollDelay:  time.Second,
		retryDelay: 5 * time.Minute,

		packages: map[string]struct{}{},
		blobs:    map[string]struct{}{},
		failures: map[string]struct{}{},
	}
}

// Watch watches path for new packages and path/blobs for new blobs to request.
func (w *Watcher) Watch(path string) {
	lastClear := time.Now()
	for {
		time.Sleep(w.pollDelay) // TODO(raggi): configurable polling rate

		if time.Since(lastClear) > w.retryDelay {
			w.mu.Lock()
			w.failures = map[string]struct{}{}
			w.mu.Unlock()
			lastClear = time.Now()
		}

		d, err := os.Open(path)
		if err != nil {
			log.Printf("unable to open %s: %s", path, err)
			continue
		}
		names, err := d.Readdirnames(-1)
		d.Close()
		if err != nil {
			log.Printf("unable to readdirnames %s: %s", path, err)
			continue
		}
		for _, name := range names {
			w.mu.Lock()
			_, ok := w.failures[name]
			w.mu.Unlock()
			if ok {
				continue
			}

			w.mu.Lock()
			_, ok = w.packages[name]
			if !ok {
				w.packages[name] = struct{}{}
			}
			w.mu.Unlock()

			if ok {
				continue
			}
			if name == "blobs" {
				continue
			}

			go func(n string) {
				if pErr := w.getPackage(n); pErr != nil {
					log.Printf("Package update failed %s\n", pErr)
					w.mu.Lock()
					w.failures[n] = struct{}{}
					w.mu.Unlock()
				}
			}(name)
		}

		d, err = os.Open(filepath.Join(path, "blobs"))
		if err != nil {
			log.Printf("unable to open %s: %s", path, err)
			continue
		}
		names, err = d.Readdirnames(-1)
		d.Close()
		if err != nil {
			log.Printf("unable to readdirnames %s: %s", path, err)
			continue
		}
		for _, name := range names {
			w.mu.Lock()
			_, ok := w.failures[name]
			w.mu.Unlock()
			if ok {
				continue
			}

			w.mu.Lock()
			_, ok = w.blobs[name]
			if !ok {
				w.blobs[name] = struct{}{}
			}
			w.mu.Unlock()

			if ok {
				continue
			}

			go w.getBlob(name)
		}

	}
}

func (w *Watcher) getPackage(name string) error {
	defer func() {
		w.mu.Lock()
		delete(w.packages, name)
		w.mu.Unlock()
	}()

	pk := pkg.Package{Name: "/" + name}
	ps := pkg.NewPackageSet()
	ps.Add(&pk)
	updates := w.d.GetUpdates(ps)
	r, ok := updates[pk]
	if !ok {
		return fmt.Errorf("Update result didn't contain requested update\n")
	}
	if r.Err != nil {
		return r.Err
	}

	_, err := WriteUpdateToPkgFS(r)
	return err
}

func (w *Watcher) getBlob(name string) {
	defer func() {
		w.mu.Lock()
		delete(w.blobs, name)
		w.mu.Unlock()
	}()

	if err := w.d.GetBlob(name); err != nil {
		w.mu.Lock()
		w.failures[name] = struct{}{}
		w.mu.Unlock()

		log.Printf("amber: failed to fetch blob %q: %s", name, err)
	}
}
