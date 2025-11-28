#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t bit_received_flag = 0;
static volatile sig_atomic_t last_bit = 0;
static volatile sig_atomic_t done = 0;
static pid_t peer_pid = -1;

// Обработчик бита 0 (SIGUSR1 от передатчика)
void bit0_handler(int signo) {
    (void)signo;
    last_bit = 0;
    bit_received_flag = 1;
    if (peer_pid > 0) {
        // отправляем подтверждение
        kill(peer_pid, SIGUSR1);
    }
}

// Обработчик бита 1 (SIGUSR2 от передатчика)
void bit1_handler(int signo) {
    (void)signo;
    last_bit = 1;
    bit_received_flag = 1;
    if (peer_pid > 0) {
        // отправляем подтверждение
        kill(peer_pid, SIGUSR1);
    }
}

// Обработчик завершения передачи (SIGINT)
void finish_handler(int signo) {
    (void)signo;
    done = 1;
}

int main(void) {
    printf("Receiver PID: %d\n", getpid());
    printf("Enter sender PID: ");
    fflush(stdout);

    int tmp_pid;
    if (scanf("%d", &tmp_pid) != 1) {
        fprintf(stderr, "Invalid PID\n");
        return 1;
    }
    peer_pid = (pid_t)tmp_pid;

    // Настраиваем обработчики сигналов
    struct sigaction sa0, sa1, safin;

    sa0.sa_handler = bit0_handler;
    sigemptyset(&sa0.sa_mask);
    sa0.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa0, NULL) == -1) {
        perror("sigaction SIGUSR1");
        return 1;
    }

    sa1.sa_handler = bit1_handler;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR2, &sa1, NULL) == -1) {
        perror("sigaction SIGUSR2");
        return 1;
    }

    safin.sa_handler = finish_handler;
    sigemptyset(&safin.sa_mask);
    safin.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &safin, NULL) == -1) {
        perror("sigaction SIGINT");
        return 1;
    }

    const int TOTAL_BITS = 32;
    uint32_t value = 0;
    int bits_received = 0;

    // Блокируем рабочие сигналы и будем ждать их через sigsuspend
    sigset_t block_mask, old_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGUSR1);
    sigaddset(&block_mask, SIGUSR2);
    sigaddset(&block_mask, SIGINT);

    if (sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1) {
        perror("sigprocmask");
        return 1;
    }

    printf("Receiver: waiting for bits...\n");

    while (!done && bits_received < TOTAL_BITS) {
        // Ждём, пока не придёт бит или SIGINT
        while (!bit_received_flag && !done) {
            sigsuspend(&old_mask); // временно разблокирует сигналы
        }

        if (done) {
            break;
        }

        if (bit_received_flag) {
            value = (value << 1) | (last_bit & 1);
            bits_received++;
            bit_received_flag = 0;
        }
    }

    // Восстанавливаем исходную маску
    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1) {
        perror("sigprocmask restore");
        return 1;
    }

    int32_t result = (int32_t)value;
    printf("Receiver: got %d (0x%08x), bits = %d\n", result, value, bits_received);
    printf("Receiver done.\n");

    return 0;
}
