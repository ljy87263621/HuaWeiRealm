import socket


def main():
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 8888))
    server.listen(1)
    client, _ = server.accept()
    pending = b""
    try:
        while True:
            data = client.recv(4096)
            if not data:
                break
            pending += data
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                command = line.decode("utf-8")
                if command.startswith("udpport "):
                    client.sendall("MSG:UDP registered\n".encode())
                elif command.startswith("AUTH "):
                    client.sendall("AUTH:OK ID 1 USER alice\n".encode())
                elif command == "list":
                    client.sendall(
                        "MSG:ID 2 IP 127.0.0.1 TCP:12345 UDP:45678\n".encode()
                    )
                elif command == "emojis":
                    client.sendall("EMOJI_BEGIN wave.txt 4\nDATA".encode())
                    client.sendall("EMOJI_END\n".encode())
                elif command.startswith("send "):
                    client.sendall(
                        ("BROADCAST:" + command[5:] + "\n").encode()
                    )
    finally:
        client.close()
        server.close()


if __name__ == "__main__":
    main()
