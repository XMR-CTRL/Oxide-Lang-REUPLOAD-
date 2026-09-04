// Interactive Win32 GUI from pure Oxide: a top-level window that hosts a child
// BUTTON, a child EDIT (text input), and a custom-drawn area. Clicking the
// button echoes the edit field's text into the drawn area and increments a
// counter, which is repainted on demand with InvalidateRect.
//
// This exercises the parts `win.ox` left untouched:
//   - child controls created with CreateWindowExA ("BUTTON"/"EDIT" window classes)
//   - WM_COMMAND (button click: wParam low word = control id, hi word = BN_CLICKED)
//   - GetWindowTextA reading a child control's text into an Oxide buffer
//   - SetWindowTextA pushing Oxide text into a control
//   - WM_SIZE relayout of children when the parent is resized
//   - InvalidateRect triggering a WM_PAINT repaint (classic invalidate/redraw cycle)
//   - DrawTextA formatting text inside a rect
//
// Build:  oxide exe examples/gui/controls.ox --link user32 --link gdi32

extern struct HWND_tag;
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
extern fn DrawTextA(hdc: HWND, s: &u8, n: i32, rc: &u8, flags: u32) -> i32;
extern fn InvalidateRect(hwnd: HWND, rc: &u8, erase: i32);
extern fn GetWindowTextA(hwnd: HWND, buf: &u8, maxlen: i32) -> i32;
extern fn GetWindowTextLengthA(hwnd: HWND) -> i32;
extern fn SetWindowTextA(hwnd: HWND, s: &u8) -> i32;
extern fn SetFocus(hwnd: HWND) -> HWND;
extern fn GetClientRect(hwnd: HWND, rc: &u8);
extern fn MoveWindow(hwnd: HWND, x: i32, y: i32, w: i32, h: i32, repaint: i32) -> i32;

const WM_DESTROY  = 2;
const WM_PAINT    = 0x000F;
const WM_COMMAND  = 0x0111;
const WM_SIZE     = 0x0005;
const COLOR_WINDOW = 5;
const CS_HREDRAW   = 2;
const CS_VREDRAW   = 1;
const IDC_ARROW    = 32512;
const SW_SHOW      = 5;
const CW_USEDEFAULT = 0x80000000;

// Window styles
const WS_OVERLAPPEDWINDOW = 0x00CF0000;
const WS_CHILD             = 0x40000000;
const WS_VISIBLE           = 0x10000000;
const WS_BORDER            = 0x00800000;

// Button / edit styles
const BS_DEFPUSHBUTTON = 0x0001;
const ES_AUTOHSCROLL   = 0x0080;

// WM_COMMAND helpers
const BN_CLICKED = 0;

// Control ids (low word of wParam on WM_COMMAND)
const IDC_EDIT   = 101;
const IDC_BUTTON = 102;

// DrawTextA flags
const DT_LEFT      = 0x00000020;
const DT_TOP       = 0x00000000;
const DT_WORDBREAK = 0x00000010;

// Global mutable state reachable from the wndproc callback. The message pump +
// callback model needs shared state somewhere; these are the module-level
// mutable handles/values the wndproc reads and writes.
//
// The child HWND handles are stored as plain i64 (zero-initialized) rather than
// the typed HWND, because a typed global needs a compile-time-constant
// initializer and `null as HWND` does not fold to one for a pointer type. i64
// zero is a constant; the handle is cast back to HWND at the Win32 call sites.
let mut g_edit: i64 = 0;
let mut g_button: i64 = 0;
let mut g_clicks: i64 = 0;
// The text currently shown in our custom-drawn area.
let mut g_echo: str = "Type something, then click Echo.";


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

struct MSG {
  hwnd: HWND;
  message: u32;
  wParam: usize;
  lParam: i64;
  time: u32;
  pt_x: i32;
  pt_y: i32;
}

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

// RECT for GetClientRect / DrawTextA: left/top/right/bottom, 32-bit each, 16B.
struct RECT {
  left: i32;
  top: i32;
  right: i32;
  bottom: i32;
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
  return RegisterClassExA(&wc);
}


// Lay out the child controls inside the parent's client area. The drawn area is
// the band below the edit+button row. Called on creation and on WM_SIZE.
fn layout_children(hwnd: HWND) {
  let rc: RECT;
  GetClientRect(hwnd, &rc);
  let cw = rc.right;
  let ch = rc.bottom;
  let pad: i32 = 8;
  let row_h: i32 = 28;
  let btn_w: i32 = 90;

  // Edit field: top row, fills width minus button + padding.
  let edit_w = cw - btn_w - pad * 3;
  MoveWindow(g_edit as HWND, pad, pad, edit_w, row_h, 1 as i32);

  // Button: top row, right side.
  MoveWindow(g_button as HWND, pad + edit_w + pad, pad, btn_w, row_h, 1 as i32);
  // (the rest of the client area is left for our own WM_PAINT drawing)
}


// Read the EDIT control's text into an Oxide string. GetWindowTextA fills a
// fixed scratch buffer and NUL-terminates it; we then walk the leading
// NUL-terminated run, promoting each byte to a char and concatenating with
// `char_to_str` + `+` (string concat is runtime-supported and const-folds for
// literals). No vec or vec_ptr needed — a stack `[u8; N]` buffer is enough.
fn read_edit_text() -> str {
  let n = GetWindowTextLengthA(g_edit as HWND);
  if n <= 0 { return ""; }
  let mut buf: [u8; 4096];
  // Zero the scratch buffer so a short copy still terminates cleanly.
  let mut z = 0;
  while z < 4096 {
    buf[z] = 0 as u8;
    z = z + 1;
  }
  let got = GetWindowTextA(g_edit as HWND, &buf[0], 4096);
  if got <= 0 { return ""; }

  let mut s: str = "";
  let mut k = 0;
  while k < (got as i64) {
    let b = buf[k];
    if b == (0 as u8) { break; }
    s = s + char_to_str(b as char);
    k = k + 1;
  }
  return s;
}


// The click-counter string uses the runtime `itos` builtin (int -> str) so we
// don't re-roll decimal formatting by hand.
fn click_str(n: i64) -> str {
  return itos(n);
}


fn ox_wndproc(hwnd: HWND, msg: u32, wp: usize, lp: i64) -> usize {
  if msg == (WM_DESTROY as u32) {
    PostQuitMessage(0);
    return 0 as usize;
  }

  if msg == (WM_SIZE as u32) {
    layout_children(hwnd);
    // Fall through to DefWindowProc (no special return value needed).
    return DefWindowProcA(hwnd, msg, wp, lp);
  }

  if msg == (WM_COMMAND as u32) {
    // wp low word = control id, hi word = notification code.
    let ctl = (wp & 0xFFFF) as i64;
    let code = ((wp >> 16) & 0xFFFF) as i64;
    if ctl == IDC_BUTTON && code == BN_CLICKED {
      g_clicks = g_clicks + 1;
      let typed = read_edit_text();
      if len(typed) > 0 {
        g_echo = "You typed: " + typed + "  (clicks: " + click_str(g_clicks) + ")";
      } else {
        g_echo = "(empty input)  clicks: " + click_str(g_clicks);
      }
      // Repaint our drawn band.
      InvalidateRect(hwnd, null as &u8, 1 as i32);
      return 0 as usize;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
  }

  if msg == (WM_PAINT as u32) {
    let ps: PAINTSTRUCT;
    let hdc = BeginPaint(hwnd, &ps);

    // Draw the echo text in the band below the controls, word-wrapped.
    let rc: RECT;
    GetClientRect(hwnd, &rc);
    let rcl = 8 as i32;
    let rct = 44 as i32;
    let rcr = (rc.right - 8) as i32;
    let rcb = (rc.bottom - 8) as i32;
    let draw_rc = RECT { left: rcl, top: rct, right: rcr, bottom: rcb };
    DrawTextA(hdc, str_ptr(g_echo), len(g_echo) as i32, &draw_rc,
              (DT_LEFT | DT_WORDBREAK) as u32);

    EndPaint(hwnd, &ps);
    return 0 as usize;
  }

  return DefWindowProcA(hwnd, msg, wp, lp);
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


fn main() -> i64 {
  print("[oxide-gui] opening an interactive Win32 window (edit + button + custom draw)");

  let hinst = GetModuleHandleA(null as &u8);
  let cname: str = "OxideControlsClass";
  let atom = register_window_class(hinst, str_ptr(cname));
  if atom == 0 {
    print("[oxide-gui] RegisterClassExA failed");
    return 1;
  }

  let hwnd = CreateWindowExA(
    0,
    str_ptr(cname),
    str_ptr("Oxide + Win32 Controls"),
    WS_OVERLAPPEDWINDOW as i64,
    CW_USEDEFAULT as i32, CW_USEDEFAULT as i32,
    520, 360,
    null as HWND, null as HWND, hinst, null as HWND);

  if hwnd == (null as HWND) {
    print("[oxide-gui] CreateWindowExA failed");
    return 1;
  }

  // Child EDIT control. WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL. The
  // "menu" parameter (HMENU) is repurposed as the control id for child windows.
  g_edit = CreateWindowExA(
    0, str_ptr("EDIT"), str_ptr(""),
    (WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL) as i64,
    0, 0, 0, 0,
    hwnd, IDC_EDIT as HWND, hinst, null as HWND) as i64;

  // Child BUTTON control.
  g_button = CreateWindowExA(
    0, str_ptr("BUTTON"), str_ptr("Echo"),
    (WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON) as i64,
    0, 0, 0, 0,
    hwnd, IDC_BUTTON as HWND, hinst, null as HWND) as i64;

  // Seed the edit box with placeholder text.
  SetWindowTextA(g_edit as HWND, str_ptr("hello oxide"));
  SetFocus(g_edit as HWND);

  layout_children(hwnd);
  ShowWindow(hwnd, SW_SHOW as i32);
  run_message_loop();

  print("[oxide-gui] window closed after", g_clicks, "button click(s)");
  return 0;
}
