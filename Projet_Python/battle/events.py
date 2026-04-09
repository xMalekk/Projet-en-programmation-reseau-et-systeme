import json
from typing import Any, Dict

# Types d'événements logiques (côté gameplay)
JOIN = "JOIN"
UNIT_SPAWN = "UNIT_SPAWN"
UNIT_MOVE = "UNIT_MOVE"
UNIT_ATTACK = "UNIT_ATTACK"
PROJECTILE_SPAWN = "PROJECTILE_SPAWN"
SHUTDOWN = "SHUTDOWN"


def encode_event(event_type: str, *args: Any) -> bytes:
    """
    Sérialise un événement en JSON. Pour rester simple, on utilise le format
    {"type": event_type, "args": list(args)} et on renvoie les bytes UTF-8.
    """
    payload: Dict[str, Any] = {
        "type": event_type,
        "args": list(args),
    }
    return json.dumps(payload).encode("utf-8")


def decode_event(data_bytes: bytes):
    """
    Fait l'inverse : bytes -> dict Python {"type": str, "args": [...]}
    Retourne None si le JSON est invalide.
    """
    try:
        payload = json.loads(data_bytes.decode("utf-8"))
        event_type = payload.get("type")
        args = payload.get("args", [])
        if isinstance(args, list):
            return (event_type, *args)
        return (event_type, args)
    except Exception as exc:
        print(f"Erreur de décodage d'événement: {exc}")
        return None
