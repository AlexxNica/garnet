// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package pkgfs

import (
	"fmt"
	"io"
	"os"
	"sync"

	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"

	"thinfs/fs"
	"thinfs/zircon/rpc"
)

// mountInfo is a platform specific type that carries platform specific mounting
// data, such as the file descriptor or handle of the mount.
type mountInfo struct {
	unmountOnce  sync.Once
	serveChannel *zx.Channel
	parentFd     int
}

// Mount attaches the filesystem host to the given path. If an error occurs
// during setup, this method returns that error. If an error occurs after
// serving has started, the error causes a log.Fatal. If the given path does not
// exist, it is created before mounting.
func (f *Filesystem) Mount(path string) error {
	err := os.MkdirAll(path, os.ModePerm)
	if err != nil {
		return err
	}

	f.mountInfo.parentFd, err = syscall.Open(path, syscall.O_ADMIN|syscall.O_DIRECTORY, 0777)
	if err != nil {
		return err
	}

	var rpcChan *zx.Channel
	rpcChan, f.mountInfo.serveChannel, err = zx.NewChannel(0)
	if err != nil {
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("channel creation: %s", err)
	}

	if err := syscall.FDIOForFD(f.mountInfo.parentFd).IoctlSetHandle(fdio.IoctlVFSMountFS, f.mountInfo.serveChannel.Handle); err != nil {
		f.mountInfo.serveChannel.Close()
		f.mountInfo.serveChannel = nil
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("mount failure: %s", err)
	}

	vfs, err := rpc.NewServer(f, rpcChan.Handle)
	if err != nil {
		f.mountInfo.serveChannel.Close()
		f.mountInfo.serveChannel = nil
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("vfs server creation: %s", err)
	}

	// TODO(raggi): handle the exit case more cleanly.
	go func() {
		defer f.Unmount()
		vfs.Serve()
	}()
	return nil
}

// Unmount detaches the filesystem from a previously mounted path. If mount was not previously called or successful, this will panic.
func (f *Filesystem) Unmount() {
	f.mountInfo.unmountOnce.Do(func() {
		// TODO(raggi): log errors?
		syscall.FDIOForFD(f.mountInfo.parentFd).Ioctl(fdio.IoctlVFSUnmountNode, nil, nil)
		f.mountInfo.serveChannel.Close()
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.serveChannel = nil
		f.mountInfo.parentFd = -1
	})
}

func goErrToFSErr(err error) error {
	switch e := err.(type) {
	case nil:
		return nil
	case *os.PathError:
		return goErrToFSErr(e.Err)
	case zx.Error:
		switch e.Status {
		case zx.ErrNotFound:
			return fs.ErrNotFound

		default:
			debugLog("pkgfs: unmapped os err to fs err: %T %v", err, err)
			return err

		}
	}
	switch err {
	case os.ErrInvalid:
		return fs.ErrInvalidArgs
	case os.ErrPermission:
		return fs.ErrPermission
	case os.ErrExist:
		return fs.ErrAlreadyExists
	case os.ErrNotExist:
		return fs.ErrNotFound
	case os.ErrClosed:
		return fs.ErrNotOpen
	case io.EOF:
		return fs.ErrEOF
	default:
		debugLog("pkgfs: unmapped os err to fs err: %T %v", err, err)
		return err
	}
}
