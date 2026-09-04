


extern fn MessageBoxA(hwnd: i64, text: &u8, caption: &u8, utype: i32) -> i32;

const MB_OK = 0;

fn main() -> i64 {
  print("[oxide-gui] calling Win32 MessageBoxA via extern FFI");
  let title: str = "Oxide says hello";
  let body: str = "This dialog is a native Win32 MessageBox, called directly from Oxide through extern + --link user32. No C shim, no callback, no message loop.";
  let rc = MessageBoxA(0, str_ptr(body), str_ptr(title), MB_OK as i32);
  print("[oxide-gui] MessageBox returned", rc);
  return 0;
}
