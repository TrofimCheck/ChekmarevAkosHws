// message.h
#ifndef MESSAGE_H
#define MESSAGE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <stdio.h>

#define PERMS   0666 // права доступа

// коды сообщений
#define MSG_TYPE_EMPTY  0 // в буфере ничего нет
#define MSG_TYPE_NUMBER 1 // передано число (в строковом виде)
#define MSG_TYPE_FINISH 2 // сообщение о завершении обмена
#define MAX_STRING 120 // максимальная длина текстового сообщения

// структура сообщения, помещаемого в разделяемую память
typedef struct {
    int  type; // тип сообщения
    char string[MAX_STRING]; // число в текстовом виде
    pid_t client_pid; // PID клиента
    pid_t server_pid; // PID сервера
} message_t;

#endif
