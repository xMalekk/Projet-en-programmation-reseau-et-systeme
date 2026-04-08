import socket
import struct
import json
import os
# On importe les constantes numériques (UNIT_MOVE, SHUTDOWN, etc.)
from .events import *

class NetworkBridge:
    def __init__(self, socket_path="/tmp/medievail_net.sock"):
        self.socket_path = socket_path
        self.client = None

    def connect(self):
        """Établit la connexion avec le démon C."""
        if not os.path.exists(self.socket_path):
            print(f"Erreur : La socket {self.socket_path} est introuvable.")
            return False
        
        try:
            self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.client.connect(self.socket_path)
            # Mode non-bloquant pour ne pas figer le rendu du jeu
            self.client.setblocking(False)
            print("Connecté au démon réseau avec succès.")
            return True
        except Exception as e:
            print(f"Échec de la connexion IPC : {e}")
            return False

    def send_event(self, event_type, payload):
        """
        Envoie un message au format :
        [Header: 4 octets Type][Header: 4 octets Taille] + [Payload: JSON]
        """
        if not self.client:
            return

        # On encode les données en JSON
        json_payload = json.dumps(payload).encode('utf-8')
        payload_size = len(json_payload)

        # On prépare le header binaire (II = deux entiers 32 bits non signés)
        # Conforme au uint32_t header[2] du code C
        header = struct.pack("II", event_type, payload_size)

        try:
            self.client.sendall(header + json_payload)
        except Exception as e:
            print(f"Erreur d'envoi réseau : {e}")

    def receive_event(self):
        
        if not self.client:
            return None

        try:
            # ÉTAPE 1 : Lire le header (Type + Taille)
            header_raw = self.client.recv(8)
            if not header_raw:
                return None

            # On décode les deux entiers (I = 4 octets)
            event_type, size = struct.unpack("II", header_raw)

            # ÉTAPE 2 : Lire le JSON si la taille est > 0
            if size > 0:
                payload_raw = self.client.recv(size)
                # On utilise ta fonction de events.py pour décoder
                data = decode_event(payload_raw)
                return event_type, data
            
            return event_type, {}

        except (BlockingIOError, socket.error):
            # C'est ici que le côté "non-bloquant" agit :
            # S'il n'y a rien sur la socket, on ne crash pas, on renvoie juste None
            return None

    def disconnect(self):
        """Ferme proprement la connexion."""
        if self.client:
            # On prévient le démon qu'on part
            self.send_event(SHUTDOWN, {"reason": "Python client exit"})
            self.client.close()
            self.client = None
            print("Déconnecté du réseau.")
