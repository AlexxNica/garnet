#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub struct Size {
    pub width: i32,
    pub height: i32,
}

impl Size {
    pub fn _add(&self, size: Size) -> Size {
        Size {
            width: self.width + size.width,
            height: self.height + size.height,
        }
    }

    pub fn _subtract(&self, size: Size) -> Size {
        Size {
            width: self.width - size.width,
            height: self.height - size.height,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

impl Point {
    pub fn add(&self, pt: Point) -> Point {
        Point {
            x: self.x + pt.x,
            y: self.y + pt.y,
        }
    }

    pub fn subtract(&self, pt: Point) -> Point {
        Point {
            x: self.x - pt.x,
            y: self.y - pt.y,
        }
    }

    pub fn to_size(&self) -> Size {
        Size {
            width: self.x,
            height: self.y,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub struct Rectangle {
    pub origin: Point,
    pub size: Size,
}

impl Rectangle {
    pub fn empty(&self) -> bool {
        self.size.width <= 0 && self.size.height <= 0
    }
}
