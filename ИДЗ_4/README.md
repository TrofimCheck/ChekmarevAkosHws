# Отчёт по индивидуальному заданию 4

## Исполнитель
- **ФИО:** Чекмарёв Трофим Владимирович  
- **Группа:** БПИ-245  

## Вариант и условие
- **Вариант:** 21  
- **Условие:** Задача о нелюдимых садовниках. Имеется пустой участок земли (двумерный массив размером M × N ) и план сада, разбитого на отдельные квадраты. От 10 до 30 процентов (задается случайно) площади сада заняты прудами или камнями. То есть недоступны для ухаживания. Эти квадраты располагаются на плане произвольным (случайным) образом. Ухаживание за садом выполняют два садовника, которые не хотят встречаться друг другом (то есть, одновременно появляться в одном и том же квадрате). Первый садовник начинает работу с верхнего левого угла сада и перемещается слева направо, сделав ряд, он спускается вниз и идет в обратном направлении, пропуская обработанные участки. Второй садовник начинает работу с нижнего правого угла сада и перемещается снизу вверх, сделав ряд, он перемещается влево и также идет в обратную сторону. Если садовник видит, что участок сада уже обработан другим садовником или является необрабатываемым, он идет дальше. Если по пути какой-то участок занят другим садовником, то садовник ожидает когда участок освободится, чтобы пройти дальше на доступный ему необработанный участок. Садовники должны работать одновременно со скоростями, определяемыми как параметры задачи. Прохождение через любой квадрат занимает некоторое время, которое задается константой, меньшей чем времена обработки и принимается за единицу времени.
Создать многопоточное приложение, моделирующее работу садовников.
Каждый садовник — это отдельный поток.

---

## Файлы с кодом
- `garden_threads.c` – основная программа (1 процесс, 2 потока-садовника):
  - генерация сада и препятствий;
  - запуск потоков `pthread_create`;
  - синхронизация доступа к клеткам через POSIX-семафоры;
  - потокобезопасный вывод в консоль и в файл;
  - корректное завершение по окончанию работы и по `SIGINT`.

Файлы для тестирования на 8 баллов:
- `configs/config1.txt`, `configs/config2.txt`, `configs/config3.txt` – примеры входных конфигураций.  
- `outputs/output1.txt`, `outputs/output2.txt`, `outputs/output3.txt`, `outputs/output4_1.txt`, `outputs/output4_2.txt` – результаты запусков.

---

## Описание решения

### Сценарий в терминах предметной области

Оба садовника начинают работу одновременно: первый из верхнего левого угла, второй из нижнего правого. Каждый садовник движется по своему маршруту и последовательно заходит в клетки. Если клетка является препятствием (пруд/камень) или уже обработана другим садовником, садовник не обрабатывает её и идёт дальше. Если клетка свободна и ещё не обработана, садовник выполняет уход за ней: обработка занимает больше времени, чем простой проход через клетку. Если на пути клетка занята другим садовником, текущий садовник ждёт освобождения клетки и продолжает движение только после этого.

### Модель параллельных вычислений

Используется модель параллелизма с общей памятью: один процесс содержит общие данные сада, а работа выполняется двумя параллельными POSIX-потоками (по одному потоку на садовника). Для каждой клетки используется бинарный семафор, который гарантирует, что два садовника не могут одновременно находиться в одной клетке (перед входом `sem_wait`, после выхода `sem_post`). Вывод синхронизируется мьютексом, чтобы строки не перемешивались и одинаково записывались в консоль и в файл.

### Входные данные и диапазоны

Параметры задаются через командную строку или конфигурационный файл: размеры сада `M` и `N` (1..20), множители скорости обработки `speed1`, `speed2` (>0), время прохода `pass_time_us` (>0, микросекунды), `seed` (если задан, то генерация одинаковая; если не задан, то используется `time(NULL)`), а также `obstacle_percent` (если задан, то фиксирурованное количество препятствий, иначе случайное значение в диапазоне 10..30%). Выходной файл задаётся параметром `-o`, при этом результаты одновременно выводятся в консоль и записываются в файл.

### Состояния клеток

- `0` – пусто (не обработана)
- `-1` – препятствие
- `1` – обработана садовником 1
- `2` – обработана садовником 2

### Генерация препятствий (случайные данные)

- Процент препятствий берётся либо из `obstacle_percent`, либо случайно в диапазоне 10..30%.
- Препятствия расставляются случайно по полю без повторений.
- При фиксированном `seed` результат одинаковый на каждом запуске.

---

## Способ запуска на Linux

Компиляция:
```bash
gcc -std=c11 -O2 -Wall -Wextra -pedantic -pthread garden_threads.c -o garden_threads
```

Запуск с параметрами командной строки:
```bash
./garden_threads -o outputs/output1.txt -m 10 -n 10 -s1 3 -s2 2 -p 100000 -seed 3883
```

Запуск из конфигурационного файла:
```bash
./garden_threads -o outputs/output2.txt -c configs/config1.txt
./garden_threads -o outputs/output3.txt -c configs/config2.txt
./garden_threads -o outputs/output4_1.txt -c configs/config3.txt
./garden_threads -o outputs/output4_2.txt -c configs/config3.txt
```
Программа делает одинаковый вывод и в консоль, и в файл `-o`.

---

## Результаты работы программы

Итоги по файлам в `outputs/`:

### Тест 1: запуск без конфигурационного файла (ввод через CLI)
Пример запуска:
```bash
./garden_threads -o outputs/output1.txt -m 10 -n 10 -s1 3 -s2 2 -p 100000 -seed 3883
Garden size: 10x10, obstacles: 22 (22%)
pass_time_us=100000, work_time1_us=300000, work_time2_us=200000

Initial garden state (M=10, N=10):
. . . . . . . . . . 
# # . . # # . . . . 
. . . . . . . . . . 
. . . . # . . . . # 
. # . . # # # # . # 
. . . . # # . . . . 
. . # . . # # . . . 
. . . . . . . . . . 
# # . . . # . . . . 
# . . . . . . # . . 

Random seed: 3883

Gardener 1 started.
Gardener 1 processed cell (0,0)
Gardener 2 started.
Gardener 2 processed cell (9,9)
Gardener 2 processed cell (8,9)
Gardener 1 processed cell (0,1)
Gardener 2 processed cell (7,9)
Gardener 1 processed cell (0,2)
Gardener 2 processed cell (6,9)
Gardener 2 processed cell (5,9)
Gardener 1 processed cell (0,3)
Gardener 1 processed cell (0,4)
Gardener 2 processed cell (2,9)
Gardener 2 processed cell (1,9)
Gardener 1 processed cell (0,5)
Gardener 2 processed cell (0,9)
Gardener 1 processed cell (0,6)
Gardener 2 processed cell (0,8)
Gardener 2 processed cell (1,8)
Gardener 1 processed cell (0,7)
Gardener 2 processed cell (2,8)
Gardener 1 skipped cell (0,8), state=2
Gardener 2 processed cell (3,8)
Gardener 1 skipped cell (0,9), state=2
Gardener 1 skipped cell (1,9), state=2
Gardener 2 processed cell (4,8)
Gardener 1 skipped cell (1,8), state=2
Gardener 1 processed cell (1,7)
Gardener 2 processed cell (5,8)
Gardener 2 processed cell (6,8)
Gardener 1 processed cell (1,6)
Gardener 2 processed cell (7,8)
Gardener 2 processed cell (8,8)
Gardener 1 processed cell (1,3)
Gardener 2 processed cell (9,8)
Gardener 1 processed cell (1,2)
Gardener 2 processed cell (8,7)
Gardener 2 processed cell (7,7)
Gardener 2 processed cell (6,7)
Gardener 1 processed cell (2,0)
Gardener 2 processed cell (5,7)
Gardener 1 processed cell (2,1)
Gardener 2 processed cell (3,7)
Gardener 2 processed cell (2,7)
Gardener 1 processed cell (2,2)
Gardener 2 skipped cell (1,7), state=1
Gardener 1 processed cell (2,3)
Gardener 2 skipped cell (0,7), state=1
Gardener 2 skipped cell (0,6), state=1
Gardener 2 skipped cell (1,6), state=1
Gardener 1 processed cell (2,4)
Gardener 2 processed cell (2,6)
Gardener 2 processed cell (3,6)
Gardener 1 processed cell (2,5)
Gardener 2 processed cell (5,6)
Gardener 1 skipped cell (2,6), state=2
Gardener 1 skipped cell (2,7), state=2
Gardener 1 skipped cell (2,8), state=2
Gardener 2 processed cell (7,6)
Gardener 1 skipped cell (2,9), state=2
Gardener 2 processed cell (8,6)
Gardener 1 skipped cell (3,8), state=2
Gardener 2 processed cell (9,6)
Gardener 1 skipped cell (3,7), state=2
Gardener 1 skipped cell (3,6), state=2
Gardener 2 processed cell (9,5)
Gardener 1 processed cell (3,5)
Gardener 2 processed cell (7,5)
Gardener 1 processed cell (3,3)
Gardener 1 processed cell (3,2)
Gardener 2 skipped cell (3,5), state=1
Gardener 2 skipped cell (2,5), state=1
Gardener 1 processed cell (3,1)
Gardener 2 skipped cell (0,5), state=1
Gardener 2 skipped cell (0,4), state=1
Gardener 1 processed cell (3,0)
Gardener 2 skipped cell (2,4), state=1
Gardener 1 processed cell (4,0)
Gardener 2 processed cell (6,4)
Gardener 1 processed cell (4,2)
Gardener 2 processed cell (7,4)
Gardener 2 processed cell (8,4)
Gardener 1 processed cell (4,3)
Gardener 2 processed cell (9,4)
Gardener 2 processed cell (9,3)
Gardener 2 processed cell (8,3)
Gardener 1 skipped cell (4,8), state=2
Gardener 2 processed cell (7,3)
Gardener 1 skipped cell (5,9), state=2
Gardener 2 processed cell (6,3)
Gardener 1 skipped cell (5,8), state=2
Gardener 1 skipped cell (5,7), state=2
Gardener 2 processed cell (5,3)
Gardener 1 skipped cell (5,6), state=2
Gardener 2 skipped cell (4,3), state=1
Gardener 2 skipped cell (3,3), state=1
Gardener 1 skipped cell (5,3), state=2
Gardener 2 skipped cell (2,3), state=1
Gardener 1 processed cell (5,2)
Gardener 2 skipped cell (1,3), state=1
Gardener 2 skipped cell (0,3), state=1
Gardener 2 skipped cell (0,2), state=1
Gardener 1 processed cell (5,1)
Gardener 2 skipped cell (1,2), state=1
Gardener 2 skipped cell (2,2), state=1
Gardener 2 skipped cell (3,2), state=1
Gardener 1 processed cell (5,0)
Gardener 2 skipped cell (4,2), state=1
Gardener 2 skipped cell (5,2), state=1
Gardener 1 processed cell (6,0)
Gardener 2 processed cell (7,2)
Gardener 2 processed cell (8,2)
Gardener 1 processed cell (6,1)
Gardener 2 processed cell (9,2)
Gardener 2 processed cell (9,1)
Gardener 1 skipped cell (6,3), state=2
Gardener 1 skipped cell (6,4), state=2
Gardener 2 processed cell (7,1)
Gardener 1 skipped cell (6,7), state=2
Gardener 2 skipped cell (6,1), state=1
Gardener 1 skipped cell (6,8), state=2
Gardener 2 skipped cell (5,1), state=1
Gardener 1 skipped cell (6,9), state=2
Gardener 1 skipped cell (7,9), state=2
Gardener 2 skipped cell (3,1), state=1
Gardener 1 skipped cell (7,8), state=2
Gardener 2 skipped cell (2,1), state=1
Gardener 1 skipped cell (7,7), state=2
Gardener 1 skipped cell (7,6), state=2
Gardener 2 skipped cell (0,1), state=1
Gardener 1 skipped cell (7,5), state=2
Gardener 2 skipped cell (0,0), state=1
Gardener 1 skipped cell (7,4), state=2
Gardener 1 skipped cell (7,3), state=2
Gardener 2 skipped cell (2,0), state=1
Gardener 1 skipped cell (7,2), state=2
Gardener 2 skipped cell (3,0), state=1
Gardener 1 skipped cell (7,1), state=2
Gardener 2 skipped cell (4,0), state=1
Gardener 1 processed cell (7,0)
Gardener 2 skipped cell (5,0), state=1
Gardener 2 skipped cell (6,0), state=1
Gardener 2 skipped cell (7,0), state=1
Gardener 1 skipped cell (8,2), state=2
Gardener 2 finished.
Gardener 1 skipped cell (8,3), state=2
Gardener 1 skipped cell (8,4), state=2
Gardener 1 skipped cell (8,6), state=2
Gardener 1 skipped cell (8,7), state=2
Gardener 1 skipped cell (8,8), state=2
Gardener 1 skipped cell (8,9), state=2
Gardener 1 skipped cell (9,9), state=2
Gardener 1 skipped cell (9,8), state=2
Gardener 1 skipped cell (9,6), state=2
Gardener 1 skipped cell (9,5), state=2
Gardener 1 skipped cell (9,4), state=2
Gardener 1 skipped cell (9,3), state=2
Gardener 1 skipped cell (9,2), state=2
Gardener 1 skipped cell (9,1), state=2
Gardener 1 finished.

Final garden state (M=10, N=10):
1 1 1 1 1 1 1 1 2 2 
# # 1 1 # # 1 1 2 2 
1 1 1 1 1 1 2 2 2 2 
1 1 1 1 # 1 2 2 2 # 
1 # 1 1 # # # # 2 # 
1 1 1 2 # # 2 2 2 2 
1 1 # 2 2 # # 2 2 2 
1 2 2 2 2 2 2 2 2 2 
# # 2 2 2 # 2 2 2 2 
# 2 2 2 2 2 2 # 2 2 

Processed by gardener 1: 32
Processed by gardener 2: 46
```

Также итог работы программы представлен в файле вывода: `outputs/output1.txt`

### Тест 2: `configs/config1.txt` (6x8, одинаковый результат на каждом запуске)
Итог работы программы представлен в файле вывода: `outputs/output2.txt`

### Тест 3: `configs/config2.txt` (10x10, препятствий ровно 25%. Садовник 2 быстрее садовника 1)
Итог работы программы представлен в файле вывода: `outputs/output3.txt`

### Тест 4: `configs/config3.txt` (15x12, медленный проход, seed не задан, поэтому всегда рандом)
Итоги работы программы представлены в файлах вывода: `outputs/output4_1.txt` и `outputs/output4_2.txt`

---

## Дополнительная информация

### Под требования на 4-5 баллов
- Используются POSIX-семафоры и мьютекс для корректного взаимодействия потоков.  
- Вывод отражает ключевые события и итоговую статистику.  
- В коде присутствуют комментарии к структурам и основным функциям.
- Корректное завершение по `SIGINT (Ctrl+C)` с освобождением ресурсов (`sem_destroy`, `pthread_mutex_destroy`, `fclose`).

### Под требования на 6-7 баллов
- Добавлена генерация случайных препятствий в диапазоне 10..30%.  
- Реализован ввод исходных данных через аргументы командной строки (размеры, скорости, `pass_time_us`, `seed`).

### Под требования на 8 баллов
- Добавлен вывод результатов в файл (`-o <file>`) одновременно с выводом в консоль.  
- Реализован альтернативный ввод из конфигурационного файла (`-c <config>`).  
- Созданы 3 входные конфигурации и соответствующие выходные файлы результатов.  
