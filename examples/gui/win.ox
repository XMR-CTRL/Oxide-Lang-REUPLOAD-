// Real Win32 top-level window, bound from pure Oxide via FFI. This is the
// "typed struct" form: WNDCLASSEX / MSG / PAINTSTRUCT are declared as Oxide
// structs with the exact Win64 field layout, built with struct literals, and
// passed to the C APIs by pointer — `RegisterClassExA(&wc)` decays the typed
// `&{...}` to `&u8` (C void*) implicitly, no hand-laid byte buffer needed.

extern struct HWND_tag;                   // opaque handle — never by-value
typedef HWND = *HWND_tag;

extern fn GetModuleHandleA(name: &u8) -> HWND;
extern fn LoadCursorA(hinst: HWND, name: usize) -> HWND;
extern fn RegisterClassExA(buf: &u8) -> i16;
extern fn CreateWindowExA(exstyle: i64, cls: &u8, name: &u8, style: i64,
                          x: i32, y: i32, w: i32, h: i32,
                          parent: HWND, menu: HWND, hinst: HWND, param: HWND) -> HWND;
extern fn ShowWindow(hwnd: HWND, cmd: i32) -> i32;
extern fn GetMessageA(msg: &u8, hwnd: HWND, min: i32, max: i32) -> i32;
extern fn TranslateMessage(msg: &u8) -> i32;
extern fn DispatchMessageA(msg: &u8) -> usize;
extern fn DefWindowProcA(hwnd: HWND, msg: u32, wp: usize, lp: i64) -> usize;
extern fn PostQuitMessage(code: i32);
extern fn BeginPaint(hwnd: HWND, ps: &u8) -> HWND;
extern fn EndPaint(hwnd: HWND, ps: &u8);
extern fn TextOutA(hdc: HWND, x: i32, y: i32, s: &u8, n: i32) -> i32;

const WM_DESTROY = 2;
const WM_PAINT   = 0x000F;
const COLOR_WINDOW = 5;
const CS_HREDRAW   = 2;
const CS_VREDRAW   = 1;
const IDC_ARROW    = 32512;
const SW_SHOW      = 5;
const CW_USEDEFAULT = 0x80000000;


// WNDCLASSEX on Win64 — 80 bytes. Field offsets:
//   cbSize@0 u32 | style@4 u32 | lpfnWndProc@8 usize | cbClsExtra@16 i32 |
//   cbWndExtra@20 i32 | hInstance@24 HWND | hIcon@32 HWND | hCursor@40 HWND |
//   hbrBackground@48 usize | lpszMenuName@56 usize | lpszClassName@64 usize |
//   hIconSm@72 HWND
struct WNDCLASSEX {
  cbSize: u32;
  style: u32;
  lpfnWndProc: usize;
  cbClsExtra: i32;
  cbWndExtra: i32;
  hInstance: HWND;
  hIcon: HWND;
  hCursor: HWND;
  hbrBackground: usize;
  lpszMenuName: usize;
  lpszClassName: usize;
  hIconSm: HWND;
}

// MSG on Win64 — 48 bytes:
//   hwnd@0 HWND | message@4 u32 | wParam@8 usize | lParam@16 i64 |
//   time@20 u32 | pt@24 {x:i32,y:i32}
struct MSG {
  hwnd: HWND;
  message: u32;
  wParam: usize;
  lParam: i64;
  time: u32;
  pt_x: i32;
  pt_y: i32;
}

// PAINTSTRUCT on Win64 — 64 bytes:
//   hdc@0 HWND | fErase@4 i32 | rcPaint@8 {l,t,r,b:i32} | fRestore@24 i32 |
//   fIncUpdate@28 i32 | rgbReserved@32 [u8;32]
struct PAINTSTRUCT {
  hdc: HWND;
  fErase: i32;
  rc_l: i32;
  rc_t: i32;
  rc_r: i32;
  rc_b: i32;
  fRestore: i32;
  fIncUpdate: i32;
  rgbReserved: [u8; 32];
}


fn register_window_class(hinst: HWND, class_name: &u8) -> i16 {
  let wc = WNDCLASSEX {
    cbSize: 80 as u32,
    style: (CS_HREDRAW | CS_VREDRAW) as u32,
    lpfnWndProc: ox_wndproc as i64 as usize,
    cbClsExtra: 0 as i32,
    cbWndExtra: 0 as i32,
    hInstance: hinst,
    hIcon: null as HWND,
    hCursor: LoadCursorA(null as HWND, IDC_ARROW as usize),
    hbrBackground: (COLOR_WINDOW + 1) as usize,
    lpszMenuName: 0 as usize,
    lpszClassName: (class_name as &u8) as usize,
    hIconSm: null as HWND,
  };
  // &wc (a typed struct pointer) decays to the &u8 the C API wants:
  return RegisterClassExA(&wc);
}


fn run_message_loop() {
  let mut msg = MSG {
    hwnd: null as HWND, message: 0 as u32, wParam: 0 as usize,
    lParam: 0 as i64, time: 0 as u32, pt_x: 0 as i32, pt_y: 0 as i32,
  };
  while GetMessageA(&msg, null as HWND, 0 as i32, 0 as i32) > 0 {
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
}


fn ox_wndproc(hwnd: HWND, msg: u32, wp: usize, lp: i64) -> usize {
  if msg == (WM_DESTROY as u32) {
    PostQuitMessage(0);
    return 0 as usize;
  } else if msg == (WM_PAINT as u32) {
    // BeginPaint fills the PAINTSTRUCT for us — a typed, uninitialized local is
    // exactly the C idiom `PAINTSTRUCT ps; BeginPaint(hwnd, &ps);`.
    let ps: PAINTSTRUCT;
    let hdc = BeginPaint(hwnd, &ps);
    let paint: str = "Hello from an Oxide-bound Win32 window";
    TextOutA(hdc, 16, 16, str_ptr(paint), len(paint) as i32);
    EndPaint(hwnd, &ps);
    return 0 as usize;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}


fn main() -> i64 {
  print("[oxide-gui] opening a native Win32 top-level window");

  let hinst = GetModuleHandleA(null as &u8);
  let cname: str = "OxideWindowClass";
  let atom = register_window_class(hinst, str_ptr(cname));
  if atom == 0 {
    print("[oxide-gui] RegisterClassExA failed");
    return 1;
  }

  let hwnd = CreateWindowExA(
    0,
    str_ptr(cname),
    str_ptr("Oxide + Win32"),
    (CS_HREDRAW | CS_VREDRAW) as i64,
    CW_USEDEFAULT as i32, CW_USEDEFAULT as i32,
    480, 320,
    null as HWND, null as HWND, hinst, null as HWND);

  if hwnd == (null as HWND) {
    print("[oxide-gui] CreateWindowExA failed");
    return 1;
  }
  ShowWindow(hwnd, SW_SHOW as i32);
  run_message_loop();
  print("[oxide-gui] window closed, exiting");
  return 0;
}
