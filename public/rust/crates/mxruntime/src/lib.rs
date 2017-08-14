// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for runtime services provided by Magenta

extern crate magenta;
extern crate magenta_sys;
extern crate mxruntime_sys;

use magenta::{AsHandleRef, Handle, Channel, ChannelOpts, Status};

use magenta_sys::{mx_handle_t, MX_OK};

use mxruntime_sys::{mxio_service_connect, mxio_service_connect_at};

use std::ffi::CString;

pub const MX_HANDLE_INVALID: mx_handle_t = 0;

// Startup handle types, derived from magenta/system/public/magenta/processargs.h

// Note: this is not a complete list, add more as use cases emerge.
#[repr(u32)]
pub enum HandleType {
    // Handle types used by the device manager and device hosts
    Resource = mxruntime_sys::PA_RESOURCE,
    // Handle types used by the mojo application model
    ApplicationLauncher = mxruntime_sys::PA_APP_LAUNCHER,
    OutgoingServices = mxruntime_sys::PA_APP_SERVICES,
    User0 = mxruntime_sys::PA_USER0,
}

/// Get a startup handle of the given type, if available.
pub fn get_startup_handle(htype: HandleType) -> Option<Handle> {
    unsafe {
        let raw = mxruntime_sys::mx_get_startup_handle(htype as u32);
        if raw == mxruntime_sys::MX_HANDLE_INVALID {
            None
        } else {
            Some(Handle::from_raw(raw))
        }
    }
}

pub fn get_service_root() -> Result<Channel, Status> {
    let (h1, h2) = Channel::create(ChannelOpts::Normal).unwrap();
    let svc = CString::new("/svc/.").unwrap();
    let connect_status = unsafe {
        mxio_service_connect(svc.as_ptr(), h1.raw_handle())
    };
    if connect_status == MX_OK {
        Ok(h2)
    } else {
        Err(Status::from_raw(connect_status))
    }
}

pub fn connect_to_environment_service(service_root: Channel, path: &str) -> Result<Channel, Status> {
    let (h1, h2) = Channel::create(ChannelOpts::Normal).unwrap();
    let path_str = CString::new(path).unwrap();
    let connect_status = unsafe {
        mxio_service_connect_at(service_root.raw_handle(), path_str.as_ptr(), h1.raw_handle())
    };
    if connect_status == MX_OK {
        Ok(h2)
    } else {
        Err(Status::from_raw(connect_status))
    }
}
