/* ==========================Cliente========================== */
/*
 * Autor: Ricardo Marthus Gremmelmaier
 * Data: 2025-05-01
 * Versão: 1.0
*/

/* 
 * Criação de um cliente UDP simples que envia mensagens para um servidor UDP e recebe respostas.
 * O cliente deve ser capaz de enviar mensagens de controle, como "FIN" ou "GET", e encerrar a conexão com o servidor.
 * O cliente deve ser capaz de simular perda de pacotes, informando qual pacote foi perdido
 * O cliente deve ser capaz de receber os segmentos recebidos corretamente e verificar a integridade (checksum) dos dados recebidos.
 * Após receber todos os segmentos (ou um sinal de fim), verificar se o arquivo foi recebido corretamente e, se sim, salvar o arquivo, podendo abrir o arquivo recebido.
 * Se o arquivo não foi recebido corretamente, identificar quais segmentos estão faltando ou corrompidos, informar ao usuário, solicitar
 * retransmissão dos segmentos e repetir o processo de recepção e verificação até que o arquivo esteja completo e correto.
 * O cliente deve ser capaz de lidar com mensagens de erro e retornar mensagens apropriadas ao usuário (Arquivo não encontrado, servidor indisponível).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <stdbool.h>

#define BUFFER_SIZE 1024
#define HEADER_SIZE (sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t))
#define DATA_SIZE (BUFFER_SIZE - HEADER_SIZE)
#define LOSS_PROBABILITY 0.1

typedef struct {
    uint32_t seq_num;     
    uint16_t size;        
    uint32_t checksum;    
    char data[DATA_SIZE]; 
} packet_t;

bool should_drop_packet() {
    return ((float)rand() / RAND_MAX) < LOSS_PROBABILITY;
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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <IP> <Porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int connection_status = 0;
    srand(time(NULL));

    // ============Configuração do servidor==================
    const char *server_ip = argv[1];
    int PORT = atoi(argv[2]);

    int sockfd;
    struct sockaddr_in serv_addr;
    socklen_t len = sizeof(serv_addr);
    char buffer[BUFFER_SIZE];

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr(server_ip);

    // ==============Handshake em 3 vias==================
    while (connection_status == 0) {
        
        int tentativas = 0;
        int max_tentativas = 3;
        int timeout_segundos = 2;

        while (tentativas < max_tentativas) {
            sendto(sockfd, "SYN", strlen("SYN"), 0, (const struct sockaddr *)&serv_addr, len);

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            struct timeval timeout;
            timeout.tv_sec = timeout_segundos;
            timeout.tv_usec = 0;

            int resultado = select(sockfd + 1, &fds, NULL, NULL, &timeout);
            if (resultado == -1) {
                perror("select");
                exit(EXIT_FAILURE);
            } else if (resultado == 0) {
                printf("Tentativa %d: servidor não respondeu (timeout de %d segundos).\n", tentativas + 1, timeout_segundos);
                tentativas++;
            } else {
                int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&serv_addr, &len);
                if (n < 0) {
                    perror("Erro ao receber resposta do servidor");
                    close(sockfd);
                    exit(EXIT_FAILURE);
                }
                buffer[n] = '\0';

                if (strcmp(buffer, "SYN-ACK") == 0) {
                    printf("Servidor aceitou conexão\n");
                    connection_status = 1;
                    sendto(sockfd, "ACK", strlen("ACK"), 0, (const struct sockaddr *)&serv_addr, len);
                    break;
                }
            }
        }
        if (tentativas == max_tentativas) {
            printf("Erro: não foi possível estabelecer conexão com o servidor.\n");
            exit(EXIT_FAILURE);
        }
    }

    while (1) {
        printf("Digite uma requisição GET filename.ext (ou 'FIN' para encerrar): ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;
    
        if (strcmp(buffer, "FIN") == 0) {
            sendto(sockfd, "FIN", strlen("FIN"), 0, (const struct sockaddr *)&serv_addr, len);
            printf("Conexão encerrada pelo cliente.\n");
            break;
        }
    
        if (strncmp(buffer, "GET ", 4) != 0) {
            printf("Formato inválido. Use: GET filename.ext\n");
            continue;
        }
    
        char *filename = buffer + 4;
    
        char output_filename[256];
        char *dot = strrchr(filename, '.');
        if (dot) {
            size_t name_len = dot - filename;
            snprintf(output_filename, sizeof(output_filename), "%.*s_recebido%s", (int)name_len, filename, dot);
        } else {
            snprintf(output_filename, sizeof(output_filename), "%s_recebido", filename);
        }
    
        FILE *output = fopen(output_filename, "wb");
        if (output == NULL) {
            perror("Erro ao criar arquivo de saída");
            continue;
        }
    
        sendto(sockfd, buffer, strlen(buffer), 0, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    
        uint32_t expected_seq = 0;
    
        // ================Implementação do protocolo Go-Back-N==================
        while (1) {
            packet_t packet;
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sockfd, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 3;  
            timeout.tv_usec = 0;

            int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
            if (activity == 0) {
                printf("Erro: timeout esperando pacote do servidor. Conexão pode ter sido perdida.\n");
            
                if (output) {
                    fclose(output);
                    output = NULL;
                }
            
                remove(output_filename);
                close(sockfd);  
                exit(EXIT_FAILURE);  
            }

            int n = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&serv_addr, &len);
            if (n < 0) {
                perror("Erro ao receber pacote");
                break;
            }
    
            if (strncmp((char *)&packet, "ERROR", 5) == 0) {
                printf("%s\n", (char *)&packet);
                if (output) {
                    fclose(output);
                    output = NULL;
                }
            
                remove(output_filename);
                break;
            }

            if (should_drop_packet() && packet.seq_num != 0) {
                printf("Simulação: pacote %u perdido (dropado artificialmente)\n", packet.seq_num);
                continue;  
            }
    
            uint32_t calc_checksum = crc32(packet.data, packet.size);
            if (packet.checksum != calc_checksum) {
                printf("Checksum incorreto no pacote %u. Ignorando...\n", packet.seq_num);
                continue;
            }
    
            if (packet.seq_num == expected_seq) {
                fwrite(packet.data, 1, packet.size, output);
    
                
                sendto(sockfd, &expected_seq, sizeof(expected_seq), 0, (struct sockaddr *)&serv_addr, len);
                expected_seq++;
    
                if (packet.size < DATA_SIZE) {
                    printf("Fim do arquivo alcançado.\n");
                    printf("Arquivo salvo como '%s'.\n", output_filename);
                    break;
                }
            } else {
                printf("Esperava pacote %u, mas recebi %u. Solicitando retransmissão...\n", expected_seq, packet.seq_num);
                uint32_t ack = (expected_seq > 0) ? expected_seq - 1 : 0;
                sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, len);
            }
        }
    
        if (output) {
            fclose(output);
            output = NULL;
        }
    }
    
    close(sockfd);
    return 0;
}
