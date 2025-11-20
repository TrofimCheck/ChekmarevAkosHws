// shared-memory-server.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

#include "message.h"

const char *shar_object = "posix-shar-object";

static volatile sig_atomic_t stop_flag = 0;
static message_t *g_msg = NULL; // нужен в обработчике сигналов

void sys_err(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Обработчик сигналов для сервера
void handle_signal(int signo) {
    stop_flag = 1;

    if (g_msg != NULL) {
        // Сообщаем клиенту о завершении обмена
        g_msg->type = MSG_TYPE_FINISH;

        // Если остановили с клавиатуры, посылаем SIGTERM клиенту
        if (signo == SIGINT) {
            pid_t client = g_msg->client_pid;
            if (client > 0) {
                kill(client, SIGTERM);
            }
        }
    }
}

int main(void) {
    int shmid; // дескриптор объекта памяти
    message_t *msg_p; // адрес сообщения в разделяемой памяти

    // Создаем/открываем объект разделяемой памяти
    if ((shmid = shm_open(shar_object, O_CREAT | O_RDWR, 0666)) == -1) {
        sys_err("server: shm_open");
    } else {
        printf("Server: object is open: name = %s, id = 0x%x\n", shar_object, shmid);
    }

    // Задаем размер объекта памяти под нашу структуру
    if (ftruncate(shmid, sizeof(message_t)) == -1) {
        sys_err("server: ftruncate");
    } else {
        printf("Server: memory size set = %lu\n", (unsigned long)sizeof(message_t));
    }

    // Получаем доступ к памяти
    msg_p = mmap(0, sizeof(message_t), PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);
    if (msg_p == MAP_FAILED) {
        sys_err("server: mmap");
    }

    g_msg = msg_p; // чтобы обработчик сигналов имел доступ

    // Инициализация структуры
    msg_p->type = MSG_TYPE_EMPTY;
    msg_p->server_pid = getpid(); // запоминаем PID сервера

    // Устанавливаем обработчики сигналов
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        sys_err("server: sigaction SIGINT");
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        sys_err("server: sigaction SIGTERM");
    }

    printf("Server: PID = %d\n", getpid());
    printf("Server: waiting for numbers...\n");

    // Основной цикл обработки сообщений
    while (!stop_flag) {
        if (msg_p->type == MSG_TYPE_NUMBER) {
            // В буфере есть число
            printf("Server: received number = %s\n", msg_p->string);
            fflush(stdout);

            msg_p->type = MSG_TYPE_EMPTY; // число обработано
        } else if (msg_p->type == MSG_TYPE_FINISH) {
            // Клиент попросил завершить обмен
            printf("Server: got FINISH message, exiting...\n");
            break;
        }

        usleep(100000);
    }

    // Перед завершением сервер тоже помечает обмен как завершенный
    msg_p->type = MSG_TYPE_FINISH;

    // Освобождаем ресурсы
    if (munmap(msg_p, sizeof(message_t)) == -1) {
        sys_err("server: munmap");
    }

    if (close(shmid) == -1) {
        sys_err("server: close");
    }

    // Сервер удаляет сегмент разделяемой памяти
    if (shm_unlink(shar_object) == -1) {
        sys_err("server: shm_unlink");
    } else {
        printf("Server: shared memory segment removed.\n");
    }

    printf("Server: exit.\n");
    return 0;
}
