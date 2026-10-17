#pragma once
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <io.h>
#include <fcntl.h>
#include <time.h>

#define TAM 200
#define MAX_PLACARES 20
#define NOME_SHM _T("Local\\SO2_SHM_ALERTAS")
#define NOME_MUTEX _T("Local\\SO2_MUTEX_ALERTAS")
#define NOME_EVENTO_UPDATE _T("Local\\SO2_EVENT_UPDATE")


typedef struct {
	BYTE tipo; // 1 – ligar, 2 – desligar, 3 – fim do alerta (duração) 5 – cancelar alerta, 6 – encerrar plataforma
} MSG_CMD;

typedef struct {
	BYTE tipo; // 4 – novo alerta
	TCHAR msg[140];
	DWORD duracao;
} MSG_ALERTA;


typedef struct {
	BYTE tipo; // 7 – atribuir identificador
	DWORD identificador;
} MSG_ID;

typedef struct {
	TCHAR nomePipe[TAM];
	HANDLE hEventoNotificar;
	HANDLE hEventoTerminar;
	HANDLE hTimer;
	BOOL* pContinuar;
} TD_PLACAR;

typedef struct {
	TCHAR pipePath[TAM];
	HANDLE* hPipe;        // Ponteiro para o handle que está no main
	BOOL* continua;
	HANDLE hEventPrincipal; // hEvent que o main usa no WaitForMultipleObjects
} TD_CONSOLA;


typedef struct {
	struct {
		DWORD identificador, duracao;
		TCHAR msg[140];
	} placar[20];
	BOOL desligar;
} SHM_ALERTA;

typedef struct {
	DWORD id;           
	HANDLE hPipe;
	TCHAR msg[TAM];
	DWORD duracao;
} PLACAR;


typedef struct {
	PLACAR placares[MAX_PLACARES];
	DWORD numPlacares;
	DWORD contadorID;
	BOOL* continua;
	HANDLE hEventoSair;

	HANDLE hMapFile;
	SHM_ALERTA* pShm; // Agora o compilador já conhece o SHM_ALERTA
	HANDLE hMutex;
	HANDLE hEventUpdate;
} DADOS_CENTRAL;


typedef struct {
	HANDLE hPipe;
	DADOS_CENTRAL* dc;
	DWORD idPlacar;
} TD_PLACAR_CENTRAL;


