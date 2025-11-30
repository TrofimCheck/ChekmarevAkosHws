#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>

#include "garden_common.h"

static SharedData *shared = NULL;
static SharedData *shared_global = NULL; // для обработчика сигнала
static sem_t *cell_sem[MAX_M][MAX_N]; // массив указателей на семафоры клеток

// Обработчик сигнала SIGINT (Ctrl+C)
static void sigint_handler(int sig) {
    (void)sig;
    if (shared_global) {
        shared_global->stop = 1;
        printf("\nGardener 1: SIGINT caught, stopping...\n");
        fflush(stdout);
    }
}

// Обработка одной клетки садовником.
// sh - указатель на разделяемые данные,
// i, j - координаты клетки,
// gardener_id - номер садовника.
// С учётом семафора клетки гарантируется взаимное исключение.
static void work_on_cell(SharedData *sh, int i, int j, int gardener_id) {
    if (sh->stop) return;

    if (sh->cell_state[i][j] == CELL_OBSTACLE) {
        sleep_us(sh->pass_time_us);
        return;
    }

    sem_t *s = cell_sem[i][j];
    if (sem_wait(s) == -1) {
        perror("sem_wait");
        return;
    }

    if (sh->stop) {
        sem_post(s);
        return;
    }

    if (sh->cell_state[i][j] == CELL_EMPTY) {
        sh->cell_state[i][j] = (gardener_id == 1) ? CELL_DONE1 : CELL_DONE2;
        if (gardener_id == 1) sh->processed1++;
        else sh->processed2++;

        printf("Gardener 1 processed cell (%d,%d)\n", i, j);
        fflush(stdout);

        if (gardener_id == 1)
            sleep_us(sh->work_time1_us);
        else
            sleep_us(sh->work_time2_us);
    } else {
        // Уже обработана
        printf("Gardener 1 skipped cell (%d,%d), state=%d\n", i, j, sh->cell_state[i][j]);
        fflush(stdout);
        sleep_us(sh->pass_time_us);
    }

    sem_post(s);
}

// Основной цикл обхода сада садовником 1.
static void gardener1_loop(SharedData *sh) {
    int M = sh->M;
    int N = sh->N;

    for (int i = 0; i < M && !sh->stop; ++i) {
        if (i % 2 == 0) {
            // слева направо
            for (int j = 0; j < N && !sh->stop; ++j) {
                work_on_cell(sh, i, j, 1);
            }
        } else {
            // справа налево
            for (int j = N - 1; j >= 0 && !sh->stop; --j) {
                work_on_cell(sh, i, j, 1);
            }
        }
    }

    printf("Gardener 1 finished\n");
    fflush(stdout);
    sh->finished1 = 1;
}

// Точка входа процесса садовника 1.
int main(void) {
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open (gardener1)");
        fprintf(stderr, "Gardener 1: run garden_ctl first.\n");
        return 1;
    }

    shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap (gardener1)");
        close(shm_fd);
        return 1;
    }

    shared_global = shared;

    // SIGINT обработчик
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction (gardener1)");
    }

    printf("Gardener 1: waiting for controller to initialize...\n");
    fflush(stdout);

    while (!shared->initialized && !shared->stop) {
        sleep(1);
    }

    if (shared->stop) {
        printf("Gardener 1: stop flag set before start, exiting.\n");
        munmap(shared, sizeof(SharedData));
        close(shm_fd);
        return 0;
    }

    int M = shared->M;
    int N = shared->N;

    // Открываем семафоры клеток.
    char sem_name[64];
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            make_cell_sem_name(sem_name, sizeof(sem_name), i, j);
            cell_sem[i][j] = sem_open(sem_name, 0);
            if (cell_sem[i][j] == SEM_FAILED) {
                perror("sem_open cell (gardener1)");
                munmap(shared, sizeof(SharedData));
                close(shm_fd);
                return 1;
            }
        }
    }

    printf("Gardener 1: started.\n");
    fflush(stdout);

    gardener1_loop(shared);

    // Закрываем семафоры и shared memory
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            if (cell_sem[i][j])
                sem_close(cell_sem[i][j]);
        }
    }

    munmap(shared, sizeof(SharedData));
    close(shm_fd);

    return 0;
}
