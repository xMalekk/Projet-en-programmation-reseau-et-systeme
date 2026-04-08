import json

# Types d'événements 
JOIN = "J"
UNIT_SPAWN = "SPAW"
UNIT_MOVE = "MOVE"
UNIT_ATTACK = "ATTACK"
PROJECTILE_SPAWN = "PS"
SHUTDOWN = "SHUT"

def encode_event(payload):
    """Prépare les données en JSON pour le payload IPC."""
    return json.dumps(payload).encode('utf-8')

def decode_event(json_bytes):
    """Transforme les octets reçus en dictionnaire Python."""
    try:
        return json.loads(json_bytes.decode('utf-8'))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None