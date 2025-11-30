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
#include <time.h>

#include "garden_common.h"

static SharedData *shared = NULL;
static int shm_fd = -1;

// Вывод итогового состояния сада и статистики по обработанным клеткам.
static void print_garden(SharedData *sh) {
    int M = sh->M;
    int N = sh->N;

    printf("Final garden state (M=%d, N=%d):\n", M, N);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            char c;
            switch (sh->cell_state[i][j]) {
                case CELL_EMPTY: c = '.'; break;
                case CELL_OBSTACLE: c = '#'; break;
                case CELL_DONE1: c = '1'; break;
                case CELL_DONE2: c = '2'; break;
                default: c = '?'; break;
            }
            printf("%c ", c);
        }
        printf("\n");
    }
    printf("Processed by gardener 1: %d\n", sh->processed1);
    printf("Processed by gardener 2: %d\n", sh->processed2);
}

// Обработчик сигнала SIGINT (Ctrl+C).
static void sigint_handler(int sig) {
    (void)sig;
    if (shared) {
        shared->stop = 1;
        printf("\nController: SIGINT caught, stopping...\n");
        fflush(stdout);
    }
}

// Точка входа контроллера сада.
int main(int argc, char *argv[]) {
    int M = 10;
    int N = 10;
    int speed1 = 3; // множитель времени обработки для садовника 1
    int speed2 = 2; // для садовника 2

    if (argc >= 3) {
        M = atoi(argv[1]);
        N = atoi(argv[2]);
    }
    if (argc >= 5) {
        speed1 = atoi(argv[3]);
        speed2 = atoi(argv[4]);
    }

    if (M <= 0 || M > MAX_M || N <= 0 || N > MAX_N) {
        fprintf(stderr, "Invalid garden size. Use M,N in 1..%d\n", MAX_M);
        return 1;
    }

    // На всякий случай очистим старые ресурсы
    shm_unlink(SHM_NAME);
    char sem_name[64];
    for (int i = 0; i < MAX_M; ++i) {
        for (int j = 0; j < MAX_N; ++j) {
            make_cell_sem_name(sem_name, sizeof(sem_name), i, j);
            sem_unlink(sem_name);
        }
    }

    // Создаем shared memory
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    memset(shared, 0, sizeof(SharedData));
    shared->M = M;
    shared->N = N;
    shared->pass_time_us  = 100000;
    shared->work_time1_us = shared->pass_time_us * speed1;
    shared->work_time2_us = shared->pass_time_us * speed2;
    shared->stop = 0;
    shared->initialized = 0;
    shared->finished1 = 0;
    shared->finished2 = 0;

    // Инициализация сада
    srand(time(NULL));
    int cells = M * N;
    int percent = 10 + rand() % 21;
    int obstacles_target = cells * percent / 100;
    int obstacles = 0;

    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            shared->cell_state[i][j] = CELL_EMPTY;
        }
    }

    while (obstacles < obstacles_target) {
        int i = rand() % M;
        int j = rand() % N;
        if (shared->cell_state[i][j] == CELL_EMPTY) {
            shared->cell_state[i][j] = CELL_OBSTACLE;
            obstacles++;
        }
    }

    // Создаем именованные семафоры для каждой клетки
    sem_t *cell_sem[MAX_M][MAX_N];

    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            make_cell_sem_name(sem_name, sizeof(sem_name), i, j);
            cell_sem[i][j] = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 1);
            if (cell_sem[i][j] == SEM_FAILED) {
                if (errno == EEXIST) {
                    // Если остался от старого запуска
                    cell_sem[i][j] = sem_open(sem_name, 0);
                }
            }
            if (cell_sem[i][j] == SEM_FAILED) {
                perror("sem_open cell");
                // простая зачистка и выход
                for (int x = 0; x <= i; ++x) {
                    for (int y = 0; y < (x == i ? j : N); ++y) {
                        make_cell_sem_name(sem_name, sizeof(sem_name), x, y);
                        sem_close(cell_sem[x][y]);
                        sem_unlink(sem_name);
                    }
                }
                munmap(shared, sizeof(SharedData));
                close(shm_fd);
                shm_unlink(SHM_NAME);
                return 1;
            }
        }
    }

    // Выводим начальное состояние сада
    printf("Garden size: %dx%d, obstacles: %d (%d%%)\n", M, N, obstacles, percent);
    printf("Initial garden state:\n");
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            char c = (shared->cell_state[i][j] == CELL_OBSTACLE) ? '#' : '.';
            printf("%c ", c);
        }
        printf("\n");
    }
    printf("Controller: garden initialized.\n");
    printf("Run ./gardener1 and ./gardener2 in other terminals.\n");
    fflush(stdout);

    // Разрешаем садовникам стартовать
    shared->initialized = 1;

    // Обработчик SIGINT
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        shared->stop = 1;
    }

    // Ждём завершения обоих садовников или остановки
    while (!shared->stop &&
           !(shared->finished1 && shared->finished2)) {
        sleep(1);
    }

    // Гарантируем остановку
    shared->stop = 1;
    // Дадим садовникам время выйти
    sleep(1);

    printf("Controller: finishing...\n");
    print_garden(shared);

    // Закрываем и удаляем семафоры
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            make_cell_sem_name(sem_name, sizeof(sem_name), i, j);
            sem_close(cell_sem[i][j]);
            sem_unlink(sem_name);
        }
    }

    // Удаляем shared memory
    munmap(shared, sizeof(SharedData));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    return 0;
}
