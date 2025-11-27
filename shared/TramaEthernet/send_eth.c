#include "eth.h"

int main(int argc, char *argv[])
{
  int sockfd, i, iLen, iLenHeader, iLenTotal, iIndex;
  /*Buffer en donde estara almacenada la trama*/
  byte sbBufferEther[BUF_SIZ]; 
  
  /* sbMac ya no se usa para convertir argv[2]. 
     Ahora usamos sbMacBroadcast para la consulta y sbMacDestino para los datos */
  byte sbMacBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  byte sbMacDestino[6];
  int bMacEncontrada = 0;
  char my_hostname[64]; /* Para autodetectar nuestro nombre de host */
  ssize_t numbytes;
  struct sockaddr saddr; /* Para la funcion recvfrom */
  int saddr_size;

  /*Para facilitar, hay una estructura para la cabecera Ethernet*/
  /*La cabecera Ethernet (psehHeaderEther) y sbBufferEther apuntan a lo mismo*/
  struct ether_header *psehHeaderEther = (struct ether_header *)sbBufferEther;
  struct sockaddr_ll socket_address;
  /*Esta estructura permite solicitar datos a la interface*/
  struct ifreq sirDatos;
  /*Mensaje a enviar*/
  char scMsj[] = "Comunicacion Exitosa!"              ;

  /* Se espera un nombre de host, no una MAC */
  if (argc != 3)
  {
    printf("Error en argumentos.\n\n");
    printf("seth INTERFACE HOSTNAME-DESTINO (Ej. pc2).\n");
    printf("Ejemplo: send_eth eth0 pc2\n\n");
    exit(1);
  }

  /*Apartir de este este punto, argv[1] = Nombre de la interfaz y argv[2] */
  /*contiene el HOSTNAME destino.                                         */
  /*Abre el socket. Para que sirven los parametros empleados?*/
  if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    perror("socket");

  /* Mediante el nombre de la interface (i.e. eth0), se obtiene su indice */
  memset(&sirDatos, 0, sizeof(struct ifreq));
  for (i = 0; argv[1][i]; i++)
    sirDatos.ifr_name[i] = argv[1][i];
  if (ioctl(sockfd, SIOCGIFINDEX, &sirDatos) < 0)
    perror("SIOCGIFINDEX");
  iIndex = sirDatos.ifr_ifindex;

  /*Ahora obtenemos la MAC de la interface por donde saldran los datos */
  memset(&sirDatos, 0, sizeof(struct ifreq));
  for (i = 0; argv[1][i]; i++)
    sirDatos.ifr_name[i] = argv[1][i];
  if (ioctl(sockfd, SIOCGIFHWADDR, &sirDatos) < 0)
    perror("SIOCGIFHWADDR");

  /*Se imprime la MAC del host*/
  printf("Iterface de salida: %u, con MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
         (byte)(iIndex),
         (byte)(sirDatos.ifr_hwaddr.sa_data[0]), (byte)(sirDatos.ifr_hwaddr.sa_data[1]),
         (byte)(sirDatos.ifr_hwaddr.sa_data[2]), (byte)(sirDatos.ifr_hwaddr.sa_data[3]),
         (byte)(sirDatos.ifr_hwaddr.sa_data[4]), (byte)(sirDatos.ifr_hwaddr.sa_data[5]));

  /* Obtener nuestro propio nombre de host */
  if (gethostname(my_hostname, sizeof(my_hostname)) == -1)
  {
    perror("gethostname");
    exit(1);
  }
  printf("Mi nombre de host: %s\n", my_hostname);

  /* Paso 1: Enviar paquete de consulta (Broadcast) === */
  
  printf("Enviando mensaje MAC para host: %s\n", argv[2]);
  
  memset(sbBufferEther, 0, BUF_SIZ); /*Llenamos con 0 el buffer*/

  /*Direccion MAC Origen (Nuestra MAC) */
  memcpy(psehHeaderEther->ether_shost, sirDatos.ifr_hwaddr.sa_data, 6);
  /*Direccion MAC destino (Broadcast) */
  memcpy(psehHeaderEther->ether_dhost, sbMacBroadcast, 6);

  /*Tipo de protocolo o la longitud del paquete*/
  psehHeaderEther->ether_type = htons(ETHER_TYPE);
  iLenHeader = sizeof(struct ether_header);

  /* Construimos el payload de la consulta */
  sbBufferEther[iLenHeader] = 'Q'; /* 'Q' de consulta */
  strcpy((char *)&sbBufferEther[iLenHeader + 1], argv[2]); /* Hostname a buscar */

  /* Rellenamos con espacios */
  i = 1 + strlen(argv[2]) + 1; /* +1 por el byte 'Q' */
  while (i < ETHER_TYPE)
  {
    sbBufferEther[iLenHeader + i] = ' ';
    i++;
  }
  iLenHeader = iLenHeader + i; /* iLenHeader ahora es iLenHeader + ETHER_TYPE */

  /*Finalmente FCS (4 bytes en 0) */
  for (i = 0; i < 4; i++)
    sbBufferEther[iLenHeader + i] = 0;
  iLenTotal = iLenHeader + 4; /*Longitud total*/

  /*Procedemos al envio de la trama de consulta*/
  socket_address.sll_ifindex = iIndex;
  socket_address.sll_halen = ETH_ALEN;
  memcpy(socket_address.sll_addr, sbMacBroadcast, 6); /* Direccion broadcast */

  iLen = sendto(sockfd, sbBufferEther, iLenTotal, 0, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll));
  if (iLen < 0)
    printf("Send failed (Query)\n");

  /*Paso 2: Esperar paquete de respusta (Unicast) === */

  printf("Esperando respuesta MAC de %s...\n", argv[2]);
  
  /* La cabecera (eh) y sbBufferEther apuntan a lo mismo */
  struct ether_header *eh_recv = (struct ether_header *)sbBufferEther;
  
  while (!bMacEncontrada)
  {
    saddr_size = sizeof saddr;
    numbytes = recvfrom(sockfd, sbBufferEther, BUF_SIZ, 0, &saddr, (socklen_t *)(&saddr_size));
    
    if (numbytes < 0) {
      perror("recvfrom");
      continue;
    }

    /* 1. ¿La trama es para mi? (Verifica MAC destino) */
    if (iLaTramaEsParaMi(sbBufferEther, &sirDatos))
    {
      iLenHeader = sizeof(struct ether_header);
      
      /* 2. ¿Es un paquete de respuesta 'R'? */
      if (sbBufferEther[iLenHeader] == 'R')
      {
        /* 3. ¿Es del host que estoy buscando? */
        if (strcmp((char *)&sbBufferEther[iLenHeader + 1], argv[2]) == 0)
        {
          printf("Respuesta recibida de %s.\n", (char *)&sbBufferEther[iLenHeader + 1]);
          /* MAC encontrada! La copiamos desde la cabecera (MAC origen del reply) */
          memcpy(sbMacDestino, eh_recv->ether_shost, 6);
          bMacEncontrada = 1;
          printf("MAC de %s: %02x:%02x:%02x:%02x:%02x:%02x\n",
                 argv[2],
                 sbMacDestino[0], sbMacDestino[1], sbMacDestino[2],
                 sbMacDestino[3], sbMacDestino[4], sbMacDestino[5]);
        }
      }
    }
  }
  /* Paso 3: Enviar paquete de datos (Unicast) === */

  /* Ahora se construye la trama Ethernet de datos */
  memset(sbBufferEther, 0, BUF_SIZ); /*Llenamos con 0 el buffer de datos (payload)*/
  
  /*Direccion MAC Origen (Nuestra MAC) */
  memcpy(psehHeaderEther->ether_shost, sirDatos.ifr_hwaddr.sa_data, 6);
  
  /*Direccion MAC destino (La MAC que encontramos) */
  memcpy(psehHeaderEther->ether_dhost, sbMacDestino, 6);

  /*Tipo de protocolo o la longitud del paquete*/
  psehHeaderEther->ether_type = htons(ETHER_TYPE);
  
  iLenHeader = sizeof(struct ether_header);
  
  /* Validacion del mensaje original */
  if (strlen(scMsj) > ETHER_TYPE - 1) 
  {
    printf("El mensaje debe ser mas corto o incremente ETHER_TYPE\n");
    close(sockfd);
    exit(1);
  }
  
  /* Construimos el payload de Datos */
  sbBufferEther[iLenHeader] = 'D'; /* 'D' de Data */
  strcpy((char *)&sbBufferEther[iLenHeader + 1], scMsj);

  /* Rellenamos con espacios */
  i = 1 + strlen(scMsj) + 1; 
  while (i < ETHER_TYPE)
  {
    sbBufferEther[iLenHeader + i] = ' ';
    i++;
  }

  iLenHeader = iLenHeader + i; /* iLenHeader ahora es iLenHeader + ETHER_TYPE */

  /*Finalmente FCS*/
  for (i = 0; i < 4; i++)
    sbBufferEther[iLenHeader + i] = 0;
  iLenTotal = iLenHeader + 4; /*Longitud total*/

  /*Procedemos al envio de la trama de DATOS */
  socket_address.sll_ifindex = iIndex;
  socket_address.sll_halen = ETH_ALEN;
  /* Apuntamos a la MAC de destino encontrada */
  memcpy(socket_address.sll_addr, sbMacDestino, 6);

  iLen = sendto(sockfd, sbBufferEther, iLenTotal, 0, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll));
  if (iLen < 0)
    printf("Send failed (Data)\n");
    
  printf("\nContenido de la trama de DATOS enviada:\n\n");
  vImprimeTrama(sbBufferEther);
  /*Cerramos*/
  close(sockfd);
  return 0;
}