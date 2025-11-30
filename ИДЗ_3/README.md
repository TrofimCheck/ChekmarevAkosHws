# Отчёт по индивидуальному заданию 3

## Исполнитель
- **ФИО:** Чекмарёв Трофим Владимирович  
- **Группа:** БПИ-245  

## Вариант и условие
- **Вариант:** 21
- **Условие:**  Задача о нелюдимых садовниках. Имеется пустой участок земли (двумерный массив размером `M × N`) и план сада, разбитого на отдельные квадраты. От 10 до 30 процентов (задается случайно) площади сада заняты прудами или камнями. То есть недоступны для ухаживания. Эти квадраты располагаются на плане произвольным  (случайным) образом. Ухаживание за садом выполняют два садовника, которые не хотят встречаться друг другом (то есть, одновременно появляться в одном и том же квадрате). Первый садовник начинает работу с верхнего левого угла сада и перемещается слева направо, сделав ряд, он спускается вниз и идет в обратном направлении, пропуская обработанные участки. Второй садовник начинает работу с нижнего правого угла сада и перемещается снизу вверх, сделав ряд, он перемещается влево и также идет в обратную сторону. Если садовник видит, что участок сада уже обработан другим садовником или является необрабатываемым, он идет дальше. Если по пути какой-то участок занят другим садовником, то садовник ожидает когда участок освободится, чтобы пройти дальше на доступный ему необработанный участок. Садовники должны работать одновременно со скоростями, определяемыми как параметры задачи. Прохождение через любой квадрат занимает некоторое время, которое задается константой,
меньшей чем времена обработки и принимается за единицу времени.
Создать многопроцессное приложение, моделирующее работу садовников.
Каждый садовник — это отдельный процесс.

---

## Файлы с кодом

### Первая программа (4-6 баллов)
- `gardeners.c` – монолитное приложение:
  - создаёт разделяемую память и неименованные семафоры;
  - порождает два дочерних процесса-садовника;
  - выводит итоговое состояние сада.

### Вторая программа (7-8 баллов)
- `garden_common.h` – общая структура `SharedData`, константы и вспомогательные функции.  
- `garden_ctl.c` – контроллер сада:
  - создаёт/инициализирует разделяемую память;
  - создаёт именованные семафоры для клеток;
  - выводит начальное/итоговое состояние, удаляет ресурсы.  
- `gardener1.c` – садовник 1, обход змейкой по строкам.  
- `gardener2.c` – садовник 2, обход змейкой по столбцам.  

---

## Краткое описание решения

### Общая модель

1. Сад задаётся структурой `SharedData` в POSIX-разделяемой памяти (`shm_open`, `ftruncate`, `mmap`).  
2. В `SharedData` хранятся:
   - размеры `M`, `N`;  
   - массив `cell_state[M][N]` (0 – пусто, -1 – препятствие, 1/2 – обработано садовником 1/2);  
   - времена задержек для прохода и обработки (`pass_time_us`, `work_time1_us`, `work_time2_us`);  
   - счётчики `processed1`, `processed2`;  
   - флаги `stop`, `initialized`, `finished1`, `finished2`.  
3. Препятствия (пруды/камни) генерируются случайно, 10-30 % клеток помечаются как недоступные.  
4. Для синхронизации доступа к каждой клетке используется семафор:
   - в первой программе неименованные `sem_t cell_sem[MAX_M][MAX_N]` в shared memory;  
   - во второй именованные POSIX-семафоры `/garden_cell_ii_jj` по одной штуке на клетку.  
5. Алгоритм обработки клетки:
   - если препятствие, то задержка прохода и переход дальше;  
   - `sem_wait` на семафор клетки;  
   - если клетка пустая, то пометка как обработанной данным садовником и задержка обработки;  
   - если уже обработана, то вывод сообщения `skipped` и задержка прохода;  
   - `sem_post` на семафор клетки.

---

### Первая программа (4–6 баллов)

1. **Родительский процесс**:
   - создаёт shared memory, инициализирует `SharedData`, неименованные семафоры;  
   - порождает два дочерних процесса (`fork()`);  
   - обрабатывает `SIGINT`: выставляет `stop`, посылает `SIGTERM` детям, затем очищает семафоры и shared memory (`sem_destroy`, `munmap`, `shm_unlink`).  
2. **Дочерний процесс – садовник 1**:
   - обходит сад змейкой по строкам: чётные строки слева направо, нечётные справа налево;  
   - для каждой клетки вызывает общую функцию обработки.  
3. **Дочерний процесс – садовник 2**:
   - обходит сад змейкой по столбцам: справа налево, для чётных по счёту столбцов снизу вверх, для нечётных сверху вниз.  
4. После завершения обоих дочерних процессов родитель выводит финальную матрицу сада и количество обработанных клеток каждым садовником.

**Способ запуска (Linux):**

```bash
gcc -std=c11 gardeners.c -pthread -o gardeners

# формат: ./gardeners [M N speed1 speed2]
./gardeners          # по умолчанию, M=10, N=10, speed1=3, speed2=2
./gardeners 8 12     # задать размеры сада
./gardeners 8 12 4 2 # задать размеры и относительные скорости обработки
```

---

### Вторая программа (7–8 баллов)

1. **`garden_ctl` (контроллер):**
   - очищает возможные старые объекты (`shm_unlink`, `sem_unlink`);  
   - создаёт shared memory и заполняет структуру `SharedData`;  
   - генерирует препятствия, инициализирует сад;  
   - создаёт именованные семафоры для всех клеток (`sem_open` с `O_CREAT`);  
   - выводит начальное состояние сада и устанавливает `initialized = 1`;  
   - ждёт до тех пор, пока не будут установлены `finished1` и `finished2` или `stop`;  
   - выводит финальное состояние сада и счётчики, закрывает и удаляет все семафоры и shared memory.  
2. **`gardener1`:**
   - подключается к существующей shared memory (`shm_open` без `O_CREAT`);  
   - ждёт, пока контроллер выставит `initialized = 1`;  
   - открывает все именованные семафоры клеток (`sem_open` без `O_CREAT`);  
   - обходит сад змейкой по строкам, выводя в свою консоль сообщения `Gardener 1 processed / skipped`;  
   - по завершении устанавливает `finished1 = 1`.  
3. **`gardener2`:**
   - аналогично подключается к shared memory и семафорам;  
   - обходит сад змейкой по столбцам, выводит `Gardener 2 processed / skipped`;  
   - по завершении устанавливает `finished2 = 1`.  
4. Во всех трёх программах предусмотрена обработка `SIGINT`: выставляется `stop`, циклы обхода корректно завершаются, ресурсы освобождаются.

**Способ запуска (Linux):**

Компиляция:

```bash
gcc -std=c11 garden_ctl.c -pthread -o garden_ctl
gcc -std=c11 gardener1.c -pthread -o gardener1
gcc -std=c11 gardener2.c -pthread -o gardener2
```

Запуск в разных терминалах:

1. Сначала контроллер:

   ```bash
   # формат: ./garden_ctl [M N speed1 speed2]
   ./garden_ctl 10 10 3 2
   ```

2. Затем, когда сад инициализирован:

   ```bash
   ./gardener1   # терминал садовника 1
   ./gardener2   # терминал садовника 2
   ```

При попытке запустить садовника до контроллера выводится сообщение об отсутствии shared memory и просьба сначала запустить `garden_ctl`.

---

## Результаты работы программ

### Пример прогона первой программы

   ```text
   root@c08d55dfcb6a:/usr/src/app# ./gardeners
   Garden size: 10x10, obstacles: 11 (11%)
   Initial garden state:
   # . . . . . . . # . 
   . . . . . . . . . . 
   . . . . . # # . . . 
   . . . . . . . # . . 
   . . . . . . . . . . 
   . # . . . . . . . . 
   . . . . . . . . # . 
   . . . . . . . . . . 
   # . . # . . . . # . 
   . . . # . . . . . . 
   Gardener 2 processed cell (9,9)
   Gardener 1 processed cell (0,1)
   Gardener 2 processed cell (8,9)
   Gardener 1 processed cell (0,2)
   Gardener 2 processed cell (7,9)
   Gardener 2 processed cell (6,9)
   Gardener 1 processed cell (0,3)
   Gardener 2 processed cell (5,9)
   Gardener 1 processed cell (0,4)
   Gardener 2 processed cell (4,9)
   Gardener 2 processed cell (3,9)
   Gardener 1 processed cell (0,5)
   Gardener 2 processed cell (2,9)
   Gardener 1 processed cell (0,6)
   Gardener 2 processed cell (1,9)
   Gardener 2 processed cell (0,9)
   Gardener 1 processed cell (0,7)
   Gardener 2 processed cell (1,8)
   Gardener 1 skipped cell (0,9), state=2
   Gardener 2 processed cell (2,8)
   Gardener 1 skipped cell (1,9), state=2
   Gardener 2 processed cell (3,8)
   Gardener 1 skipped cell (1,8), state=2
   Gardener 1 processed cell (1,7)
   Gardener 2 processed cell (4,8)
   Gardener 1 processed cell (1,6)
   Gardener 2 processed cell (5,8)
   Gardener 1 processed cell (1,5)
   Gardener 2 processed cell (7,8)
   Gardener 1 processed cell (1,4)
   Gardener 2 processed cell (9,8)
   Gardener 2 processed cell (9,7)
   Gardener 1 processed cell (1,3)
   Gardener 2 processed cell (8,7)
   Gardener 1 processed cell (1,2)
   Gardener 2 processed cell (7,7)
   Gardener 2 processed cell (6,7)
   Gardener 1 processed cell (1,1)
   Gardener 2 processed cell (5,7)
   Gardener 1 processed cell (1,0)
   Gardener 2 processed cell (4,7)
   Gardener 1 processed cell (2,0)
   Gardener 2 processed cell (2,7)
   Gardener 2 skipped cell (1,7), state=1
   Gardener 1 processed cell (2,1)
   Gardener 2 skipped cell (0,7), state=1
   Gardener 2 skipped cell (0,6), state=1
   Gardener 2 skipped cell (1,6), state=1
   Gardener 1 processed cell (2,2)
   Gardener 2 processed cell (3,6)
   Gardener 1 processed cell (2,3)
   Gardener 2 processed cell (4,6)
   Gardener 2 processed cell (5,6)
   Gardener 1 processed cell (2,4)
   Gardener 2 processed cell (6,6)
   Gardener 2 processed cell (7,6)
   Gardener 1 skipped cell (2,7), state=2
   Gardener 2 processed cell (8,6)
   Gardener 1 skipped cell (2,8), state=2
   Gardener 1 skipped cell (2,9), state=2
   Gardener 2 processed cell (9,6)
   Gardener 1 skipped cell (3,9), state=2
   Gardener 1 skipped cell (3,8), state=2
   Gardener 2 processed cell (9,5)
   Gardener 1 skipped cell (3,6), state=2
   Gardener 2 processed cell (8,5)
   Gardener 1 processed cell (3,5)
   Gardener 2 processed cell (7,5)
   Gardener 1 processed cell (3,4)
   Gardener 2 processed cell (6,5)
   Gardener 2 processed cell (5,5)
   Gardener 1 processed cell (3,3)
   Gardener 2 processed cell (4,5)
   Gardener 1 processed cell (3,2)
   Gardener 2 skipped cell (3,5), state=1
   Gardener 2 skipped cell (1,5), state=1
   Gardener 1 processed cell (3,1)
   Gardener 2 skipped cell (0,5), state=1
   Gardener 2 skipped cell (0,4), state=1
   Gardener 2 skipped cell (1,4), state=1
   Gardener 1 processed cell (3,0)
   Gardener 2 skipped cell (2,4), state=1
   Gardener 2 skipped cell (3,4), state=1
   Gardener 2 processed cell (4,4)
   Gardener 1 processed cell (4,0)
   Gardener 2 processed cell (5,4)
   Gardener 1 processed cell (4,1)
   Gardener 2 processed cell (6,4)
   Gardener 2 processed cell (7,4)
   Gardener 1 processed cell (4,2)
   Gardener 2 processed cell (8,4)
   Gardener 1 processed cell (4,3)
   Gardener 2 processed cell (9,4)
   Gardener 1 skipped cell (4,4), state=2
   Gardener 1 skipped cell (4,5), state=2
   Gardener 2 processed cell (7,3)
   Gardener 1 skipped cell (4,6), state=2
   Gardener 1 skipped cell (4,7), state=2
   Gardener 2 processed cell (6,3)
   Gardener 1 skipped cell (4,8), state=2
   Gardener 1 skipped cell (4,9), state=2
   Gardener 2 processed cell (5,3)
   Gardener 1 skipped cell (5,9), state=2
   Gardener 1 skipped cell (5,8), state=2
   Gardener 2 skipped cell (4,3), state=1
   Gardener 1 skipped cell (5,7), state=2
   Gardener 2 skipped cell (3,3), state=1
   Gardener 1 skipped cell (5,6), state=2
   Gardener 2 skipped cell (2,3), state=1
   Gardener 1 skipped cell (5,5), state=2
   Gardener 2 skipped cell (1,3), state=1
   Gardener 1 skipped cell (5,4), state=2
   Gardener 2 skipped cell (0,3), state=1
   Gardener 1 skipped cell (5,3), state=2
   Gardener 2 skipped cell (0,2), state=1
   Gardener 1 processed cell (5,2)
   Gardener 2 skipped cell (1,2), state=1
   Gardener 2 skipped cell (2,2), state=1
   Gardener 2 skipped cell (3,2), state=1
   Gardener 2 skipped cell (4,2), state=1
   Gardener 1 processed cell (5,0)
   Gardener 2 skipped cell (5,2), state=1
   Gardener 2 processed cell (6,2)
   Gardener 1 processed cell (6,0)
   Gardener 2 processed cell (7,2)
   Gardener 2 processed cell (8,2)
   Gardener 1 processed cell (6,1)
   Gardener 2 processed cell (9,2)
   Gardener 1 skipped cell (6,2), state=2
   Gardener 2 processed cell (9,1)
   Gardener 1 skipped cell (6,3), state=2
   Gardener 1 skipped cell (6,4), state=2
   Gardener 2 processed cell (8,1)
   Gardener 1 skipped cell (6,5), state=2
   Gardener 1 skipped cell (6,6), state=2
   Gardener 2 processed cell (7,1)
   Gardener 1 skipped cell (6,7), state=2
   Gardener 2 skipped cell (6,1), state=1
   Gardener 1 skipped cell (6,9), state=2
   Gardener 1 skipped cell (7,9), state=2
   Gardener 2 skipped cell (4,1), state=1
   Gardener 1 skipped cell (7,8), state=2
   Gardener 2 skipped cell (3,1), state=1
   Gardener 1 skipped cell (7,7), state=2
   Gardener 2 skipped cell (2,1), state=1
   Gardener 1 skipped cell (7,6), state=2
   Gardener 2 skipped cell (1,1), state=1
   Gardener 1 skipped cell (7,5), state=2
   Gardener 2 skipped cell (0,1), state=1
   Gardener 1 skipped cell (7,4), state=2
   Gardener 1 skipped cell (7,3), state=2
   Gardener 2 skipped cell (1,0), state=1
   Gardener 1 skipped cell (7,2), state=2
   Gardener 2 skipped cell (2,0), state=1
   Gardener 1 skipped cell (7,1), state=2
   Gardener 2 skipped cell (3,0), state=1
   Gardener 1 processed cell (7,0)
   Gardener 2 skipped cell (4,0), state=1
   Gardener 2 skipped cell (5,0), state=1
   Gardener 2 skipped cell (6,0), state=1
   Gardener 2 skipped cell (7,0), state=1
   Gardener 1 skipped cell (8,1), state=2
   Gardener 1 skipped cell (8,2), state=2
   Gardener 2 processed cell (9,0)
   Gardener 1 skipped cell (8,4), state=2
   Gardener 2 finished
   Gardener 1 skipped cell (8,5), state=2
   Gardener 1 skipped cell (8,6), state=2
   Gardener 1 skipped cell (8,7), state=2
   Gardener 1 skipped cell (8,9), state=2
   Gardener 1 skipped cell (9,9), state=2
   Gardener 1 skipped cell (9,8), state=2
   Gardener 1 skipped cell (9,7), state=2
   Gardener 1 skipped cell (9,6), state=2
   Gardener 1 skipped cell (9,5), state=2
   Gardener 1 skipped cell (9,4), state=2
   Gardener 1 skipped cell (9,2), state=2
   Gardener 1 skipped cell (9,1), state=2
   Gardener 1 skipped cell (9,0), state=2
   Gardener 1 finished
   Final garden state (M=10, N=10):
   # 1 1 1 1 1 1 1 # 2 
   1 1 1 1 1 1 1 1 2 2 
   1 1 1 1 1 # # 2 2 2 
   1 1 1 1 1 1 2 # 2 2 
   1 1 1 1 2 2 2 2 2 2 
   1 # 1 2 2 2 2 2 2 2 
   1 1 2 2 2 2 2 2 # 2 
   1 2 2 2 2 2 2 2 2 2 
   # 2 2 # 2 2 2 2 # 2 
   2 2 2 # 2 2 2 2 2 2 
   Processed by gardener 1: 35
   Processed by gardener 2: 54
   ```

### Пример прогона второй программы

   1. В терминале контроллера:

   ```text
   root@15d004aa7db5:/usr/src/app# ./garden_ctl 10 10 3 2
   Garden size: 10x10, obstacles: 21 (21%)
   Initial garden state:
   . # . . . . . . . . 
   . # . . . # . # . . 
   # . # . . . # . . . 
   . . # . . # . # # . 
   . # . . . . . . . . 
   . . . # . . . . . . 
   . . . . . # . . . # 
   . . . . # # . . . . 
   . . . . . . . . . . 
   # . . # . . # . # . 
   Controller: garden initialized.
   Run ./gardener1 and ./gardener2 in other terminals.
   Controller: finishing...
   Final garden state (M=10, N=10):
   1 # 1 1 1 1 1 1 1 1 
   1 # 1 1 1 # 1 # 1 1 
   # 1 # 1 1 1 # 2 2 2 
   1 1 # 1 1 # 2 # # 2 
   1 # 1 1 1 2 2 2 2 2 
   1 1 1 # 2 2 2 2 2 2 
   1 1 1 2 2 # 2 2 2 # 
   1 2 2 2 # # 2 2 2 2 
   1 2 2 2 2 2 2 2 2 2 
   # 2 2 # 2 2 # 2 # 2 
   Processed by gardener 1: 36
   Processed by gardener 2: 43
   ```

   2. В терминале садовника 1:

   ```text
   root@15d004aa7db5:/usr/src/app# ./gardener1
   Gardener 1: waiting for controller to initialize...
   Gardener 1: started.
   Gardener 1 processed cell (0,0)
   Gardener 1 processed cell (0,2)
   Gardener 1 processed cell (0,3)
   Gardener 1 processed cell (0,4)
   Gardener 1 processed cell (0,5)
   Gardener 1 processed cell (0,6)
   Gardener 1 processed cell (0,7)
   Gardener 1 processed cell (0,8)
   Gardener 1 processed cell (0,9)
   Gardener 1 processed cell (1,9)
   Gardener 1 processed cell (1,8)
   Gardener 1 processed cell (1,6)
   Gardener 1 processed cell (1,4)
   Gardener 1 processed cell (1,3)
   Gardener 1 processed cell (1,2)
   Gardener 1 processed cell (1,0)
   Gardener 1 processed cell (2,1)
   Gardener 1 processed cell (2,3)
   Gardener 1 processed cell (2,4)
   Gardener 1 processed cell (2,5)
   Gardener 1 skipped cell (2,7), state=2
   Gardener 1 skipped cell (2,8), state=2
   Gardener 1 skipped cell (2,9), state=2
   Gardener 1 skipped cell (3,9), state=2
   Gardener 1 skipped cell (3,6), state=2
   Gardener 1 processed cell (3,4)
   Gardener 1 processed cell (3,3)
   Gardener 1 processed cell (3,1)
   Gardener 1 processed cell (3,0)
   Gardener 1 processed cell (4,0)
   Gardener 1 processed cell (4,2)
   Gardener 1 processed cell (4,3)
   Gardener 1 processed cell (4,4)
   Gardener 1 skipped cell (4,5), state=2
   Gardener 1 skipped cell (4,6), state=2
   Gardener 1 skipped cell (4,7), state=2
   Gardener 1 skipped cell (4,8), state=2
   Gardener 1 skipped cell (4,9), state=2
   Gardener 1 skipped cell (5,9), state=2
   Gardener 1 skipped cell (5,8), state=2
   Gardener 1 skipped cell (5,7), state=2
   Gardener 1 skipped cell (5,6), state=2
   Gardener 1 skipped cell (5,5), state=2
   Gardener 1 skipped cell (5,4), state=2
   Gardener 1 processed cell (5,2)
   Gardener 1 processed cell (5,1)
   Gardener 1 processed cell (5,0)
   Gardener 1 processed cell (6,0)
   Gardener 1 processed cell (6,1)
   Gardener 1 processed cell (6,2)
   Gardener 1 skipped cell (6,3), state=2
   Gardener 1 skipped cell (6,4), state=2
   Gardener 1 skipped cell (6,6), state=2
   Gardener 1 skipped cell (6,7), state=2
   Gardener 1 skipped cell (6,8), state=2
   Gardener 1 skipped cell (7,9), state=2
   Gardener 1 skipped cell (7,8), state=2
   Gardener 1 skipped cell (7,7), state=2
   Gardener 1 skipped cell (7,6), state=2
   Gardener 1 skipped cell (7,3), state=2
   Gardener 1 skipped cell (7,2), state=2
   Gardener 1 skipped cell (7,1), state=2
   Gardener 1 processed cell (7,0)
   Gardener 1 processed cell (8,0)
   Gardener 1 skipped cell (8,1), state=2
   Gardener 1 skipped cell (8,2), state=2
   Gardener 1 skipped cell (8,3), state=2
   Gardener 1 skipped cell (8,4), state=2
   Gardener 1 skipped cell (8,5), state=2
   Gardener 1 skipped cell (8,6), state=2
   Gardener 1 skipped cell (8,7), state=2
   Gardener 1 skipped cell (8,8), state=2
   Gardener 1 skipped cell (8,9), state=2
   Gardener 1 skipped cell (9,9), state=2
   Gardener 1 skipped cell (9,7), state=2
   Gardener 1 skipped cell (9,5), state=2
   Gardener 1 skipped cell (9,4), state=2
   Gardener 1 skipped cell (9,2), state=2
   Gardener 1 skipped cell (9,1), state=2
   Gardener 1 finished
   ```

   3. В терминале садовника 2:

   ```text
   root@15d004aa7db5:/usr/src/app# ./gardener2
   Gardener 2: waiting for controller to initialize...
   Gardener 2: started.
   Gardener 2 processed cell (9,9)
   Gardener 2 processed cell (8,9)
   Gardener 2 processed cell (7,9)
   Gardener 2 processed cell (5,9)
   Gardener 2 processed cell (4,9)
   Gardener 2 processed cell (3,9)
   Gardener 2 processed cell (2,9)
   Gardener 2 skipped cell (1,9), state=1
   Gardener 2 skipped cell (0,9), state=1
   Gardener 2 skipped cell (0,8), state=1
   Gardener 2 skipped cell (1,8), state=1
   Gardener 2 processed cell (2,8)
   Gardener 2 processed cell (4,8)
   Gardener 2 processed cell (5,8)
   Gardener 2 processed cell (6,8)
   Gardener 2 processed cell (7,8)
   Gardener 2 processed cell (8,8)
   Gardener 2 processed cell (9,7)
   Gardener 2 processed cell (8,7)
   Gardener 2 processed cell (7,7)
   Gardener 2 processed cell (6,7)
   Gardener 2 processed cell (5,7)
   Gardener 2 processed cell (4,7)
   Gardener 2 processed cell (2,7)
   Gardener 2 skipped cell (0,7), state=1
   Gardener 2 skipped cell (0,6), state=1
   Gardener 2 skipped cell (1,6), state=1
   Gardener 2 processed cell (3,6)
   Gardener 2 processed cell (4,6)
   Gardener 2 processed cell (5,6)
   Gardener 2 processed cell (6,6)
   Gardener 2 processed cell (7,6)
   Gardener 2 processed cell (8,6)
   Gardener 2 processed cell (9,5)
   Gardener 2 processed cell (8,5)
   Gardener 2 processed cell (5,5)
   Gardener 2 processed cell (4,5)
   Gardener 2 skipped cell (2,5), state=1
   Gardener 2 skipped cell (0,5), state=1
   Gardener 2 skipped cell (0,4), state=1
   Gardener 2 skipped cell (1,4), state=1
   Gardener 2 skipped cell (2,4), state=1
   Gardener 2 skipped cell (3,4), state=1
   Gardener 2 skipped cell (4,4), state=1
   Gardener 2 processed cell (5,4)
   Gardener 2 processed cell (6,4)
   Gardener 2 processed cell (8,4)
   Gardener 2 processed cell (9,4)
   Gardener 2 processed cell (8,3)
   Gardener 2 processed cell (7,3)
   Gardener 2 processed cell (6,3)
   Gardener 2 skipped cell (4,3), state=1
   Gardener 2 skipped cell (3,3), state=1
   Gardener 2 skipped cell (2,3), state=1
   Gardener 2 skipped cell (1,3), state=1
   Gardener 2 skipped cell (0,3), state=1
   Gardener 2 skipped cell (0,2), state=1
   Gardener 2 skipped cell (1,2), state=1
   Gardener 2 skipped cell (4,2), state=1
   Gardener 2 skipped cell (5,2), state=1
   Gardener 2 skipped cell (6,2), state=1
   Gardener 2 processed cell (7,2)
   Gardener 2 processed cell (8,2)
   Gardener 2 processed cell (9,2)
   Gardener 2 processed cell (9,1)
   Gardener 2 processed cell (8,1)
   Gardener 2 processed cell (7,1)
   Gardener 2 skipped cell (6,1), state=1
   Gardener 2 skipped cell (5,1), state=1
   Gardener 2 skipped cell (3,1), state=1
   Gardener 2 skipped cell (2,1), state=1
   Gardener 2 skipped cell (0,0), state=1
   Gardener 2 skipped cell (1,0), state=1
   Gardener 2 skipped cell (3,0), state=1
   Gardener 2 skipped cell (4,0), state=1
   Gardener 2 skipped cell (5,0), state=1
   Gardener 2 skipped cell (6,0), state=1
   Gardener 2 skipped cell (7,0), state=1
   Gardener 2 skipped cell (8,0), state=1
   Gardener 2 finished
   ```

---

## Дополнительная информация

### Под требования на 4-6 баллов
- Используются неименованные POSIX-семафоры (`sem_init`, `sem_wait`, `sem_post`, `sem_destroy`) в разделяемой памяти.  
- Обмен данными осуществляется через POSIX shared memory (`shm_open`, `mmap`).  
- Один родительский процесс и два дочерних процесса-садовника.  
- Реализовано корректное завершение по окончанию работы и по `SIGINT` с освобождением всех ресурсов.

### Под требования на 7-8 баллов
- Разработано приложение из отдельных программ-процессов (`garden_ctl`, `gardener1`, `gardener2`), запускаемых независимо в разных консолях.  
- Взаимодействие процессов реализовано через POSIX shared memory и именованные POSIX-семафоры для каждой клетки сада (`sem_open`, `sem_close`, `sem_unlink`).  
- Каждый процесс ведёт собственный вывод:
  - контроллер: матрица сада и итоговая статистика;  
  - `gardener1`: ход работы первого садовника;  
  - `gardener2`: ход работы второго садовника.  
- Реализована очистка всех семафоров и shared memory при штатном завершении и при прерывании с клавиатуры.
