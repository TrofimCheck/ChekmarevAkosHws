#ifndef GARDEN_COMMON_H
#define GARDEN_COMMON_H

#include <stdio.h>
#include <time.h>

#define SHM_NAME "/garden_shm_named"
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
    int initialized; // флаг завершения инициализации контроллером
    int finished1; // флаг окончания работы садовника 1
    int finished2; // флаг окончания работы садовника 2
    int cell_state[MAX_M][MAX_N]; // состояние клетки
} SharedData;

// Сон в микросекундах через nanosleep
static inline void sleep_us(int microseconds) {
    struct timespec ts;
    ts.tv_sec  = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// Имя семафора клетки (i,j)
static inline void make_cell_sem_name(char *buf, size_t bufsize, int i, int j) {
    snprintf(buf, bufsize, "/garden_cell_%02d_%02d", i, j);
}

#endif
