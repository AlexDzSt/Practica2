/*Escucha todos los paquetes ethernet que llegan, pero se*/
/*desea el que le corresponde*/
#include "eth.h"

int main(int argc, char *argv[])
{
  int sockfd, i, iTramaLen;
  ssize_t numbytes;
  byte sbBufferEther[BUF_SIZ];
  /*La cabecera Ethernet (eh) y sbBufferEther apuntan a lo mismo*/
  struct ether_header *eh = (struct ether_header *)sbBufferEther;
  int saddr_size;
  struct sockaddr saddr;
  struct ifreq sirDatos;
  int iEtherType;

  /* Variables para responder a las consultas */
  char my_hostname[64];
  int iIndex;
  int iLenHeader, iLenTotal;

  if (argc != 2)
  {
    printf("Error en argumentos.\n\n");
    printf("ethdump INTERFACE\n");
    printf("Ejemplo: recv_eth eth0\n\n");
    exit(1);
  }
  /*Apartir de este este punto, argv[1] = Nombre de la interfaz.          */

  /*Podriamos recibir tramas de nuestro "protocolo" o de cualquier protocolo*/
  /*sin embargo, evitaremos esto y recibiremos de todo. Hay que tener cuidado*/
  /*if ((sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETHER_TYPE))) == -1)*/
  /*Se abre el socket para "escuchar" todo sin pasar por al CAR*/
  if ((sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
  {
    perror("Listener: socket");
    return -1;
  }

  /*Ahora obtenemos la MAC de la interface del host*/
  memset(&sirDatos, 0, sizeof(struct ifreq));
  for (i = 0; argv[1][i]; i++)
    sirDatos.ifr_name[i] = argv[1][i];
  if (ioctl(sockfd, SIOCGIFHWADDR, &sirDatos) < 0)
    perror("SIOCGIFHWADDR");

  /*Se imprime la MAC del host*/
  printf("Direccion MAC de la interfaz de entrada: %02x:%02x:%02x:%02x:%02x:%02x\n",
         (byte)(sirDatos.ifr_hwaddr.sa_data[0]), (byte)(sirDatos.ifr_hwaddr.sa_data[1]),
         (byte)(sirDatos.ifr_hwaddr.sa_data[2]), (byte)(sirDatos.ifr_hwaddr.sa_data[3]),
         (byte)(sirDatos.ifr_hwaddr.sa_data[4]), (byte)(sirDatos.ifr_hwaddr.sa_data[5]));

  /* Obtener nuestro nombre de host e indice de interfaz */
  if (gethostname(my_hostname, sizeof(my_hostname)) == -1)
  {
    perror("gethostname");
    exit(1);
  }
  printf("Mi nombre de host: %s\n", my_hostname);

  /* Tambien necesitamos el indice de la interfaz para poder ENVIAR respuestas */
  struct ifreq sirDatosIndex;
  memset(&sirDatosIndex, 0, sizeof(struct ifreq));
  strncpy(sirDatosIndex.ifr_name, argv[1], IFNAMSIZ - 1);
  if (ioctl(sockfd, SIOCGIFINDEX, &sirDatosIndex) < 0)
  {
    perror("SIOCGIFINDEX (en recv)");
    exit(1);
  }
  iIndex = sirDatosIndex.ifr_ifindex;

  /*Se mantiene en escucha*/
  do
  { /*Capturando todos los paquetes*/
    saddr_size = sizeof saddr;
    iTramaLen = recvfrom(sockfd, sbBufferEther, BUF_SIZ, 0, &saddr, (socklen_t *)(&saddr_size));
    
    if (iTramaLen < 0) {
        perror("recvfrom");
        continue;
    }

    /* Logica para procesar el tipo de trama */
    
    iLenHeader = sizeof(struct ether_header);
    char packetType = sbBufferEther[iLenHeader];
    char *payload = (char *)&sbBufferEther[iLenHeader + 1];

    /* Paso 1: Verificamos si es un broadcast (FF:FF:FF:FF:FF:FF) */
    int bEsBroadcast = 1;
    for (i = 0; i < 6; i++)
    {
      if ((byte)eh->ether_dhost[i] != 0xFF)
        bEsBroadcast = 0;
    }

    /* Paso 2: Procesar si es una consulta ('Q') y es broadcast */
    if (packetType == 'Q' && bEsBroadcast)
    {
      /* Paso 3. Vemos si el nombre en el payload es el nuestro */
      if (strcmp(payload, my_hostname) == 0)
      {
        printf("El mensaje es para mi (%s). Enviando respuesta...\n", my_hostname);

        /* Es para nosotros. Debemos enviar una respuesta 'R' */
        byte sbReplyBuffer[BUF_SIZ];
        struct ether_header *eh_reply = (struct ether_header *)sbReplyBuffer;
        struct sockaddr_ll socket_address_reply;

        memset(sbReplyBuffer, 0, BUF_SIZ);

        /* Destino: La MAC de quien pregunto (MAC Origen de la consulta) */
        memcpy(eh_reply->ether_dhost, eh->ether_shost, 6);
        /* Origen: Nuestra MAC */
        memcpy(eh_reply->ether_shost, sirDatos.ifr_hwaddr.sa_data, 6);
        /* Tipo/Longitud */
        eh_reply->ether_type = htons(ETHER_TYPE);

        /* Payload de la respuesta */
        sbReplyBuffer[iLenHeader] = 'R'; /* 'R' de Respuesta */
        strcpy((char *)&sbReplyBuffer[iLenHeader + 1], my_hostname); /* Nuestro nombre */

        /* Relleno */
        i = 1 + strlen(my_hostname) + 1;
        while (i < ETHER_TYPE)
        {
          sbReplyBuffer[iLenHeader + i] = ' ';
          i++;
        }
        iLenTotal = iLenHeader + ETHER_TYPE + 4; /* +4 de FCS */

        /* Preparamos la direccion para sendto */
        socket_address_reply.sll_ifindex = iIndex;
        socket_address_reply.sll_halen = ETH_ALEN;
        memcpy(socket_address_reply.sll_addr, eh->ether_shost, 6); /* MAC de destino (quien pregunto) */
        
        /* Enviar la respuesta */
        if (sendto(sockfd, sbReplyBuffer, iLenTotal, 0, (struct sockaddr *)&socket_address_reply, sizeof(struct sockaddr_ll)) < 0)
        {
          printf("Send failed (Reply)\n");
        }
      }
    }
    /* Paso 4: Procesar si es un paquete de Datos ('D') y es para nuestra MAC */
    else if (packetType == 'D' && iLaTramaEsParaMi(sbBufferEther, &sirDatos))
    {
      printf("\nContenido de la trama de DATOS recibida:\n");
      vImprimeTrama(sbBufferEther);
    }
    /* Si es 'R' o 'Q' para otro host, o 'D' para otra MAC, simplemente se ignora */
    
    /* === FIN CAMBIO === */

  } while (1);
  close(sockfd);
  return (0);
} 