// shared-memory-client.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>

#include "message.h"

const char *shar_object = "posix-shar-object";

static volatile sig_atomic_t stop_flag = 0;
static message_t *g_msg = NULL; // нужен в обработчике сигналов

#define GEN_MIN 0
#define GEN_MAX 999

void sys_err(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Обработчик сигналов для клиента
void handle_signal(int signo) {
    stop_flag = 1;

    if (g_msg != NULL) {
        // Сообщаем серверу о завершении обмена
        g_msg->type = MSG_TYPE_FINISH;

        // Если нас остановили с клавиатуры, посылаем SIGTERM серверу
        if (signo == SIGINT) {
            pid_t server = g_msg->server_pid;
            if (server > 0) {
                kill(server, SIGTERM);
            }
        }
    }
}

int main(void) {
    int shmid; // дескриптор разделяемой памяти
    message_t *msg_p; // адрес сообщения в разделяемой памяти

    // Открываем объект разделяемой памяти
    if ((shmid = shm_open(shar_object, O_CREAT | O_RDWR, 0666)) == -1) {
        sys_err("client: shm_open");
    } else {
        printf("Client: object is open: name = %s, id = 0x%x\n", shar_object, shmid);
    }

    // Получаем доступ к памяти
    msg_p = mmap(0, sizeof(message_t), PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);
    if (msg_p == MAP_FAILED) {
        sys_err("client: mmap");
    }

    g_msg = msg_p; // чтобы обработчик сигналов имел доступ

    // Запоминаем PID клиента
    msg_p->client_pid = getpid();

    // Устанавливаем обработчики сигналов
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        sys_err("client: sigaction SIGINT");
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        sys_err("client: sigaction SIGTERM");
    }

    printf("Client: PID = %d\n", getpid());
    printf("Client: generating random numbers in [%d, %d]...\n", GEN_MIN, GEN_MAX);

    // Инициализируем генератор случайных чисел
    srand((unsigned int)(time(NULL) ^ getpid()));

    // Основной цикл: автоматически генерируем числа и пишем в разделяемую память
    while (!stop_flag) {
        // Если сервер попросил завершить обмен, выходим
        if (msg_p->type == MSG_TYPE_FINISH) {
            printf("Client: got FINISH message from server, exiting...\n");
            break;
        }

        // Пишем новое число, только если предыдущие данные уже прочитаны
        if (msg_p->type == MSG_TYPE_EMPTY) {
            int value = GEN_MIN + rand() % (GEN_MAX - GEN_MIN + 1);

            snprintf(msg_p->string, MAX_STRING, "%d", value);
            msg_p->type = MSG_TYPE_NUMBER;

            printf("Client: sent number = %s\n", msg_p->string);
            fflush(stdout);
        }

        usleep(500000); // 500 ms
    }

    // Перед завершением помечаем обмен как завершенный
    msg_p->type = MSG_TYPE_FINISH;

    // Освобождение ресурсов
    if (munmap(msg_p, sizeof(message_t)) == -1) {
        sys_err("client: munmap");
    }

    if (close(shmid) == -1) {
        sys_err("client: close");
    }

    printf("Client: exit.\n");
    return 0;
}
