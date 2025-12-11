// Асинхронная параллельная схема суммирования с 100 источниками.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>

#define NUM_SOURCES 100
#define MAX_BUFFER  256

static int buffer[MAX_BUFFER];
static int buf_count = 0; // сколько чисел сейчас в буфере

static int sources_finished = 0; // сколько источников уже отработали
static int active_summators = 0; // сколько сумматоров сейчас считают
static int summator_id_counter = 0; // для красивых номеров сумматоров

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;

// Случайные чисел в потоке
static int rand_range(unsigned int *seed, int min, int max) {
    return min + (int)(rand_r(seed) % (unsigned)(max - min + 1));
}

// Поток-источник
static void *source_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)pthread_self();
    int delay = rand_range(&seed, 1, 7);
    sleep((unsigned int)delay);
    pthread_mutex_lock(&mtx);
    if (buf_count >= MAX_BUFFER) {
        fprintf(stderr, "[Источник %3d] Ошибка: буфер переполнен!\n", id);
        pthread_mutex_unlock(&mtx);
        return NULL;
    }
    buffer[buf_count++] = id;
    printf("[Источник %3d] задержка %d c, сгенерировано %d, добавлено в буфер (размер=%d)\n",
           id, delay, id, buf_count);
    sources_finished++;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mtx);
    return NULL;
}

// Аргументы для сумматора
typedef struct {
    int a;
    int b;
    int id;
} sum_args_t;

// Поток-сумматор
static void *summator_thread(void *arg) {
    sum_args_t *s = (sum_args_t *)arg;
    int a = s->a;
    int b = s->b;
    int id = s->id;
    free(s);

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)pthread_self();
    int delay = rand_range(&seed, 3, 6);

    printf("[Сумматор %3d] старт: %d + %d, задержка %d c\n", id, a, b, delay);
    sleep((unsigned int)delay);
    int sum = a + b;

    pthread_mutex_lock(&mtx);
    if (buf_count >= MAX_BUFFER) {
        fprintf(stderr, "[Сумматор %3d] Ошибка: буфер переполнен при добавлении результата!\n", id);
    } else {
        buffer[buf_count++] = sum;
        printf("[Сумматор %3d] завершено: %d + %d = %d, результат добавлен в буфер (размер=%d)\n",
               id, a, b, sum, buf_count);
    }
    active_summators--;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mtx);

    return NULL;
}

// Поток-монитор
static void *monitor_thread(void *arg) {
    (void)arg;

    while (1) {
        pthread_mutex_lock(&mtx);
        if (sources_finished == NUM_SOURCES &&
            active_summators == 0 &&
            buf_count == 1) {

            int result = buffer[0];
            printf("\n[Монитор] все источники завершены, сумматоров нет.\n");
            printf("[Монитор] в буфере одно число: %d - окончательный результат.\n", result);
            pthread_mutex_unlock(&mtx);
            break;
        }

        while (buf_count < 2 &&
               !(sources_finished == NUM_SOURCES && active_summators == 0 && buf_count == 1)) {

            pthread_cond_wait(&cond, &mtx);
        }
        
        if (sources_finished == NUM_SOURCES &&
            active_summators == 0 &&
            buf_count == 1) {

            int result = buffer[0];
            printf("\n[Монитор] все источники завершены, сумматоров нет.\n");
            printf("[Монитор] в буфере одно число: %d - окончательный результат.\n", result);
            pthread_mutex_unlock(&mtx);
            break;
        }

        if (buf_count >= 2) {
            int b = buffer[--buf_count];
            int a = buffer[--buf_count];
            int sid = ++summator_id_counter;
            active_summators++;

            printf("[Монитор] из буфера взяты числа %d и %d (размер буфера теперь=%d), запуск сумматора %3d\n",
                   a, b, buf_count, sid);

            sum_args_t *args = (sum_args_t *)malloc(sizeof(sum_args_t));
            if (!args) {
                fprintf(stderr, "[Монитор] Ошибка: не удалось выделить память под аргументы сумматора\n");
                active_summators--;
                pthread_mutex_unlock(&mtx);
                continue;
            }
            args->a = a;
            args->b = b;
            args->id = sid;

            pthread_t tid;
            if (pthread_create(&tid, NULL, summator_thread, args) != 0) {
                fprintf(stderr, "[Монитор] Ошибка: не удалось создать поток-сумматор\n");
                active_summators--;
                free(args);
                pthread_mutex_unlock(&mtx);
                continue;
            }
            pthread_detach(tid);
        }

        pthread_mutex_unlock(&mtx);
    }

    return NULL;
}

// Главная программа
int main(void) {
    printf("Старт программы. Источников: %d\n", NUM_SOURCES);
    printf("Числа 1..%d порождаются с задержкой 1-7 c и попадают в общий буфер.\n", NUM_SOURCES);
    printf("Монитор запускает сумматоры 3-6 c для суммирования любых двух чисел.\n\n");

    pthread_t sources[NUM_SOURCES];
    pthread_t monitor;

    if (pthread_create(&monitor, NULL, monitor_thread, NULL) != 0) {
        perror("pthread_create (monitor)");
        return 1;
    }

    for (int i = 0; i < NUM_SOURCES; ++i) {
        int id = i + 1;
        if (pthread_create(&sources[i], NULL, source_thread, (void *)(intptr_t)id) != 0) {
            perror("pthread_create (source)");
            return 1;
        }
    }

    for (int i = 0; i < NUM_SOURCES; ++i) {
        pthread_join(sources[i], NULL);
    }

    pthread_join(monitor, NULL);

    printf("\nПрограмма завершена.\n");
    return 0;
}

