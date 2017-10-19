// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use libc::PATH_MAX;

use bytes;
use bytes::BufMut;
use std::sync::Arc;
use tokio_core;
use zircon;
use tokio_fuchsia;
use futures;
use futures::Future;
use std;
use std::io;
use std::borrow::Borrow;
use std::os::unix::ffi::OsStrExt;

use remoteio::*;

/// Vfs contains filesystem global state and outlives all Vnodes that it
/// services. It fundamentally handles filesystem global concerns such as path
/// walking through mounts, moves and links, watchers, and so on.
pub trait Vfs {
    fn open(&self, _vn: &Arc<Vnode>, _path: std::path::PathBuf, _flags: i32, _mode: u32) -> Result<(Arc<Vnode>, std::path::PathBuf), zircon::Status> {
        // TODO(raggi): ...
        Err(zircon::Status::ErrNotSupported)
    }

    fn register_connection(&self, c: Connection, handle: &tokio_core::reactor::Handle) {
        handle.spawn(c.map_err(|e| eprintln!("fuchsia-vfs: connection error {:?}", e)))
    }
}

/// Vnode represents a single addressable node in the filesystem (that may be
/// addressable via more than one path). It may have file, directory, mount or
/// device semantics.
pub trait Vnode {
    fn close(&self) -> zircon::Status {
        zircon::Status::NoError
    }

    fn serve(&self, _vfs: Arc<Vfs>, _chan: tokio_fuchsia::Channel, _flags: i32) {
        // TODO(raggi): ...
        // TODO(raggi): ...
        // TODO(raggi): ...
        // TODO(raggi): ...
    }
}

/// Connection represents a single client connection to a Vnode within a Vfs. It
/// contains applicable IO state such as current position, as well as the channel
/// on which the IO is served.
pub struct Connection {
    vfs: Arc<Vfs>,
    vn: Arc<Vnode>,
    chan: tokio_fuchsia::Channel,
    buf: zircon::MessageBuf,
    handle: tokio_core::reactor::Handle,
}

impl Connection {
    pub fn new(
        vfs: Arc<Vfs>,
        vn: Arc<Vnode>,
        chan: zircon::Channel,
        handle: &tokio_core::reactor::Handle,
    ) -> Result<Connection, io::Error> {
        let mut c = Connection {
            vfs: vfs,
            vn: vn,
            chan: tokio_fuchsia::Channel::from_channel(chan, handle)?,
            buf: zircon::MessageBuf::new(),
            handle: handle.clone(),
        };

        c.buf.ensure_capacity_bytes(ZXRIO_MSG_SZ);
        c.buf.ensure_capacity_handles(FDIO_MAX_HANDLES);

        Ok(c)
    }

    fn dispatch(&mut self) -> Result<(), std::io::Error> {
        let len = self.buf.bytes().len();

        if len < ZXRIO_HDR_SZ {
            eprintln!("vfs: channel read too short: {} {}", len, ZXRIO_HDR_SZ);
            return Err(zircon::Status::ErrIo.into());
        }

        // NOTE(raggi): we're explicitly and deliberately breaching the type and
        // the mutability here. We own the messagebuf.
        let msg: &mut zxrio_msg_t = unsafe { &mut *(self.buf.bytes().as_ptr() as *mut _) };

        if len > FDIO_CHUNK_SIZE || self.buf.n_handles() > FDIO_MAX_HANDLES
            || len != msg.datalen as usize + ZXRIO_HDR_SZ
            || self.buf.n_handles() != ZXRIO_HC!(msg.op) as usize
        {
            eprintln!(
                "vfs: invalid message: len: {}, nhandles: {}, {:?}",
                len,
                self.buf.n_handles(),
                msg
            );
            self.reply_status(&self.chan, zircon::Status::ErrInvalidArgs)?;

            return Err(zircon::Status::ErrInvalidArgs.into());
        }

        println!("{:?} <- {:?}", self.chan, msg);

        match ZXRIO_OP!(msg.op) {
            ZXRIO_OPEN => {
                let chan = tokio_fuchsia::Channel::from_channel(
                    // ZXRIO_HC! returns 1 for ZXRIO_OPEN, so n_handles must == 1
                    zircon::Channel::from(self.buf.take_handle(0).expect("vfs: handle disappeared")),
                    &self.handle,
                )?;

                // TODO(raggi): enforce O_ADMIN
                if msg.datalen < 1 || msg.datalen > PATH_MAX as u32 {
                    self.reply_status(&chan, zircon::Status::ErrInvalidArgs)?;
                    return Err(zircon::Status::ErrInvalidArgs.into());
                }

                let path = std::path::PathBuf::from(std::ffi::OsStr::from_bytes(&msg.data[0..msg.datalen as usize]));

                // TODO(raggi): verify if the protocol mistreatment of args signage is intentionally unchecked here:
                self.open(chan, path, msg.arg, unsafe { msg.arg2.mode })?;
            }
            // ZXRIO_STAT => self.stat(msg, chan, handle),
            // ZXRIO_CLOSE => self.close(msg, chan, handle),
            _ => self.reply_status(&self.chan, zircon::Status::ErrNotSupported)?
        }

        Ok(())
    }

    fn open(&self, chan: tokio_fuchsia::Channel, path: std::path::PathBuf, flags: i32, mode: u32) -> Result<(), std::io::Error> {
        let pipeline = flags & O_PIPELINE != 0;
        let open_flags = flags & !O_PIPELINE;

        let mut status = zircon::Status::NoError;
        let mut proto = FDIO_PROTOCOL_REMOTE;
        let mut handles: Vec<zircon::Handle> = vec![];

        match self.vfs.open(&self.vn, path, open_flags, mode) {
            Ok((vn, _path)) => {
                // TODO(raggi): get_handles (maybe call it get_extra?)

                // protocols that return handles on open can't be pipelined.
                if pipeline && handles.len() > 0 {
                    vn.close();
                    return Err(std::io::ErrorKind::InvalidInput.into());
                }

                if status != zircon::Status::NoError {
                    return Err(std::io::ErrorKind::InvalidInput.into());
                }

                if !pipeline {
                    self.write_zxrio_object(&chan, status, proto, &[], &mut handles).ok();
                }

                // TODO(raggi): construct connection...
                vn.serve(Arc::clone(&self.vfs), chan, open_flags);
                
                return Ok(());
            }
            Err(e) => {
                proto = 0;
                status = e;
                eprintln!("vfs: open error: {:?}", e);
            }
        }

        if !pipeline {
            return self.write_zxrio_object(&chan, status, proto, &[], &mut handles);
        }
        Ok(())
    }

    fn reply_status(&self, chan: &tokio_fuchsia::Channel, status: zircon::Status) -> Result<(), io::Error>  {
        println!("{:?} -> {:?}", &chan, status);

        self.write_zxrio_object(chan, status, 0, &[], &mut vec![])
    }

    fn write_zxrio_object(
        &self,
        chan: &tokio_fuchsia::Channel,
        status: zircon::Status,
        proto: u32,
        extra: &[u8],
        handles: &mut Vec<zircon::Handle>,
    ) -> Result<(), io::Error> {
        if extra.len() > ZXRIO_OBJECT_EXTRA || handles.len() > FDIO_MAX_HANDLES as usize {
            return Err(io::ErrorKind::InvalidInput.into());
        }

        let mut buf = bytes::BytesMut::with_capacity(ZXRIO_OBJECT_MINSIZE + extra.len());
        buf.put_i32::<bytes::LittleEndian>(status as i32);
        buf.put_u32::<bytes::LittleEndian>(proto);
        buf.put_slice(extra);

        chan.write(buf.borrow(), handles, 0)
    }
}

impl Future for Connection {
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self) -> futures::Poll<Self::Item, Self::Error> {
        loop {
            try_nb!(self.chan.recv_from(0, &mut self.buf));
            // Note: ignores errors, as they are sent on the protocol
            let _ = self.dispatch();
            for i in 0..self.buf.n_handles() {
                if let Some(h) = self.buf.take_handle(i) {
                    std::mem::drop(h);
                }
            }
        }
    }
}
