#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//  Константы и состояния
#define MAX_M 20 // максимальное количество строк
#define MAX_N 20 // максимальное количество столбцов
#define CELL_EMPTY 0 // свободная клетка
#define CELL_DONE1 1 // обработана садовником 1
#define CELL_DONE2 2 // обработана садовником 2
#define CELL_OBSTACLE -1 // препятствие

//  Глобальные флаги
static volatile sig_atomic_t g_stop = 0; // остановка по Ctrl+C

// SIGINT: только выставляем флаг остановки
static void sigint_handler(int sig) {
    (void)sig;
    g_stop = 1;
}

//  Вспомогательные функции

// Сон в микросекундах через nanosleep
static void sleep_us(int microseconds) {
    if (microseconds <= 0) return;
    struct timespec ts;
    ts.tv_sec  = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        if (g_stop) return;
    }
}

// Считает момент времени, до которого ждём sem_timedwait (через delta_ms мс).
static struct timespec make_abs_deadline_ms(long delta_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long nsec_add = (delta_ms % 1000) * 1000000L;
    long sec_add  = delta_ms / 1000;

    ts.tv_sec += sec_add;
    ts.tv_nsec += nsec_add;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return ts;
}

//  Конфигурация ввода

// Параметры запуска (CLI или config file)
typedef struct {
    int M, N; // размеры сада
    int speed1, speed2; // множители времени обработки
    int pass_time_us; // время прохода
    int obstacle_percent; // % препятствий, если < 0, то случайно 10..30
    unsigned int seed; // seed rand(), если 0, то time(NULL)
    char output_path[256]; // файл вывода (обязателен)
    char config_path[256]; // конфиг-файл (опционально)
} Config;

// Значения по умолчанию
static Config config_default(void) {
    Config c;
    c.M = 10;
    c.N = 10;
    c.speed1 = 3;
    c.speed2 = 2;
    c.pass_time_us = 100000;
    c.obstacle_percent = -1;
    c.seed = 0;
    c.output_path[0] = '\0';
    c.config_path[0] = '\0';
    return c;
}

// Вывод справки по запуску
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s -o <out.txt> [-m M] [-n N] [-s1 k] [-s2 k] [-p pass_us] [-seed N]\n"
        "  %s -o <out.txt> -c <config.txt>\n"
        "\n"
        "Options:\n"
        "  -o <file>     Output file (console + file)\n"
        "  -m <M>        Rows (1..%d)\n"
        "  -n <N>        Cols (1..%d)\n"
        "  -s1 <k>       Gardener 1 multiplier\n"
        "  -s2 <k>       Gardener 2 multiplier\n"
        "  -p <us>       Pass time (microseconds)\n"
        "  -seed <N>     Random seed\n"
        "  -c <file>     Read params from config file\n"
        "  -h            Help\n",
        prog, prog, MAX_M, MAX_N
    );
}

// Парсинг unsigned int из строки
static bool parse_uint(const char *s, unsigned int *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return false;
    *out = (unsigned int)v;
    return true;
}

// Удаляет пробелы и \r и \n по краям строки
static void trim(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' ' || s[len-1] == '\t')) {
        s[len-1] = '\0';
        len--;
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

// Читает конфиг key=value (строки с # игнорируются)
static bool load_config_file(const char *path, Config *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen(config)");
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        if (key[0] == '\0') continue;

        if (strcmp(key, "M") == 0) cfg->M = atoi(val);
        else if (strcmp(key, "N") == 0) cfg->N = atoi(val);
        else if (strcmp(key, "speed1") == 0) cfg->speed1 = atoi(val);
        else if (strcmp(key, "speed2") == 0) cfg->speed2 = atoi(val);
        else if (strcmp(key, "pass_time_us") == 0) cfg->pass_time_us = atoi(val);
        else if (strcmp(key, "seed") == 0) { unsigned int tmp; if (parse_uint(val, &tmp)) cfg->seed = tmp; }
        else if (strcmp(key, "obstacle_percent") == 0) cfg->obstacle_percent = atoi(val);
    }

    fclose(f);
    return true;
}

// Парсинг CLI аргументов
static bool parse_args(int argc, char **argv, Config *cfg) {
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (strcmp(a, "-o") == 0 && i + 1 < argc) {
            strncpy(cfg->output_path, argv[++i], sizeof(cfg->output_path) - 1);
            cfg->output_path[sizeof(cfg->output_path) - 1] = '\0';
        } else if (strcmp(a, "-c") == 0 && i + 1 < argc) {
            strncpy(cfg->config_path, argv[++i], sizeof(cfg->config_path) - 1);
            cfg->config_path[sizeof(cfg->config_path) - 1] = '\0';
        } else if (strcmp(a, "-m") == 0 && i + 1 < argc) {
            cfg->M = atoi(argv[++i]);
        } else if (strcmp(a, "-n") == 0 && i + 1 < argc) {
            cfg->N = atoi(argv[++i]);
        } else if (strcmp(a, "-s1") == 0 && i + 1 < argc) {
            cfg->speed1 = atoi(argv[++i]);
        } else if (strcmp(a, "-s2") == 0 && i + 1 < argc) {
            cfg->speed2 = atoi(argv[++i]);
        } else if (strcmp(a, "-p") == 0 && i + 1 < argc) {
            cfg->pass_time_us = atoi(argv[++i]);
        } else if (strcmp(a, "-seed") == 0 && i + 1 < argc) {
            cfg->seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", a);
            print_usage(argv[0]);
            return false;
        }
    }

    // Проверка обязательного параметра на вывод в файл
    if (cfg->output_path[0] == '\0') {
        fprintf(stderr, "Output file is required (-o <file>).\n");
        print_usage(argv[0]);
        return false;
    }

    return true;
}

//  Модель сада

// Общие данные для потоков (сад, синхронизация, логирование)
typedef struct {
    int M, N; // размеры сада
    int pass_time_us; // время прохода клетки
    int work_time1_us; // время обработки клетки садовником 1
    int work_time2_us; // время обработки клетки садовником 2
    int processed1; // сколько клеток обработал садовник 1
    int processed2; // сколько клеток обработал садовник 2
    int obstacle_percent; // процент препятствий (итоговый, после генерации)
    int cell_state[MAX_M][MAX_N]; // состояния клеток (., #, 1, 2)

    sem_t cell_sem[MAX_M][MAX_N]; // семафор на клетку: два садовника не заходят одновременно
    pthread_mutex_t log_mutex; // защита общего вывода (stdout и файл)
    bool log_mutex_inited; // был ли инициализирован log_mutex
    FILE *log_fp; // файл с результатами

    int sem_inited_count; // сколько семафоров уже успели инициализировать
} Garden;

// Вывод строки в stdout и в файл под мьютексом
static void log_printf(Garden *g, const char *fmt, ...) {
    pthread_mutex_lock(&g->log_mutex);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);

    if (g->log_fp) {
        va_start(ap, fmt);
        vfprintf(g->log_fp, fmt, ap);
        va_end(ap);
        fflush(g->log_fp);
    }

    pthread_mutex_unlock(&g->log_mutex);
}

// Вывод текущего состояния сада
static void print_garden(Garden *g, const char *title) {
    log_printf(g, "%s (M=%d, N=%d):\n", title, g->M, g->N);
    for (int i = 0; i < g->M; ++i) {
        for (int j = 0; j < g->N; ++j) {
            char c;
            switch (g->cell_state[i][j]) {
                case CELL_EMPTY: c = '.'; break;
                case CELL_OBSTACLE: c = '#'; break;
                case CELL_DONE1: c = '1'; break;
                case CELL_DONE2: c = '2'; break;
                default: c = '?'; break;
            }
            log_printf(g, "%c ", c);
        }
        log_printf(g, "\n");
    }
    log_printf(g, "\n");
}

// Захват клетки с таймаутом, чтобы можно было выйти по Ctrl+C
static bool lock_cell_timed(Garden *g, int i, int j) {
    while (!g_stop) {
        struct timespec abs = make_abs_deadline_ms(200);
        if (sem_timedwait(&g->cell_sem[i][j], &abs) == 0) return true;
        if (errno == ETIMEDOUT || errno == EINTR) continue;
        return false;
    }
    return false;
}

// Освобождение клетки
static void unlock_cell(Garden *g, int i, int j) {
    (void)g;
    sem_post(&g->cell_sem[i][j]);
}

// Посещение клетки: обработать/пропустить/препятствие с учётом семафора
static void work_on_cell(Garden *g, int i, int j, int gardener_id) {
    if (g_stop) return;

    if (g->cell_state[i][j] == CELL_OBSTACLE) {
        sleep_us(g->pass_time_us);
        return;
    }

    if (!lock_cell_timed(g, i, j)) {
        if (!g_stop) log_printf(g, "Gardener %d ERROR: cannot lock cell (%d,%d)\n", gardener_id, i, j);
        return;
    }

    if (g_stop) {
        unlock_cell(g, i, j);
        return;
    }

    if (g->cell_state[i][j] == CELL_EMPTY) {
        if (gardener_id == 1) { g->cell_state[i][j] = CELL_DONE1; g->processed1++; }
        else { g->cell_state[i][j] = CELL_DONE2; g->processed2++; }

        log_printf(g, "Gardener %d processed cell (%d,%d)\n", gardener_id, i, j);

        if (gardener_id == 1) sleep_us(g->work_time1_us);
        else sleep_us(g->work_time2_us);
    } else {
        log_printf(g, "Gardener %d skipped cell (%d,%d), state=%d\n", gardener_id, i, j, g->cell_state[i][j]);
        sleep_us(g->pass_time_us);
    }

    unlock_cell(g, i, j);
}

// Потоки садовников

typedef struct {
    Garden *g;
    int id;
} ThreadArgs;

// Поток садовника 1 (змейка по строкам)
static void *gardener1_thread(void *arg) {
    ThreadArgs *ta = (ThreadArgs *)arg;
    Garden *g = ta->g;

    log_printf(g, "Gardener 1 started.\n");

    for (int i = 0; i < g->M && !g_stop; ++i) {
        if (i % 2 == 0) {
            for (int j = 0; j < g->N && !g_stop; ++j) work_on_cell(g, i, j, 1);
        } else {
            for (int j = g->N - 1; j >= 0 && !g_stop; --j) work_on_cell(g, i, j, 1);
        }
    }

    log_printf(g, "Gardener 1 finished.\n");
    return NULL;
}

// Поток садовника 2 (змейка по столбцам справа налево)
static void *gardener2_thread(void *arg) {
    ThreadArgs *ta = (ThreadArgs *)arg;
    Garden *g = ta->g;

    log_printf(g, "Gardener 2 started.\n");

    for (int j = g->N - 1; j >= 0 && !g_stop; --j) {
        if ((g->N - 1 - j) % 2 == 0) {
            for (int i = g->M - 1; i >= 0 && !g_stop; --i) work_on_cell(g, i, j, 2);
        } else {
            for (int i = 0; i < g->M && !g_stop; ++i) work_on_cell(g, i, j, 2);
        }
    }

    log_printf(g, "Gardener 2 finished.\n");
    return NULL;
}

//  Инициализация и очистка

// Освобождение ресурсов (безопасно для частичной инициализации)
static void garden_destroy(Garden *g) {
    if (g->sem_inited_count > 0 && g->N > 0) {
        for (int k = 0; k < g->sem_inited_count; ++k) {
            int i = k / g->N;
            int j = k % g->N;
            sem_destroy(&g->cell_sem[i][j]);
        }
        g->sem_inited_count = 0;
    }

    if (g->log_mutex_inited) {
        pthread_mutex_destroy(&g->log_mutex);
        g->log_mutex_inited = false;
    }

    if (g->log_fp) {
        fclose(g->log_fp);
        g->log_fp = NULL;
    }
}

// Создание сада: файл с результатами, мьютекс, семафоры клеток, генерация препятствий
static bool garden_init(Garden *g, const Config *cfg) {
    memset(g, 0, sizeof(*g));

    g->M = cfg->M;
    g->N = cfg->N;
    g->pass_time_us = cfg->pass_time_us;
    g->work_time1_us = cfg->pass_time_us * cfg->speed1;
    g->work_time2_us = cfg->pass_time_us * cfg->speed2;

    if (g->M <= 0 || g->M > MAX_M || g->N <= 0 || g->N > MAX_N) {
        fprintf(stderr, "Invalid garden size: M,N must be in 1..%d\n", MAX_M);
        return false;
    }
    if (cfg->speed1 <= 0 || cfg->speed2 <= 0 || cfg->pass_time_us <= 0) {
        fprintf(stderr, "Invalid speed/pass_time parameters.\n");
        return false;
    }

    g->log_fp = fopen(cfg->output_path, "w");
    if (!g->log_fp) {
        perror("fopen(output)");
        return false;
    }

    if (pthread_mutex_init(&g->log_mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        garden_destroy(g);
        return false;
    }
    g->log_mutex_inited = true;

    for (int i = 0; i < g->M; ++i) {
        for (int j = 0; j < g->N; ++j) {
            if (sem_init(&g->cell_sem[i][j], 0, 1) == -1) {
                perror("sem_init(cell)");
                garden_destroy(g);
                return false;
            }
            g->sem_inited_count++;
        }
    }

    for (int i = 0; i < g->M; ++i) {
        for (int j = 0; j < g->N; ++j) {
            g->cell_state[i][j] = CELL_EMPTY;
        }
    }

    int percent = cfg->obstacle_percent;
    if (percent < 0) percent = 10 + (rand() % 21);
    if (percent < 10) percent = 10;
    if (percent > 30) percent = 30;
    g->obstacle_percent = percent;

    int total = g->M * g->N;
    int target = total * percent / 100;
    int placed = 0;
    while (placed < target) {
        int i = rand() % g->M;
        int j = rand() % g->N;
        if (g->cell_state[i][j] == CELL_EMPTY) {
            g->cell_state[i][j] = CELL_OBSTACLE;
            placed++;
        }
    }

    log_printf(g, "Garden size: %dx%d, obstacles: %d (%d%%)\n", g->M, g->N, placed, percent);
    log_printf(g, "pass_time_us=%d, work_time1_us=%d, work_time2_us=%d\n\n", g->pass_time_us, g->work_time1_us, g->work_time2_us);

    print_garden(g, "Initial garden state");
    return true;
}

// Точка входа в программу
int main(int argc, char **argv) {
    Config cfg = config_default();
    
    // Чтение параметров запуска из командной строки
    if (!parse_args(argc, argv, &cfg)) return 1;

    // Если задан -c, читаем параметры из файла, а -o оставляем из CLI
    if (cfg.config_path[0] != '\0') {
        Config from_file = config_default();
        strncpy(from_file.output_path, cfg.output_path, sizeof(from_file.output_path) - 1);
        from_file.output_path[sizeof(from_file.output_path) - 1] = '\0';

        if (!load_config_file(cfg.config_path, &from_file)) {
            fprintf(stderr, "Cannot read config file: %s\n", cfg.config_path);
            return 1;
        }
        cfg = from_file;
    }
    // Инициализация генератора случайных чисел
    unsigned int seed = cfg.seed ? cfg.seed : (unsigned int)time(NULL);
    srand(seed);

    // Обработка Ctrl+C
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) perror("sigaction(SIGINT)");

    Garden g;
    if (!garden_init(&g, &cfg)) {
        garden_destroy(&g);
        return 1;
    }
    
    // Вывод seed и имя config файла, если есть
    log_printf(&g, "Random seed: %u\n", seed);
    if (cfg.config_path[0] != '\0') log_printf(&g, "Config file: %s\n", cfg.config_path);
    log_printf(&g, "\n");
    
    // Создаём 2 потока (2 садовника)
    pthread_t t1, t2;
    ThreadArgs a1 = { .g = &g, .id = 1 };
    ThreadArgs a2 = { .g = &g, .id = 2 };

    if (pthread_create(&t1, NULL, gardener1_thread, &a1) != 0) {
        perror("pthread_create(gardener1)");
        garden_destroy(&g);
        return 1;
    }
    if (pthread_create(&t2, NULL, gardener2_thread, &a2) != 0) {
        perror("pthread_create(gardener2)");
        g_stop = 1;
        pthread_join(t1, NULL);
        garden_destroy(&g);
        return 1;
    }
    
    // Ожидание завершения потоков
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    // Итоговое состояние сада и статистика
    log_printf(&g, "\n");
    print_garden(&g, "Final garden state");
    log_printf(&g, "Processed by gardener 1: %d\n", g.processed1);
    log_printf(&g, "Processed by gardener 2: %d\n", g.processed2);

    // Если остановили по Ctrl+C, то фиксируем это в отчёте выполнения
    if (g_stop) log_printf(&g, "\nStopped by SIGINT (Ctrl+C).\n");
    
    // Освобождение ресурсов
    garden_destroy(&g);
    return 0;
}
