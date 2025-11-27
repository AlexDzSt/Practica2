#include "eth.h"

/* Tabla de Enrutamiento Estática 
   Devuelve el ID de la interfaz donde se encuentra el host.
   0 = eth0 (Red A: pc1, pc2)
   1 = eth1 (Red B: pc4, pc5)
   -1 = Desconocido
*/
int obtener_red_destino(char *hostname) {
    if (strcmp(hostname, "pc1") == 0) return 0;
    if (strcmp(hostname, "pc2") == 0) return 0;
    
    if (strcmp(hostname, "pc4") == 0) return 1;
    if (strcmp(hostname, "pc5") == 0) return 1;
    
    return -1;
}

int abrir_socket_promiscuo(char *device, int *idx) {
    int s;
    struct ifreq ifr;
    struct sockaddr_ll sll;
    struct packet_mreq mr;

    if ((s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        perror("socket"); exit(1);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, IFNAMSIZ-1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl index"); exit(1); }
    *idx = ifr.ifr_ifindex;

    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = *idx;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(s, (struct sockaddr *)&sll, sizeof(sll)) < 0) { perror("bind"); exit(1); }

    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = *idx;
    mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(s, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
        perror("setsockopt promisc"); 
    }

    return s;
}

/* Lógica de Capa 3:
   idx_in: ID de la interfaz por donde entró el paquete (0 o 1)
*/
void procesar_y_enrutar(int sock_in, int sock_out, int idx_out, int id_red_entrada, char *nombre_debug) {
    byte buf[BUF_SIZ];
    int len;
    struct sockaddr_ll sll_out;
    
    /* Punteros para diseccionar el paquete */
    struct ether_header *eh;
    char *payload;
    char tipo_paquete;
    char hostname[50];
    int red_objetivo;
    int reenviar = 0; // Bandera de decisión

    memset(&sll_out, 0, sizeof(sll_out));
    sll_out.sll_family = AF_PACKET;
    sll_out.sll_ifindex = idx_out;
    sll_out.sll_halen = ETH_ALEN;

    while(1) {
        len = recvfrom(sock_in, buf, BUF_SIZ, 0, NULL, NULL);
        if (len > 0) {
            eh = (struct ether_header *)buf;
            
            // Solo nos interesa nuestro protocolo (0x0100)
            if (ntohs(eh->ether_type) == ETHER_TYPE) {
                
                payload = (char *)(buf + sizeof(struct ether_header));
                tipo_paquete = payload[0];
                
                // Extraemos el nombre del host (saltando el byte de tipo)
                // Usamos sscanf o strcpy simple asumiendo formato correcto del payload
                strcpy(hostname, payload + 1);

                reenviar = 0; // Por defecto bloqueamos

                if (tipo_paquete == 'Q') {
                    /* Lógica para QUERY (Pregunta) */
                    red_objetivo = obtener_red_destino(hostname);
                    
                    // Si el destino es conocido Y está en la OTRA red -> ENRUTAR
                    if (red_objetivo != -1 && red_objetivo != id_red_entrada) {
                        printf("[%s] [L3] Query para %s (Red %d). ENRUTANDO.\n", nombre_debug, hostname, red_objetivo);
                        reenviar = 1;
                    } else {
                        printf("[%s] [L3] Query para %s (Local o Desconocido). DESCARTANDO.\n", nombre_debug, hostname);
                    }
                }
                else if (tipo_paquete == 'R') {
                    /* Lógica para REPLY (Respuesta) */
                    // El payload trae el nombre de QUIEN responde.
                    // Si la respuesta viene de la red actual, debe cruzar a la otra.
                    red_objetivo = obtener_red_destino(hostname);
                    
                    // Si el host que responde pertenece a la red de entrada, 
                    // asumimos que la pregunta vino del otro lado. Enrutamos.
                    if (red_objetivo == id_red_entrada) {
                         printf("[%s] [L3] Reply de %s. ENRUTANDO respuesta.\n", nombre_debug, hostname);
                         reenviar = 1;
                    }
                }
                else if (tipo_paquete == 'D') {
                    /* Lógica para DATA (Datos) */
                    // Limitación del protocolo: El paquete 'D' NO tiene nombre de destino.
                    // Actuamos como switch unicast para mantener la conexión.
                    printf("[%s] [Data] Paquete de datos. Reenviando transparente.\n", nombre_debug);
                    reenviar = 1; 
                }

                if (reenviar) {
                    if (sendto(sock_out, buf, len, 0, (struct sockaddr*)&sll_out, sizeof(sll_out)) < 0) {
                        perror("Error forwarding");
                    }
                }

            } else {
                // Tráfico no perteneciente a nuestro protocolo (IPv6, ARP normal, etc)
                // Lo ignoramos o lo dejamos pasar si queremos transparencia total
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) { printf("Uso: sudo ./router eth0 eth1\n"); exit(1); }
    
    int idx1, idx2;
    int s1 = abrir_socket_promiscuo(argv[1], &idx1); // eth0 (Red A)
    int s2 = abrir_socket_promiscuo(argv[2], &idx2); // eth1 (Red B)
    
    printf("ROUTER L3 Activo. \nRed A (eth0): pc1, pc2\nRed B (eth1): pc4, pc5\n");
    
    /* id_red: 0 para eth0, 1 para eth1.
       Esto ayuda a la función a saber "de dónde" viene el paquete.
    */

    if (fork() == 0) {
        // Hijo: Escucha en eth1 (Red B) -> Envía a eth0
        procesar_y_enrutar(s2, s1, idx1, 1, "B->A"); 
    } else {
        // Padre: Escucha en eth0 (Red A) -> Envía a eth1
        procesar_y_enrutar(s1, s2, idx2, 0, "A->B"); 
    }
    
    return 0;
}