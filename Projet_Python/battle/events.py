import json

# Types d'événements 
JOIN = "J"
UNIT_SPAWN = "SPAWN"
UNIT_MOVE = "MOVE"
UNIT_ATTACK = "ATTACK"
PROJECTILE_SPAWN = "PS"
SHUTDOWN = "SHUT"

def encode_event(event_type, *args):
    """
    Prépare une chaîne type : TYPE,arg1,arg2,arg3
    Exemple : encode_event(UNIT_MOVE, 1, 150, 200) -> "MOVE,1,150,200"
    """
    # On transforme tous les arguments en string et on les joint par des virgules
    payload = ",".join(map(str, args))
    message = f"{event_type},{payload}"
    return message.encode('utf-8')

def decode_event(data_bytes):
    """
    Décode une chaîne type : TYPE,val1,val2
    Retourne une liste : [TYPE, val1, val2, ...]
    """
    try:
        message = data_bytes.decode('utf-8')
        # On découpe simplement par les virgules
        parts = message.split(',')
        return parts
    except Exception as e:
        print(f"Erreur de décodage : {e}")
        return None