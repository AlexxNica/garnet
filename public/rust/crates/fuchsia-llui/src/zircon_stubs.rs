use libc;

use fuchsia_zircon_sys::zx_handle_t;
use fuchsia_zircon_sys::zx_status_t;

bitflags! {
    #[repr(C)]
    pub flags zx_vmar_flags_t: u32 {
    // flags to vmar routines
        const ZX_VM_FLAG_PERM_READ          = 1 << 0,
        const ZX_VM_FLAG_PERM_WRITE         = 1  << 1,
        const ZX_VM_FLAG_PERM_EXECUTE       = 1  << 2,
        const ZX_VM_FLAG_COMPACT            = 1  << 3,
        const ZX_VM_FLAG_SPECIFIC           = 1  << 4,
        const ZX_VM_FLAG_SPECIFIC_OVERWRITE = 1  << 5,
        const ZX_VM_FLAG_CAN_MAP_SPECIFIC   = 1  << 6,
        const ZX_VM_FLAG_CAN_MAP_READ       = 1  << 7,
        const ZX_VM_FLAG_CAN_MAP_WRITE      = 1  << 8,
        const ZX_VM_FLAG_CAN_MAP_EXECUTE    = 1  << 9,
    }
}

#[link(name = "zircon")]
extern "C" {
    // ioctl
    pub fn fdio_ioctl(
        fd: libc::c_int,
        op: libc::c_int,
        in_buf: *const libc::c_void,
        in_len: libc::size_t,
        out_buf: *mut libc::c_void,
        out_len: libc::size_t,
    ) -> isize;

    pub fn zx_vmar_map(
        vmar_handle: zx_handle_t,
        vmar_offset: libc::size_t,
        vmo_handle: zx_handle_t,
        vmo_offset: u64,
        len: libc::size_t,
        flags: u32,
        mapped_addr: *mut u64,
    ) -> zx_status_t;

    pub fn zx_vmar_root_self() -> zx_handle_t;
}

pub fn make_ioctl(kind: i32, family: i32, number: i32) -> i32 {
    ((((kind) & 0xF) << 20) | (((family) & 0xFF) << 8) | ((number) & 0xFF))
}

pub const IOCTL_KIND_DEFAULT: i32 = 0;
pub const IOCTL_KIND_GET_HANDLE: i32 = 0x1;

pub const IOCTL_FAMILY_DEVICE: i32 = 0x01;
pub const IOCTL_FAMILY_CONSOLE: i32 = 0x10;
pub const IOCTL_FAMILY_INPUT: i32 = 0x11;
pub const IOCTL_FAMILY_DISPLAY: i32 = 0x12;
