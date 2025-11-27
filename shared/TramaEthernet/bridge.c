#include "eth.h"

int abrir_socket_promiscuo(char *device, int *idx) {
    int s;
    struct ifreq ifr;
    struct sockaddr_ll sll;
    struct packet_mreq mr; // Para configurar modo promiscuo

    if ((s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        perror("socket"); exit(1);
    }

    // 1. Obtener índice
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, IFNAMSIZ-1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl index"); exit(1); }
    *idx = ifr.ifr_ifindex;

    // 2. Bind al socket
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = *idx;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(s, (struct sockaddr *)&sll, sizeof(sll)) < 0) { perror("bind"); exit(1); }

    // 3. ACTIVAR MODO PROMISCUO (La Clave)
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = *idx;
    mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(s, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
        perror("setsockopt promisc"); 
        // No salimos, a veces falla en contenedores pero sigue funcionando si se hizo ifconfig manual
    }

    return s;
}

void forward_loop(int sock_in, int sock_out, int idx_out, char *nombre_dir) {
    byte buf[BUF_SIZ];
    int len;
    struct sockaddr_ll sll_out;
    
    memset(&sll_out, 0, sizeof(sll_out));
    sll_out.sll_family = AF_PACKET;
    sll_out.sll_ifindex = idx_out;
    sll_out.sll_halen = ETH_ALEN;

    while(1) {
        len = recvfrom(sock_in, buf, BUF_SIZ, 0, NULL, NULL);
        if (len > 0) {
            // Imprimir debug para ver que el tráfico fluye
            printf("[%s] Reenviando paquete de %d bytes\n", nombre_dir, len);
            
            if (sendto(sock_out, buf, len, 0, (struct sockaddr*)&sll_out, sizeof(sll_out)) < 0) {
                perror("Error al reenviar");
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) { printf("Uso: sudo ./bridge eth0 eth1\n"); exit(1); }
    
    int idx1, idx2;
    int s1 = abrir_socket_promiscuo(argv[1], &idx1); // eth0
    int s2 = abrir_socket_promiscuo(argv[2], &idx2); // eth1
    
    printf("Bridge activo (Promiscuo) entre %s y %s\n", argv[1], argv[2]);
    
    if (fork() == 0) forward_loop(s2, s1, idx1, "ETH1 -> ETH0"); // Respuestas (Reply)
    else forward_loop(s1, s2, idx2, "ETH0 -> ETH1");             // Preguntas (Query) y Datos
    
    return 0;
}