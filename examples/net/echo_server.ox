import "net.ox";



fn echo_loop(client: i64) {

  let buf: [u8; 4096];
  let mut guard = 0;
  while guard < 1000000 {
    let n = recv(client, &buf[0], 4096 as i32, 0 as i32);
    if n < (0 as i32) {

      let e = last_error();
      if e == (WSAECONNRESET as i32) || e == (WSAECONNABORTED as i32) {
        print("[net] client disconnected (reset)");
      } else {
        print("[net] recv failed WSAGetLastError=", e);
      }
      return;
    }
    if n == (0 as i32) {

      print("[net] client closed connection");
      return;
    }
    let mut remaining = n as i64;
    let mut off = 0;
    while remaining > 0 {
      let w = send(client, &buf[off], remaining as i32, 0 as i32);
      if w < (0 as i32) {
        print("[net] send failed WSAGetLastError=", last_error());
        return;
      }
      remaining = remaining - (w as i64);
      off = off + (w as i64);
    }
    guard = guard + 1;
  }
}


fn main() -> i64 {
  let rc = wsastartup();
  if rc != (0 as i32) {
    print("[net] WSAStartup failed rc=", rc, " err=", last_error());
    return 1;
  }
  print("[oxide net] winsock initialised");

  let port: i64 = 0x4F4F;
  print("[oxide net] listening on 0.0.0.0:", port);

  let listener = listen_on(port, 16 as i32);
  if listener == INVALID_SOCKET {
    print("[oxide net] could not start listener");
    WSACleanup();
    return 1;
  }

  let mut served = 0;
  while served < 4 {
    let peer: [u8; 16];
    let mut alen: i32 = 16 as i32;
    let client = accept(listener, &peer[0], &alen);
    if client == INVALID_SOCKET {
      print("[net] accept failed WSAGetLastError=", last_error());
      continue;
    }
    print("[oxide net] client connected id=", client);

    echo_loop(client);

    closesocket(client);
    served = served + 1;
    print("[oxide net] served", served, "connection(s), looping");
  }

  closesocket(listener);
  WSACleanup();
  print("[oxide net] done, shutting down");
  return 0;
}
