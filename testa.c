#include "demon.c" // On inclut ton fichier (ou on le compilera ensemble)
#include <stdio.h>

// Définition temporaire des fonctions de log pour éviter les erreurs
void log_error(const char* fmt, ...) { printf("[ERROR] %s\n", fmt); }
void log_warn(const char* fmt, ...) { printf("[WARN] %s\n", fmt); }
void log_info(const char* fmt, ...) { printf("[INFO] %s\n", fmt); }

int main() {
    IPCConnection *conn = ipc_connection_create("127.0.0.1:21000");
    if (!conn) return 1;

    printf("Démon lancé ! En attente de Python...\n");

while (1) {
        IPCMessage msg;
        int status = ipc_connection_poll(conn, &msg, 1000); 
        if (status > 0) {
            printf("Message reçu ! Type: %d\n", msg.type);

            // --- AJOUT : RÉPONSE À PYTHON ---
            IPCMessage rep;
            rep.type = 2; // IPC_MESSAGE_EVENT
            char *texte = "Bien reçu !";
            rep.payload = texte;
            rep.payload_size = strlen(texte);
            
            ipc_connection_send(conn, &rep);
            printf("Réponse envoyée à Python\n");
            // --------------------------------

            ipc_message_free(&msg);
        }
    }
    ipc_connection_send(conn, &(IPCMessage){.type = IPC_MESSAGE_SHUTDOWN});
    ipc_connection_destroy(conn);
    return 0;
}