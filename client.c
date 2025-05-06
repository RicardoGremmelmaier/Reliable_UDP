/* ==========================Cliente========================== */
/*
 * Autor: Ricardo Marthus Gremmelmaier
 * Data: 2025-05-01
 * Versão: 1.0
*/

/* 
 * Criação de um cliente UDP simples que envia mensagens para um servidor UDP e recebe respostas.
 * O cliente deve ser capaz de enviar mensagens de controle, como "sair" ou "desconectar", e encerrar a conexão com o servidor.
 * O cliente deve ser capaz de simular perda de pacotes, informando qual pacote foi perdido
 * O cliente deve ser capaz de receber os segmentos recebidos corretamente e verificar a integridade (checksum) dos dados recebidos.
 * Após receber todos os segmentos (ou um sinal de fim), verificar se o arquivo foi recebido corretamente e, se sim, salvar o arquivo, podendo abrir o arquivo recebido.
 * Se o arquivo não foi recebido corretamente, identificar quais segmentos estão faltando ou corrompidos, informar ao usuário, solicitar
 * retransmissão dos segmentos e repetir o processo de recepção e verificação até que o arquivo esteja completo e correto.
 * O cliente deve ser capaz de lidar com mensagens de erro e retornar mensagens apropriadas ao usuário (Arquivo não encontrado, servidor indisponível).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <IP> <Porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int connection_status = 0;

    // Configuração do servidor
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

    // Handshake em 3 vias
    while (connection_status == 0) {
        
        //Timer para tentativa de conexão
        int tentativas = 0;
        int max_tentativas = 3;
        int timeout_segundos = 2;

        while (tentativas < max_tentativas) {
            sendto(sockfd, "SYN", strlen("SYN"), 0, (const struct sockaddr *)&serv_addr, len);

            // Timeout usando select()
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
    printf("Digite uma requisição (ou 'sair' para encerrar): ");
    fgets(buffer, sizeof(buffer), stdin);

    
    buffer[strcspn(buffer, "\n")] = 0;

    if (strcmp(buffer, "sair") == 0) break;

    sendto(sockfd, buffer, strlen(buffer), 0,
           (const struct sockaddr *)&serv_addr, sizeof(serv_addr));

    printf("Mensagem enviada: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
