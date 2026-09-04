



extern fn WSAStartup(wVersionRequested: i64, lpWSAData: &u8) -> i32;
extern fn WSACleanup() -> i32;
extern fn WSAGetLastError() -> i32;

extern fn socket(af: i32, t: i32, proto: i32) -> i64;
extern fn bind(s: i64, name: &u8, namelen: i32) -> i32;
extern fn listen(s: i64, backlog: i32) -> i32;
extern fn accept(s: i64, addr: &u8, addrlen: &i32) -> i64;
extern fn connect(s: i64, name: &u8, namelen: i32) -> i32;
extern fn recv(s: i64, buf: &u8, len: i32, flags: i32) -> i32;
extern fn send(s: i64, buf: &u8, len: i32, flags: i32) -> i32;
extern fn closesocket(s: i64) -> i32;
extern fn inet_addr(cp: &u8) -> i32;


const AF_INET    = 2;
const SOCK_STREAM = 1;
const IPPROTO_TCP = 6;

const INVALID_SOCKET = -1;
const SOCKET_ERROR   = -1;
const SOMAXCONN      = 0x7fffffff;

const WSAECONNRESET   = 10054;
const WSAECONNABORTED = 10053;



let WSA_BACKING: [u8; 512];


fn wsastartup() -> i32 {

  let version: i64 = (2 << 8) | 2;
  return WSAStartup(version, &WSA_BACKING[0]);
}


fn last_error() -> i32 {
  return WSAGetLastError();
}


fn htons(port: i64) -> i16 {

  let hi: i16 = (port >> 8) as i16;
  let lo: i16 = (port & 0xFF) as i16;
  return (lo << 8) | hi;
}


struct sockaddr_in {
  sin_family: i16;
  sin_port:   i16;
  sin_addr:   i32;
  sin_zero:   [u8; 8];
}


fn build_addr_any(buf: &u8, port: i64) {
  let family: &i16 = buf as &i16;
  mmio_store(family, AF_INET as i16);
  let portp: &i16 = (buf as &u8 + 2) as &i16;
  mmio_store(portp, htons(port));
  let addrp: &i32 = (buf as &u8 + 4) as &i32;
  mmio_store(addrp, 0);
  let z: &u8 = buf as &u8 + 8;
  let mut i: i64 = 0;
  while i < 8 {
    mmio_store(z + i, 0);
    i = i + 1;
  }
}


fn new_tcp_socket() -> i64 {
  return socket(AF_INET as i32, SOCK_STREAM as i32, IPPROTO_TCP as i32);
}


fn listen_on(port: i64, backlog: i32) -> i64 {
  let s = new_tcp_socket();
  if s == INVALID_SOCKET { return INVALID_SOCKET; }

  let addr: [u8; 16];
  build_addr_any(&addr[0], port);

  let bound = bind(s, &addr[0], 16 as i32);
  if bound == (SOCKET_ERROR as i32) {
    print("[net] bind failed WSAGetLastError=", last_error());
    closesocket(s);
    return INVALID_SOCKET;
  }

  let listening = listen(s, backlog);
  if listening == (SOCKET_ERROR as i32) {
    print("[net] listen failed WSAGetLastError=", last_error());
    closesocket(s);
    return INVALID_SOCKET;
  }
  return s;
}
