// extern struct: declare opaque C-handle tag types (HWND, SOCKET, HANDLE).
// `extern struct Name;` has no fields — you only use `*Name` pointer handles,
// never a by-value instance. Combined with `typedef`, this gives distinct,
// non-interchangeable handle types like C's DECLARE_HANDLE(HWND).

extern struct HWND_tag;
extern struct SOCKET_tag;
typedef HWND   = *HWND_tag;
typedef SOCKET = *SOCKET_tag;

// An extern fn that hands back a typed handle:
extern fn CreateHwnd(seed: i64) -> HWND;

// An extern fn taking an opaque byte pointer — a typed handle pointer decays
// to it implicitly (C-style void*), no `as &u8` needed:
extern fn DemandOpaquePtr(p: &u8) -> i32;

fn main() -> i64 {
  let hwnd: HWND = CreateHwnd(7);

  // decay: *HWND_tag flows straight into an extern fn(&u8) call
  let r = DemandOpaquePtr(hwnd);

  // sizeof the handle pointer, not the opaque tag (the tag's size is undefined):
  let ptr_size = sizeof(HWND);

  print("hwnd handle present; ptr bytes =", ptr_size);
  return r as i64;
}
