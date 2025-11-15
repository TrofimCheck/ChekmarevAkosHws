/*
 * Чекмарев Трофим Владимирович
 * Группа БПИ-245
 * Домашнее задание 7
 */

// Программа, выполненная на 8 баллов, для побайтного копирования файлов с использованием только системных вызовов ОС
#include <unistd.h> // read, write, close, fchmod
#include <fcntl.h> // open
#include <sys/types.h>  // типы
#include <sys/stat.h> // fstat, struct stat
#include <cstdio> // perror
#include <cstring> // strlen
#include <cstdlib> // exit

int main(int argc, char* argv[]) {
    if (argc != 3) {
        const char* msg = "Usage: copyfile <input_file> <output_file>\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }
    const char* inputName  = argv[1];
    const char* outputName = argv[2];
    // Открываем входной файл
    int inFd = open(inputName, O_RDONLY);
    if (inFd == -1) {
        perror("Error opening input file");
        return 1;
    }
    // Узнаём права исходного файла
    struct stat st;
    if (fstat(inFd, &st) == -1) {
        perror("Error fstat on input file");
        close(inFd);
        return 1;
    }
    // Берём только биты прав
    mode_t srcMode = st.st_mode & 07777;
    // Открываем выходной файл
    int outFd = open(outputName, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (outFd == -1) {
        perror("Error opening output file");
        close(inFd);
        return 1;
    }
    // Циклический буфер 32 байта
    static const size_t BUF_SIZE = 32;
    char buffer[BUF_SIZE];
    size_t head = 0; // откуда пишем в выходной файл
    size_t tail = 0; // куда читаем новые данные
    size_t bytesInBuffer = 0; // сколько байт сейчас в буфере
    bool eof = false; // достигнут ли конец входного файла
    while (!eof || bytesInBuffer > 0) {
        // Чтение в свободную часть буфера
        if (!eof && bytesInBuffer < BUF_SIZE) {
            size_t freeSpace = BUF_SIZE - bytesInBuffer;
            // Cколько можно прочитать подряд до конца массива
            size_t contiguousSpace = BUF_SIZE - tail;
            if (contiguousSpace > freeSpace) {
                contiguousSpace = freeSpace;
            }
            ssize_t r = read(inFd, buffer + tail, contiguousSpace);
            if (r == -1) {
                perror("Error reading input file");
                close(inFd);
                close(outFd);
                return 1;
            }
            if (r == 0) {
                eof = true;
            } else {
                bytesInBuffer += static_cast<size_t>(r);
                tail = (tail + static_cast<size_t>(r)) % BUF_SIZE;
            }
        }
        // Запись данных из буфера в выходной файл
        if (bytesInBuffer > 0) {
            size_t contiguousData = BUF_SIZE - head;
            if (contiguousData > bytesInBuffer) {
                contiguousData = bytesInBuffer;
            }
            ssize_t w = write(outFd, buffer + head, contiguousData);
            if (w == -1) {
                perror("Error writing output file");
                close(inFd);
                close(outFd);
                return 1;
            }
            bytesInBuffer -= static_cast<size_t>(w);
            head = (head + static_cast<size_t>(w)) % BUF_SIZE;
        }
    }
    // Копируем права доступа
    // Если исходный файл был исполняемым, то и копия будет исполняемой
    // Если исходный файл не был исполняемым, копия тоже останется неисполняемой
    if (fchmod(outFd, srcMode) == -1) {
        perror("Error setting permissions on output file");
        // Само содержимое уже скопировано, но права могли не установиться.
        close(inFd);
        close(outFd);
        return 1;
    }
    close(inFd);
    close(outFd);
    return 0;
}
