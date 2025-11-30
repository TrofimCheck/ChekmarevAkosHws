#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#define SHM_NAME "/garden_shm_example"
#define MAX_M 20
#define MAX_N 20

#define CELL_EMPTY 0
#define CELL_DONE1 1
#define CELL_DONE2 2
#define CELL_OBSTACLE -1

typedef struct {
    int M, N; // размер сада
    int pass_time_us; // время прохода через клетку
    int work_time1_us; // время обработки клетки садовником 1
    int work_time2_us; // время обработки клетки садовником 2
    int processed1; // сколько клеток обработал садовник 1
    int processed2; // сколько клеток обработал садовник 2
    int stop; // флаг завершения

    sem_t print_sem; // для синхронизации печати
    sem_t cell_sem[MAX_M][MAX_N]; // семафор на каждую клетку
    int cell_state[MAX_M][MAX_N]; // состояние клетки
} SharedData;

static SharedData *shared = NULL;
static int shm_fd = -1;
static pid_t child1 = -1, child2 = -1;
static volatile sig_atomic_t terminate_flag = 0;

// Освобождение ресурсов shared memory и неименованных семафоров.
// Вызывается в конце программы или при ошибках/прерывании.
void cleanup_shared(void) {
    if (shared) {
        int M = shared->M;
        int N = shared->N;

        sem_destroy(&shared->print_sem);
        for (int i = 0; i < M; ++i) {
            for (int j = 0; j < N; ++j) {
                sem_destroy(&shared->cell_sem[i][j]);
            }
        }
        munmap(shared, sizeof(SharedData));
        shared = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
        shm_unlink(SHM_NAME);
    }
}

// Обработчик сигнала SIGINT (Ctrl+C) в родительском процессе.
// Ставит флаг завершения, выставляет shared->stop и посылает SIGTERM дочерним процессам.
void sigint_handler(int sig) {
    (void)sig;
    terminate_flag = 1;
    if (shared) {
        shared->stop = 1;
    }
    if (child1 > 0) kill(child1, SIGTERM);
    if (child2 > 0) kill(child2, SIGTERM);
}

// Функция сна в микросекундах через nanosleep.
// Используется для моделирования времени прохода и обработки клетки.
static void sleep_us(int microseconds) {
    struct timespec ts;
    ts.tv_sec  = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// Обработка одной клетки садовником.
// sh - указатель на разделяемые данные,
// i, j - координаты клетки,
// gardener_id - номер садовника.
// С учётом семафора клетки гарантируется взаимное исключение.
void work_on_cell(SharedData *sh, int i, int j, int gardener_id) {
    if (sh->stop) return;

    if (sh->cell_state[i][j] == CELL_OBSTACLE) {
        // Если клетка-препятствие, то пропускаем.
        sleep_us(sh->pass_time_us);
        return;
    }

    // Заходим в критическую секцию клетки и ждём, если там другой садовник.
    sem_wait(&sh->cell_sem[i][j]);

    if (sh->stop) {
        sem_post(&sh->cell_sem[i][j]);
        return;
    }

    if (sh->cell_state[i][j] == CELL_EMPTY) {
        // Первый садовник, добравшийся до клетки, её обрабатывает.
        if (gardener_id == 1) {
            sh->cell_state[i][j] = CELL_DONE1;
            sh->processed1++;
        } else {
            sh->cell_state[i][j] = CELL_DONE2;
            sh->processed2++;
        }

        // Вывод сообщения о том, что клетка обработана.
        sem_wait(&sh->print_sem);
        printf("Gardener %d processed cell (%d,%d)\n", gardener_id, i, j);
        fflush(stdout);
        sem_post(&sh->print_sem);

        // Моделируем время обработки.
        if (gardener_id == 1)
            sleep_us(sh->work_time1_us);
        else
            sleep_us(sh->work_time2_us);
    } else {
        // Если клетка уже кем-то обработан, то просто пропускаем.
        sem_wait(&sh->print_sem);
        printf("Gardener %d skipped cell (%d,%d), state=%d\n", gardener_id, i, j, sh->cell_state[i][j]);
        fflush(stdout);
        sem_post(&sh->print_sem);

        // Моделируем время прохода.
        sleep_us(sh->pass_time_us);
    }

    // Выход из критической секции клетки.
    sem_post(&sh->cell_sem[i][j]);
}

// Тело процесса садовника 1.
// Обходит сад змейкой по строкам: чётные слева направо, нечётные справа налево.
void gardener1_process(SharedData *sh) {
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

    sem_wait(&sh->print_sem);
    printf("Gardener 1 finished\n");
    fflush(stdout);
    sem_post(&sh->print_sem);
}

// Тело процесса садовника 2.
// Обходит сад змейкой по столбцам справа налево:
// для чётного по счёту столбца движение снизу вверх, для нечётного сверху вниз.
void gardener2_process(SharedData *sh) {
    int M = sh->M;
    int N = sh->N;

    // снизу вверх по правому столбцу, затем столбец левее сверху вниз и т.д.
    for (int j = N - 1; j >= 0 && !sh->stop; --j) {
        if ((N - 1 - j) % 2 == 0) {
            // движение вверх
            for (int i = M - 1; i >= 0 && !sh->stop; --i) {
                work_on_cell(sh, i, j, 2);
            }
        } else {
            // движение вниз
            for (int i = 0; i < M && !sh->stop; ++i) {
                work_on_cell(sh, i, j, 2);
            }
        }
    }

    sem_wait(&sh->print_sem);
    printf("Gardener 2 finished\n");
    fflush(stdout);
    sem_post(&sh->print_sem);
}

// Вывод итогового состояния сада и статистики по обработанным клеткам.
void print_garden(SharedData *sh) {
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

// Точка входа: создание shared memory, инициализация сада и семафоров,
// запуск двух дочерних процессов-садовников и ожидание их завершения.
int main(int argc, char *argv[]) {
    int M = 10;
    int N = 10;
    int speed1 = 3; // во сколько раз обработка дольше прохода для садовника 1
    int speed2 = 2; // для садовника 2

    // Чтение параметров из командной строки: M N
    if (argc >= 3) {
        M = atoi(argv[1]);
        N = atoi(argv[2]);
    }
    if (argc >= 5) {
        speed1 = atoi(argv[3]);
        speed2 = atoi(argv[4]);
    }

    if (M <= 0 || M > MAX_M || N <= 0 || N > MAX_N) {
        fprintf(stderr, "Invalid garden size. Use 1..%d for M, 1..%d for N.\n", MAX_M, MAX_N);
        return 1;
    }

    // создаём POSIX shared memory
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

    // Обнуляем структуру и задаём основные параметры сада и времён.
    memset(shared, 0, sizeof(SharedData));
    shared->M = M;
    shared->N = N;
    shared->pass_time_us = 100000; // 0.1 секунды проход
    shared->work_time1_us = shared->pass_time_us * speed1;
    shared->work_time2_us = shared->pass_time_us * speed2;
    shared->stop = 0;

    // инициализация сада и семафоров
    srand(time(NULL));
    int cells = M * N;
    int percent = 10 + rand() % 21; // от 10 до 30
    int obstacles_target = cells * percent / 100;
    int obstacles = 0;

    // Инициализация состояний клеток и неименованных семафоров на каждую клетку.
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            shared->cell_state[i][j] = CELL_EMPTY;
            if (sem_init(&shared->cell_sem[i][j], 1, 1) == -1) {
                perror("sem_init cell");
                cleanup_shared();
                return 1;
            }
        }
    }

    // Семафор для синхронизации вывода в консоль.
    if (sem_init(&shared->print_sem, 1, 1) == -1) {
        perror("sem_init print_sem");
        cleanup_shared();
        return 1;
    }

    // случайная расстановка препятствий
    while (obstacles < obstacles_target) {
        int i = rand() % M;
        int j = rand() % N;
        if (shared->cell_state[i][j] == CELL_EMPTY) {
            shared->cell_state[i][j] = CELL_OBSTACLE;
            obstacles++;
        }
    }

    printf("Garden size: %dx%d, obstacles: %d (%d%%)\n", M, N, obstacles, percent);

    printf("Initial garden state:\n");
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            char c = (shared->cell_state[i][j] == CELL_OBSTACLE) ? '#' : '.';
            printf("%c ", c);
        }
        printf("\n");
    }
    fflush(stdout); // чтобы вывод не продублировался после fork

    // обработчик SIGINT в родителе
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        cleanup_shared();
        return 1;
    }

    // создаём процессы-садовников
    child1 = fork();
    if (child1 == -1) {
        perror("fork gardener1");
        cleanup_shared();
        return 1;
    }

    if (child1 == 0) {
        // дочерний процесс: садовник 1.
        gardener1_process(shared);
        _exit(0);
    }

    child2 = fork();
    if (child2 == -1) {
        perror("fork gardener2");
        kill(child1, SIGTERM);
        cleanup_shared();
        return 1;
    }

    if (child2 == 0) {
        // дочерний процесс: садовник 2
        gardener2_process(shared);
        _exit(0);
    }

    // родитель ждёт завершения обоих детей
    int finished = 0;
    while (finished < 2) {
        int status;
        pid_t pid = wait(&status);
        if (pid == -1) {
            if (errno == EINTR) {
                if (terminate_flag) break;
                continue;
            } else {
                perror("wait");
                break;
            }
        } else {
            finished++;
        }
    }

    // на всякий случай убиваем висящие процессы
    if (child1 > 0) kill(child1, SIGTERM);
    if (child2 > 0) kill(child2, SIGTERM);
    while (wait(NULL) > 0) {
    }

    // итоговый сад
    print_garden(shared);

    cleanup_shared();
    return 0;
}
