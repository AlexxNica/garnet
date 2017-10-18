use std::cmp;
use std::collections::HashMap;
use std::fs::File;
use std::io::Read;
use std::mem;
use std::os::unix::io::AsRawFd;
use std::path::Path;
use std::ptr;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::mpsc::Sender;
use std::thread;

extern crate libc;

use zircon_stubs::*;

use fuchsia_zircon_sys::ZX_SIGNAL_NONE;
use fuchsia_zircon_sys::ZX_USER_SIGNAL_0;
use fuchsia_zircon_sys::zx_handle_t;
use fuchsia_zircon_sys::zx_object_wait_many;
use fuchsia_zircon_sys::zx_wait_item_t;

#[repr(C, packed)]
struct boot_mouse_report_t {
    buttons: u8,
    rel_x: i8,
    rel_y: i8,
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub enum InputEvent {
    Mouse {
        source: i32,
        delta_x: i32,
        delta_y: i32,
        buttons: i32,
    },
    Keyboard { keycode: u8, pressed: bool },
}

pub trait InputReportHandler: Send + Sync {
    fn parse(&mut self, source: i32, buffer: &Vec<u8>, event_channel: &Sender<InputEvent>);
}

pub type InputReportHandlerPtr = Box<InputReportHandler>;

pub struct MouseReportHandler {}

impl InputReportHandler for MouseReportHandler {
    fn parse(&mut self, source: i32, buffer: &Vec<u8>, channel: &Sender<InputEvent>) {
        let mut byte_length = buffer.len();
        let mut index = 0;
        while byte_length > 0 {
            let p = buffer[index..index + 2].as_ptr();
            let report_ptr: *const boot_mouse_report_t = p as *const boot_mouse_report_t;
            let (rel_x, rel_y, buttons) =
                unsafe { ((*report_ptr).rel_x, (*report_ptr).rel_y, (*report_ptr).buttons) };
            byte_length -= mem::size_of::<boot_mouse_report_t>();
            index += mem::size_of::<boot_mouse_report_t>();
            let event = InputEvent::Mouse {
                source: source,
                delta_x: rel_x as i32,
                delta_y: rel_y as i32,
                buttons: buttons as i32,
            };
            channel.send(event).unwrap();
        }
    }
}

pub struct KeyboardReportHandler {}

impl InputReportHandler for KeyboardReportHandler {
    fn parse(&mut self, _source: i32, _buffer: &Vec<u8>, _channel: &Sender<InputEvent>) {}
}

pub struct InputDevice {
    file: File,
    _name: String,
    handle: zx_handle_t,
    handler: InputReportHandlerPtr,
}

impl InputDevice {
    pub fn check_for_events(&mut self, event_channel: &Sender<InputEvent>) -> bool {
        let fd = self.file.as_raw_fd() as i32;
        let mut max_report_len: isize = 0;
        let max_report_len_ptr: *mut libc::c_void = &mut max_report_len as *mut _ as
            *mut libc::c_void;
        let IOCTL_INPUT_GET_MAX_REPORTSIZE = make_ioctl(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 6);
        let rc = unsafe {
            fdio_ioctl(
                fd,
                IOCTL_INPUT_GET_MAX_REPORTSIZE,
                ptr::null(),
                0,
                max_report_len_ptr,
                mem::size_of::<isize>(),
            )
        };

        if rc < 0 || max_report_len < 1 {
            return false;
        }
        let mut buffer: Vec<u8> = vec![0; max_report_len as usize];
        let amount_read = self.file.read(buffer.as_mut_slice()).unwrap();
        if amount_read > 0 {
            self.handler.parse(fd, &buffer, event_channel);
        }
        return true;
    }
}

pub type InputDeviceList = Vec<InputDevice>;

pub struct InputHandler {
    input_devices: InputDeviceList,
}

impl InputHandler {
    pub fn new() -> InputHandler {
        InputHandler { input_devices: Vec::new() }
    }

    pub fn new_handler(input_protocol: i32) -> Option<InputReportHandlerPtr> {
        match input_protocol {
            1 => Some(Box::new(KeyboardReportHandler {})),
            2 => Some(Box::new(MouseReportHandler {})),
            _ => None,
        }
    }

    pub fn open_handle(fd: i32) -> Option<zx_handle_t> {
        let IOCTL_DEVICE_GET_EVENT_HANDLE =
            make_ioctl(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 1);
        let mut handle: zx_handle_t = 0;
        let handle_ptr: *mut libc::c_void = &mut handle as *mut _ as *mut libc::c_void;
        let result = unsafe {
            fdio_ioctl(
                fd,
                IOCTL_DEVICE_GET_EVENT_HANDLE,
                ptr::null(),
                0,
                handle_ptr,
                mem::size_of::<zx_handle_t>(),
            )
        };
        if result < 0 {
            println!("IOCTL_DEVICE_GET_EVENT_HANDLE result = {}", result);
            None
        } else {
            Some(handle)
        }
    }

    pub fn open_devices(&mut self) {
        let mut device_index = 0;
        loop {
            let device_path_string = format!("/dev/class/input/{:03}", device_index);
            let device_path = Path::new(&device_path_string);
            if device_path.exists() {
                let file = File::open(device_path).unwrap();
                let fd = file.as_raw_fd() as i32;
                let mut input_protocol: i32 = 0;
                let input_protocol_ptr: *mut libc::c_void = &mut input_protocol as *mut _ as
                    *mut libc::c_void;

                let IOCTL_INPUT_GET_PROTOCOL =
                    make_ioctl(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 0);

                unsafe {
                    fdio_ioctl(
                        fd,
                        IOCTL_INPUT_GET_PROTOCOL,
                        ptr::null(),
                        0,
                        input_protocol_ptr,
                        mem::size_of::<i32>(),
                    );
                }
                let optional_input_handler = InputHandler::new_handler(input_protocol);
                match optional_input_handler {
                    Some(input_handler) => {
                        if let Some(handle) = InputHandler::open_handle(fd) {
                            println!("found device at {}", device_path_string);
                            let input_device = InputDevice {
                                file: file,
                                _name: device_path_string.clone(),
                                handle: handle,
                                handler: input_handler,
                            };
                            self.input_devices.push(input_device);
                        }
                    }
                    None => {}
                }
            } else {
                break;
            }
            device_index += 1;
            if device_index > 999 {
                break;
            }
        }
    }

    // pub fn wait_for_events(&mut self, event_channel: &Sender<InputEvent>) {
    //     let devices = self.input_devices.clone();
    //     let channel = event_channel.clone();
    //     thread::spawn(move || loop {
    //         let mut items: Vec<zx_wait_item_t> = devices
    //             .lock()
    //             .unwrap()
    //             .iter()
    //             .map(|one_device| {
    //                 zx_wait_item_t {
    //                     handle: one_device.handle,
    //                     waitfor: ZX_USER_SIGNAL_0,
    //                     pending: ZX_SIGNAL_NONE,
    //                 }
    //             })
    //             .collect();
    //         let result = unsafe {
    //             zx_object_wait_many(items.as_mut_ptr(), items.len() as u32, u64::max_value())
    //         };
    //         if result == 0 {
    //             let mut index = 0;
    //             let mut locked_devices = devices.lock().unwrap();
    //             for one_item in &items {
    //                 if one_item.pending & ZX_USER_SIGNAL_0 != ZX_SIGNAL_NONE {
    //                     let ref mut one_report_handler = locked_devices[index];
    //                     one_report_handler.check_for_events(&channel);
    //                 }
    //                 index += 1;
    //             }
    //         }
    //     });
    // }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub enum ButtonAction {
    NoChange,
    Down,
    Up,
}

const BUTTON_COUNT: usize = 8;

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct Cursor {
    max_x: i32,
    max_y: i32,
    pub x: i32,
    pub y: i32,
    buttons: i32,
}

pub type ButtonActions = HashMap<usize, ButtonAction>;

impl Cursor {
    pub fn new(max_x: i32, max_y: i32) -> Cursor {
        Cursor {
            max_x: max_x,
            max_y: max_y,
            x: 0,
            y: 0,
            buttons: 0,
        }
    }

    pub fn create_event(&mut self, event_source: i32, delta_x: i32, delta_y: i32, buttons: i32) {
        self.x += delta_x;
        self.x = cmp::min(self.max_x, self.x);
        self.x = cmp::max(0, self.x);
        self.y += delta_y;
        self.y = cmp::min(self.max_y, self.y);
        self.y = cmp::max(0, self.y);
        let changed_buttons = self.buttons ^ buttons;
        let mut actions = ButtonActions::new();
        let mut button_mask = 1;
        for button_index in 0..BUTTON_COUNT {
            if changed_buttons & button_mask != 0 {
                let action =
                    if buttons & button_mask != 0 { ButtonAction::Down } else { ButtonAction::Up };
                actions.insert(button_index, action);
            } else {
                actions.insert(button_index, ButtonAction::NoChange);
            }
            button_mask <<= 1;
        }

        self.buttons = buttons;

        // let mut events = vec![];
        // for (button_index, action) in actions {
        //     match action {
        //         ButtonAction::Down => {
        //             events.push(WindowEvent::MouseInput {
        //                 device_id,
        //                 state: ElementState::Pressed,
        //                 button: MouseButton::Left,
        //             })
        //         }
        //         ButtonAction::Up => {
        //             events.push(WindowEvent::MouseInput {
        //                 device_id,
        //                 state: ElementState::Released,
        //                 button: MouseButton::Left,
        //             })
        //         }
        //         ButtonAction::NoChange => {
        //             events.push(WindowEvent::MouseMoved {
        //                 device_id,
        //                 position: (self.x as f64, self.y as f64),
        //             })
        //         }
        //     }
        // }
        // events
    }
}
