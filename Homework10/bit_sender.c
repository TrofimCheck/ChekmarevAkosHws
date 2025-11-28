#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t ack_received = 0;
static pid_t peer_pid = -1;

void ack_handler(int signo) {
    (void)signo;
    // Пришло подтверждение от приёмника
    ack_received = 1;
}

int main(void) {
    printf("Sender PID: %d\n", getpid());
    printf("Enter receiver PID: ");
    fflush(stdout);

    int tmp_pid;
    if (scanf("%d", &tmp_pid) != 1) {
        fprintf(stderr, "Invalid PID\n");
        return 1;
    }
    peer_pid = (pid_t)tmp_pid;

    // Устанавливаем обработчик для подтверждений (SIGUSR1)
    struct sigaction sa;
    sa.sa_handler = ack_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    printf("Enter integer to send: ");
    fflush(stdout);

    int32_t value;
    if (scanf("%d", &value) != 1) {
        fprintf(stderr, "Invalid number\n");
        return 1;
    }

    uint32_t uvalue = (uint32_t)value;
    const int TOTAL_BITS = 32;

    // Маска для блокировки SIGUSR1
    sigset_t block_mask, old_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGUSR1);

    printf("Sending %d as 32 bits...\n", value);

    for (int i = TOTAL_BITS - 1; i >= 0; --i) {
        int bit = (uvalue >> i) & 1;
        int sig_to_send = (bit == 0) ? SIGUSR1 : SIGUSR2;

        // Блокируем SIGUSR1 и обнуляем флаг подтверждения
        if (sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1) {
            perror("sigprocmask");
            return 1;
        }

        ack_received = 0;

        // Отправляем бит
        if (kill(peer_pid, sig_to_send) == -1) {
            perror("kill (send bit)");
            // Разблокируем сигналы перед выходом
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return 1;
        }

        // Ждём подтверждения SIGUSR1
        while (!ack_received) {
            // Временно устанавливаем старую маску, где SIGUSR1 разблокирован
            sigsuspend(&old_mask);
            // sigsuspend возвращается только после сигнала
        }

        // Восстанавливаем старую маску сигналов
        if (sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1) {
            perror("sigprocmask restore");
            return 1;
        }
    }

    printf("All bits sent. Sending SIGINT to finish.\n");

    // Сообщаем приёмнику о завершении передачи
    if (kill(peer_pid, SIGINT) == -1) {
        perror("kill (SIGINT)");
        return 1;
    }

    printf("Sender done.\n");
    return 0;
}
