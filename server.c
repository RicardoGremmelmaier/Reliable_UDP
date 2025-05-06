/* ==========================Server========================== */
/*
 * Autor: Ricardo Marthus Gremmelmaier
 * Data: 2025-05-01
 * Versão: 1.0
*/

/* 
 * Criação de um servidor UDP simples que escuta em uma porta específica e retorna os arquivos solicitados pelo cliente
 * O servidor imprime o IP local e a porta em que está escutando, e recebe mensagens de clientes, processando-as e respondendo de acordo.
 * O protocolo de aplicação será semelhante ao protocolo HTTP, onde o cliente solicita um arquivo e o servidor responde com o conteúdo do arquivo (Get filename.ext).
 * Caso o arquivo não exista, o servidor deve retornar uma mensagem de erro apropriada.
 * Para arquivos grandes (> 1MB), o servidor deve dividir o arquivo em pacotes de 1024 bytes e enviar os pacotes sequencialmente, aguardando a confirmação do cliente 
 * Cada segmento de pacote deve conter um cabeçalho customizado com informações como número do segmento, tamanho do pacote, checksum e outros dados relevantes.
 * O servidor deve ser capaz de lidar com múltiplos clientes simultaneamente (ou apenas retornar um erro caso não consiga).
 * O servidor deve ser capaz de lidar com mensagens de erro e retornar mensagens apropriadas ao cliente.
 * O servidor deve ser capaz de lidar com mensagens de controle, como "sair" ou "desconectar", e encerrar a conexão com o cliente.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#define PORT 12345
#define BUFFER_SIZE 1024

void print_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, "eth0") == 0) {

            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), ip, INET_ADDRSTRLEN);
            printf("Servidor rodando no IP: %s:%d\n", ip, PORT);
        }
    }

    freeifaddrs(ifaddr);
}

int handshake(char buffer[], int connection_status) {
    if (strcmp(buffer, "SYN") == 0) {
        printf("Cliente requisitou conexão\n");
        return 1;   
    } else if (strcmp(buffer, "ACK") == 0) {
        printf("Cliente confirmou conexão\n");
        return 2;
    }
    return 0;
}

int main() {
    int sockfd;
    struct sockaddr_in serv_addr, cli_addr;
    char buffer[BUFFER_SIZE];
    socklen_t len;
    int connection_status = 0;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro no bind");
        exit(EXIT_FAILURE);
    }

    print_local_ip();

    while (1) {
        len = sizeof(cli_addr);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                         (struct sockaddr *)&cli_addr, &len);
        if (n < 0) {
            perror("Erro ao receber dados");
            continue;
        }
        buffer[n] = '\0'; 

        // Realização do handshake
        while(connection_status < 2){
            connection_status = handshake(buffer, connection_status);
            if (connection_status == 1) {
                sendto(sockfd, "SYN-ACK", strlen("SYN-ACK"), 0, (const struct sockaddr *)&cli_addr, len);
            }

            int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                (struct sockaddr *)&cli_addr, &len);
            if (n < 0) {
            perror("Erro ao receber dados");
            continue;
            }
            buffer[n] = '\0'; 
        }


    }

    close(sockfd);
    return 0;
}
