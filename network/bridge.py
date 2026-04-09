import socket
import os
import sys

from battle.events import encode_event, decode_event

class NetworkBridge:
    def __init__(self, host="127.0.0.1", port=12345):
        self.addr = (host, port)
        self.sock = None

    def connect(self):
        """
        En UDP, on prépare juste la socket.
        """
        try:
            # socket.SOCK_DGRAM = UDP
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # On met un timeout de 0 pour que receive_event ne bloque pas le jeu
            self.sock.setblocking(False)
            print(f"Socket UDP prête pour {self.addr}")
            return True
        except Exception as e:
            print(f"Erreur socket UDP : {e}")
            return False

    def send_event(self, event_type, *args):
        """
        Envoie un message type "MOVE,1,10,20" en UDP.
        """
        if not self.sock:
            return

        try:
            # On utilise ta nouvelle fonction encode avec les virgules
            message = encode_event(event_type, *args)
            self.sock.sendto(message, self.addr)
        except Exception as e:
            print(f"Erreur d'envoi UDP : {e}")

    def receive_event(self):
        """
        Écoute si un paquet UDP est arrivé.
        """
        if not self.sock:
            return None

        try:
            # On lit le paquet (1024 octets max, bien assez pour une ligne de texte)
            data, _ = self.sock.recvfrom(1024)
            # On utilise ta nouvelle fonction decode
            return decode_event(data)
        except (BlockingIOError, socket.error):
            # Rien n'est arrivé, on continue la boucle du jeu
            return None

    def disconnect(self):
        if self.sock:
            self.sock.close()
            print("Socket UDP fermée.")