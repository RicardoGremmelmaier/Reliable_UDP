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
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#define PORT 5555
#define WINDOW_SIZE 5
#define BUFFER_SIZE 1024
#define HEADER_SIZE (sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t))
#define DATA_SIZE (BUFFER_SIZE - HEADER_SIZE)
#define TIMEOUT_SEC 1
#define TIMEOUT_USEC 0

typedef struct {
    uint32_t seq_num;     
    uint16_t size;        
    uint32_t checksum;    
    char data[DATA_SIZE]; 
} packet_t;

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

uint32_t crc32(const void *data, size_t n_bytes) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *bytes = (const uint8_t *)data;

    for (size_t i = 0; i < n_bytes; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }

    return ~crc;
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
    
        //============ Realização do handshake==================
        while(connection_status < 2){
            if (strcmp(buffer, "SYN") == 0) {
                printf("Cliente requisitou conexão\n");
                connection_status = 1;   
            } else if (strcmp(buffer, "ACK") == 0) {
                printf("Cliente confirmou conexão\n");
                connection_status = 2;
            }
            if (connection_status == 1) {
                sendto(sockfd, "SYN-ACK", strlen("SYN-ACK"), 0, (const struct sockaddr *)&cli_addr, len);
            }

            int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&cli_addr, &len);
            
            if (n < 0) {
                perror("Erro ao receber dados");
                continue;
            }
            
            buffer[n] = '\0'; 
        }

        // =============Processamento da mensagem recebida==================
        printf("Mensagem recebida: %s\n", buffer);
        if (strncmp(buffer, "GET", 3) == 0) {
            char *filename = buffer + 4;
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                perror("Erro ao abrir arquivo");
                sendto(sockfd, "ERROR: Arquivo não encontrado", strlen("ERROR: Arquivo não encontrado"), 0, (const struct sockaddr *)&cli_addr, len);
                continue;
            }

            // ===============Go-back-N ARQ==================
            packet_t window[WINDOW_SIZE]; 
            uint32_t base = 0;
            uint32_t next_seq = 0;
            int eof_reached = 0;

            fd_set read_fds;
            struct timeval timeout;

            while (1) {
                
                if (!eof_reached && next_seq < base + WINDOW_SIZE) {
                    packet_t *p = &window[next_seq % WINDOW_SIZE];
                    size_t bytes_read = fread(p->data, 1, sizeof(p->data), file);

                    if (bytes_read == 0) {
                        eof_reached = 1;
                    } else {
                        p->seq_num = next_seq;
                        p->size = bytes_read;
                        p->checksum = crc32(p->data, bytes_read);

                        sendto(sockfd, p, sizeof(packet_t), 0, (const struct sockaddr *)&cli_addr, len);
                        printf("Enviado pacote %u (%zu bytes)\n", next_seq, bytes_read);

                        next_seq++;
                    }
                }

                
                FD_ZERO(&read_fds);
                FD_SET(sockfd, &read_fds);
                timeout.tv_sec = TIMEOUT_SEC;
                timeout.tv_usec = TIMEOUT_USEC;

                int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

                if (activity < 0) {
                    perror("Erro no select");
                    break;
                } else if (activity == 0) {
                    // Timeout: reenvia todos os pacotes da janela
                    printf("Timeout! Reenviando janela a partir do pacote %u\n", base);
                    for (uint32_t i = base; i < next_seq; ++i) {
                        packet_t *p = &window[i % WINDOW_SIZE];
                        sendto(sockfd, p, sizeof(packet_t), 0, (const struct sockaddr *)&cli_addr, len);
                        printf("Reenviado pacote %u\n", p->seq_num);
                    }
                } else {
                    uint32_t ack;
                    ssize_t ack_bytes = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&cli_addr, &len);
                    if (ack_bytes < 0) {
                        perror("Erro ao receber ACK");
                        continue;
                    }

                    printf("ACK recebido: %u\n", ack);

                    if (ack >= base) {
                        base = ack + 1;
                    }
                }

                // Condição de parada: EOF + todos os pacotes reconhecidos
                if (eof_reached && base == next_seq) {
                    printf("Todos os pacotes enviados e reconhecidos. Fim da transmissão.\n");
                    break;
                }
            }

        // =============Desconexão==================
        } else if (strncmp(buffer, "FIN", 3) == 0) {
            printf("Cliente desconectou\n");
            sendto(sockfd, "ACK", strlen("ACK"), 0, (const struct sockaddr *)&cli_addr, len);
            connection_status = 0; 
        } else {
            printf("Comando desconhecido\n");
        }
    }

    close(sockfd);
    return 0;
}
