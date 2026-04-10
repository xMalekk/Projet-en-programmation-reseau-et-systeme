import os
import socket
import struct
import sys
from typing import Any, Optional, Tuple

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from battle.events import encode_event, decode_event

HEADER = struct.Struct("!II")  # (type, payload_size)
IPC_MESSAGE_CONTROL = 1
IPC_MESSAGE_EVENT = 2
IPC_MESSAGE_SHUTDOWN = 3
MAX_PACKET_SIZE = 65535

class NetworkBridge:
    def __init__(self, ipc_host="127.0.0.1", ipc_port=21000, local_port=0):
        self.ipc_addr = (ipc_host, ipc_port)
        self.local_port = local_port
        self.sock: Optional[socket.socket] = None

    def connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind(("127.0.0.1", self.local_port))
            self.sock.setblocking(False)
            print(f"[bridge] UDP prêt sur {self.sock.getsockname()} -> {self.ipc_addr}")
            self._send_packet(IPC_MESSAGE_CONTROL, b"HELLO")
            return True
        except Exception as exc:
            print(f"[bridge] échec de connexion UDP: {exc}")
            if self.sock:
                self.sock.close()
            self.sock = None
            return False

    def _send_packet(self, msg_type: int, payload: bytes) -> None:
        if not self.sock:
            return
        payload = payload or b""
        header = HEADER.pack(msg_type, len(payload))
        try:
            self.sock.sendto(header + payload, self.ipc_addr)
        except Exception as exc:
            print(f"[bridge] erreur d'envoi UDP: {exc}")

    def send_event(self, event_type: str, *args: Any) -> None:
        payload = encode_event(event_type, *args)
        self._send_packet(IPC_MESSAGE_EVENT, payload)

    def send_shutdown(self) -> None:
        self._send_packet(IPC_MESSAGE_SHUTDOWN, b"shutdown")

    def receive_event(self) -> Optional[Tuple[str, ...]]:
        if not self.sock:
            return None
        try:
            data, _ = self.sock.recvfrom(MAX_PACKET_SIZE)
        except (BlockingIOError, socket.error):
            return None

        if len(data) < HEADER.size:
            return None
        msg_type, size = HEADER.unpack_from(data)
        payload = data[HEADER.size:HEADER.size + size]

        if msg_type == IPC_MESSAGE_EVENT:
            return decode_event(payload)
        if msg_type == IPC_MESSAGE_CONTROL:
            return ("CONTROL", payload.decode(errors="ignore"))
        if msg_type == IPC_MESSAGE_SHUTDOWN:
            return ("SHUTDOWN",)
        return None

    def disconnect(self) -> None:
        if self.sock:
            try:
                self.send_shutdown()
            except Exception:
                pass
            self.sock.close()
            self.sock = None
            print("[bridge] socket UDP fermée")

    def add_peer(self, ip: str, port: int = 20000) -> None:
        """Envoie un message de contrôle au démon C pour ajouter une IP distante"""
        # On formate le message sous la forme "PEER 192.168.1.50:20000"
        payload = f"PEER {ip}:{port}".encode("utf-8")
        self._send_packet(IPC_MESSAGE_CONTROL, payload)
        print(f"[bridge] Demande d'ajout du peer {ip}:{port} envoyée au démon C.")