#include "../comum.h"

PLACAR* devolvePlacarPorId(DADOS_CENTRAL* dc, DWORD id) {

	PLACAR* res = NULL;
	for (DWORD i = 0; i < dc->numPlacares; i++) {
		if (dc->placares[i].id == id) {
			res = &dc->placares[i];
			break;
		}
	}
	return res;
}

PLACAR* devolvePlacarPorHandle(DADOS_CENTRAL* dc, HANDLE hPipe) {
	PLACAR* res = NULL;
	for (DWORD i = 0; i < dc->numPlacares; i++) {
		if (dc->placares[i].hPipe == hPipe) {
			res = &dc->placares[i];
			break;
		}
	}
	return res;
}
void atualizaSHM(DADOS_CENTRAL* dc) {
	if (dc == NULL || dc->hMutex == NULL || dc->pShm == NULL) return;


	WaitForSingleObject(dc->hMutex, INFINITE);

	ZeroMemory(dc->pShm, sizeof(SHM_ALERTA));

	for (DWORD i = 0; i < dc->numPlacares; i++) {
		dc->pShm->placar[i].identificador = dc->placares[i].id;
		dc->pShm->placar[i].duracao = dc->placares[i].duracao;
		_tcscpy_s(dc->pShm->placar[i].msg, 140, dc->placares[i].msg);
	}

	dc->pShm->desligar = !(*(dc->continua));

	ReleaseMutex(dc->hMutex);


	if (dc->hEventUpdate) {
		SetEvent(dc->hEventUpdate);
		// Sleep(20);                  // Pequena pausa para o Monitor acordar e copiar os dados
		// ResetEvent(dc->hEventUpdate);
	}


}

void removerPlacarPorId(DADOS_CENTRAL* dc, DWORD id) {
	for (DWORD i = 0; i < dc->numPlacares; i++) {
		_tprintf_s(_T("i: %u \n"), i);
		if (dc->placares[i].id == id) {

			for (DWORD j = i; j < dc->numPlacares - 1; j++) {
				dc->placares[j] = dc->placares[j + 1];
			}
			dc->numPlacares--;

			dc->placares[dc->numPlacares].id = 0;

			CloseHandle(dc->placares[dc->numPlacares].hPipe);

			dc->placares[dc->numPlacares].hPipe = INVALID_HANDLE_VALUE;

			return;
		}
	}
	_tprintf_s(_T("[SISTEMA] Erro: Placar %u não encontrado.\n"), id);
}

void listarPlacares(DADOS_CENTRAL* dc) {
	for (DWORD i = 0; i < dc->numPlacares; i++) {
		if (_tcslen(dc->placares[i].msg) > 0)
			_tprintf_s(_T("Placar %u - %s (%u segundos)\n"), dc->placares[i].id, dc->placares[i].msg, dc->placares[i].duracao);
		else
			_tprintf_s(_T("Placar %u - '---' (0 segundos)\n"), dc->placares[i].id);
	}
}

BOOL handleMensagem(HANDLE hPipe, MSG_CMD* msgRecebida, DADOS_CENTRAL* dc) {
	DWORD nBytes;
	PLACAR* ptdPlacar;


	// 1 - Ligar
	if (msgRecebida->tipo == 1) {
		_tprintf_s(_T("[DEBUG] Ligou-se um placar...\n"));
		_tprintf_s(_T("[DEBUG] Recebi %u bytes do placar... MSG_CMD (tipo = %u).\n"), sizeof(*msgRecebida), msgRecebida->tipo);

		MSG_ID msgEnviada;
		PLACAR* placar = NULL;

		OVERLAPPED ov = { 0 };
		ZeroMemory(&ov, sizeof(ov));
		ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		EnterCriticalSection(&dc->csPlacares);

		// Se a main incrementou primeiro, o placar atual está em numPlacares - 1

		placar = &dc->placares[dc->numPlacares - 1];
		placar->id = dc->contadorID;
		placar->hPipe = hPipe;
		placar->msg[0] = _T('\0');
		placar->duracao = 0;

		dc->contadorID++;

		_tprintf_s(_T("[DEBUG] Atribuí o identificador %u...\n"), placar->id);

		msgEnviada.tipo = 7;
		msgEnviada.identificador = placar->id;



		if (!WriteFile(hPipe, &msgEnviada, sizeof(msgEnviada), &nBytes, &ov)) {
			if (GetLastError() == ERROR_IO_PENDING) {
				// Espera que a escrita termine
				WaitForSingleObject(ov.hEvent, INFINITE);
			}
			else {
				_tprintf_s(_T("[CENTRAL - handleMensagem] Erro no WriteFile.\n"));
				CloseHandle(ov.hEvent);
				LeaveCriticalSection(&dc->csPlacares);
				return FALSE;
			}

		}
		CloseHandle(ov.hEvent);

		_tprintf_s(_T("[DEBUG] Enviei %u bytes ao placar %d... MSG_CMD (tipo = %u)\n"), sizeof(msgEnviada), placar->id, msgEnviada.tipo);
		atualizaSHM(dc);
		LeaveCriticalSection(&dc->csPlacares);
	}
	else if (msgRecebida->tipo == 2) {
		MSG_CMD msgDesligar;
		DWORD nbytes, id;
		msgDesligar.tipo = 2;


		EnterCriticalSection(&dc->csPlacares);
		ptdPlacar = devolvePlacarPorHandle(dc, hPipe);
		if (ptdPlacar == NULL) {
			_tprintf_s(_T("Placar desconhecido...\n"));
			LeaveCriticalSection(&dc->csPlacares);
			return FALSE;
		}
		id = ptdPlacar->id;
		removerPlacarPorId(dc, ptdPlacar->id);
		_tprintf_s(_T("[DEBUG] Recebi %u byte do placar %u... MSG_CMD (tipo = %u)\n"), sizeof(*msgRecebida), id, msgRecebida->tipo);

		OVERLAPPED ov = { 0 };
		ZeroMemory(&ov, sizeof(ov));
		ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		if (!WriteFile(hPipe, &msgDesligar, sizeof(msgDesligar), &nbytes, &ov)) {
			if (GetLastError() == ERROR_IO_PENDING) {
				// Espera que a escrita termine
				WaitForSingleObject(ov.hEvent, INFINITE);
			}
			else {
				_tprintf_s(_T("[CENTRAL - handleMensagem] Erro real no WriteFile: %d\n"), GetLastError());
				CloseHandle(ov.hEvent);
				LeaveCriticalSection(&dc->csPlacares);
				return FALSE;
			}
		}

		CloseHandle(ov.hEvent);

		_tprintf_s(_T("[DEBUG] Enviei %u byte ao placar %u... MSG_CMD (tipo = %u)\n"), nbytes, id, msgDesligar.tipo);
		_tprintf_s(_T("[DEBUG] Desliguei/removi o placar %u...\n"), id);

		atualizaSHM(dc);
		LeaveCriticalSection(&dc->csPlacares);

	}
	else if (msgRecebida->tipo == 3) {
		EnterCriticalSection(&dc->csPlacares);
		ptdPlacar = devolvePlacarPorHandle(dc, hPipe);
		// Se o placar não for encontrado, algo está muito errado (deveria ser impossível)

		if (ptdPlacar) {
			_tprintf_s(_T("\n[DEBUG] Recebi %u byte do placar %u... MSG_CMD (tipo = %u)\n"),
				sizeof(*msgRecebida), ptdPlacar->id, msgRecebida->tipo);
			ptdPlacar->msg[0] = _T('\0');
			ptdPlacar->duracao = 0;
			atualizaSHM(dc);
			_tprintf_s(_T("[DEBUG] Duracao do alerta do placar 2 terminou...\n"));
		}
		else {
			_tprintf_s(_T("\n[CENTRAL - handleMensagem] Alerta terminado recebido de um placar desconhecido.\n"));
		}
		LeaveCriticalSection(&dc->csPlacares);

		return TRUE;

	}
	else if (msgRecebida->tipo == 4) {
		MSG_ALERTA* confirmacao = (MSG_ALERTA*)msgRecebida;
		EnterCriticalSection(&dc->csPlacares);

		PLACAR* ptdPlacar = devolvePlacarPorHandle(dc, hPipe);

		if (ptdPlacar) {
			_tcscpy_s(ptdPlacar->msg, _countof(ptdPlacar->msg), confirmacao->msg);
			ptdPlacar->duracao = confirmacao->duracao;

			_tprintf_s(_T("\n[DEBUG] Recebi %u bytes do placar %u... MSG_ALERTA (tipo = %u)\n"),
				sizeof(*confirmacao), ptdPlacar->id, msgRecebida->tipo);
			_tprintf_s(_T("[DEBUG] Placar %u com alerta ativo...\n"), ptdPlacar->id);
			atualizaSHM(dc);
		}


		LeaveCriticalSection(&dc->csPlacares);

		return TRUE;
	}
	else if (msgRecebida->tipo == 5) {
		EnterCriticalSection(&dc->csPlacares);
		ptdPlacar = devolvePlacarPorHandle(dc, hPipe);

		if (ptdPlacar) {
			_tprintf_s(_T("\n[DEBUG] Recebi %u byte do placar %u... MSG_CMD (tipo = %u)\n"), sizeof(*msgRecebida), ptdPlacar->id, msgRecebida->tipo);
			_tprintf_s(_T("[DEBUG] Cancelado alerta do placar 1...\n"));
			ptdPlacar->msg[0] = _T('\0');
			ptdPlacar->duracao = 0;
			atualizaSHM(dc);
		}
		else {
			_tprintf_s(_T("\n[CENTRAL - handleMensagem] Confirmação do cancelamento de alerta recebido de um placar desconhecido.\n"));
		}
		LeaveCriticalSection(&dc->csPlacares);
	}

	return TRUE;
}

DWORD WINAPI atendePlacar(LPVOID param) {
	TD_PLACAR_CENTRAL* td = (TD_PLACAR_CENTRAL*)param;
	BYTE buffer[sizeof(MSG_ALERTA)];
	DWORD nBytes;
	OVERLAPPED ov;

	ZeroMemory(&ov, sizeof(ov));
	ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// Criamos um array com os handles que queremos monitorizar
	HANDLE eventos[2] = { ov.hEvent, td->dc->hEventoSair };

	while (td->dc->continua) {

		nBytes = 0;
		ResetEvent(ov.hEvent);

		BOOL lido = ReadFile(td->hPipe, buffer, sizeof(buffer), &nBytes, &ov);

		if (!lido && GetLastError() == ERROR_IO_PENDING) {
			// Espera por qualquer um dos dois eventos
			DWORD waitRes = WaitForMultipleObjects(2, eventos, FALSE, INFINITE);

			if (waitRes == WAIT_OBJECT_0 + 1) {
				// O evento de SAIR foi sinalizado!
				_tprintf_s(_T("[THREAD] A encerrar via evento.\n"));
				break;
			}

			// Se chegou aqui, foi o ov.hEvent (dados no pipe)
			GetOverlappedResult(td->hPipe, &ov, &nBytes, FALSE);
		}

		if (nBytes > 0) {
			BOOL continuar = handleMensagem(td->hPipe, (MSG_CMD*)buffer, td->dc);
			if (!continuar)
				break;
		}
	}
	CancelIo(td->hPipe);
	CloseHandle(ov.hEvent);
	return 0;
}
DWORD WINAPI threadAdmin(LPVOID param) {
	DADOS_CENTRAL* dc = (DADOS_CENTRAL*)param;
	TCHAR linha[TAM];
	TCHAR cmd[TAM], msgAlerta[TAM];
	DWORD idPlacar, duracao;
	HANDLE hPipe;
	PLACAR* ptdPlacar;
	MSG_ALERTA alerta, resposta;

	int lidos;

	while (dc->continua) {
		_tprintf_s(_T("\n>"));

		hPipe = INVALID_HANDLE_VALUE;
		ZeroMemory(&alerta, sizeof(alerta));
		ZeroMemory(&resposta, sizeof(resposta));
		ptdPlacar = NULL;

		if (_fgetts(linha, TAM, stdin) == NULL) continue;

		if (_stscanf_s(linha, _T("%s"), cmd, (unsigned)_countof(cmd)) <= 0) continue;

		if (_tcscmp(cmd, _T("alerta")) == 0) {
			// O %*s serve para saltar a primeira palavra (o comando já lido)
			lidos = _stscanf_s(linha, _T("%*s %s %u %u"),
				msgAlerta, (unsigned)_countof(msgAlerta),
				&duracao, &idPlacar);

			// _tprintf_s(_T("MSG: %s \t dur: %u \t id: %u \t\n"), msgAlerta, duracao, idPlacar);

			if (lidos == 3) {
				HANDLE hPipeDestino = INVALID_HANDLE_VALUE;
				OVERLAPPED ovWrite;
				ZeroMemory(&ovWrite, sizeof(ovWrite));
				ovWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

				// Enviar msg do tipo 4
				alerta.tipo = 4;
				alerta.duracao = duracao;
				_tcscpy_s(alerta.msg, 140, msgAlerta);
				EnterCriticalSection(&dc->csPlacares);

				ptdPlacar = devolvePlacarPorId(dc, idPlacar);

				if (ptdPlacar != NULL) {
					hPipeDestino = ptdPlacar->hPipe;
				}
				LeaveCriticalSection(&dc->csPlacares);

				if (hPipeDestino == INVALID_HANDLE_VALUE) {
					_tprintf_s(_T("[ADMIN] Placar não encontrado.\n"));
					continue;
				}

				if (!WriteFile(hPipeDestino, &alerta, sizeof(MSG_ALERTA), NULL, &ovWrite)) {
					if (GetLastError() == ERROR_IO_PENDING) {
						// Espera o envio completar
						WaitForSingleObject(ovWrite.hEvent, INFINITE);
					}
					else {
						_tprintf_s(_T("Erro real no WriteFile: %d\n"), GetLastError());
						CloseHandle(ovWrite.hEvent);
						continue;
					}
				}

				CloseHandle(ovWrite.hEvent);
				_tprintf_s(_T("[DEBUG] Enviei %u bytes ao placar %u... MSG_ALERTA (tipo = %u).\n"), sizeof(alerta), idPlacar, alerta.tipo);

				EnterCriticalSection(&dc->csPlacares);
				ptdPlacar = devolvePlacarPorId(dc, idPlacar);
				if (ptdPlacar != NULL) {
					_tcscpy_s(ptdPlacar->msg, _countof(ptdPlacar->msg), msgAlerta);
					ptdPlacar->duracao = duracao;
				}
				atualizaSHM(dc); // Garante que a SHM reflete o novo alerta de imediato
				LeaveCriticalSection(&dc->csPlacares);

			}
			else {
				_tprintf_s(_T("[THREAD-ADMIN] Erro. Uso: alerta <msg> <duração> <id_placar>\n"));
			}
		}

		else if (_tcscmp(cmd, _T("cancelar")) == 0) {
			// O %*s serve para saltar a primeira palavra (o comando já lido)
			lidos = _stscanf_s(linha, _T("%*s %u"), &idPlacar);

			if (lidos == 1) {
				HANDLE hPipeDestino = INVALID_HANDLE_VALUE;
				MSG_CMD msgCancelar;
				msgCancelar.tipo = 5;

				OVERLAPPED ovWrite;
				ZeroMemory(&ovWrite, sizeof(ovWrite));
				ovWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

				EnterCriticalSection(&dc->csPlacares);

				ptdPlacar = devolvePlacarPorId(dc, idPlacar);
				hPipeDestino = ptdPlacar->hPipe;

				LeaveCriticalSection(&dc->csPlacares);

				if (hPipeDestino == INVALID_HANDLE_VALUE) {
					_tprintf_s(_T("[THREAD-ADMIN] Placar %u não encontrado.\n"), idPlacar);

					continue;
				}

				if (!WriteFile(hPipeDestino, &msgCancelar, sizeof(msgCancelar), NULL, &ovWrite)) {
					if (GetLastError() == ERROR_IO_PENDING) {
						WaitForSingleObject(ovWrite.hEvent, INFINITE);
					}
					else {
						_tprintf_s(_T("Erro ao enviar mensagem de cancelamento ao placar.\n"));
						CloseHandle(ovWrite.hEvent);
						continue;
					}
				}
				CloseHandle(ovWrite.hEvent);
				_tprintf_s(_T("[DEBUG] Enviei %u byte ao placar %u... MSG_CMD (tipo = %u)"), sizeof(msgCancelar), idPlacar, msgCancelar.tipo);
			}
			else {
				_tprintf_s(_T("[THREAD-ADMIN] Erro. Uso: cancelar <id_placar>\n"));
			}
		}

		else if (_tcscmp(cmd, _T("listar")) == 0) {
			EnterCriticalSection(&dc->csPlacares);
			listarPlacares(dc);
			LeaveCriticalSection(&dc->csPlacares);
		}

		else if (_tcscmp(cmd, _T("encerrar")) == 0) {
			_tprintf_s(_T("[THREAD-ADMIN] A encerrar o sistema...\n"));
			MSG_CMD msgEncerrar;
			msgEncerrar.tipo = 6;

			OVERLAPPED ovWrite;
			ZeroMemory(&ovWrite, sizeof(ovWrite));
			ovWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			EnterCriticalSection(&dc->csPlacares);

			for (int i = 0; i < dc->numPlacares; ++i) {
				if (!WriteFile(dc->placares[i].hPipe, &msgEncerrar, sizeof(msgEncerrar), NULL, &ovWrite)) {
					if (GetLastError() == ERROR_IO_PENDING) {
						WaitForSingleObject(ovWrite.hEvent, INFINITE);
					}
					else {
						_tprintf_s(_T("Erro real no WriteFile: %d\n"), GetLastError());
					}
				}
				_tprintf_s(_T("[DEBUG] Enviei mensagem de encerrar ao placar %u\n"), dc->placares[i].id);
				CloseHandle(dc->placares[i].hPipe);
				dc->placares[i].hPipe = INVALID_HANDLE_VALUE;
			}
			dc->numPlacares = 0; // Limpa tudo de uma vez

			dc->continua = FALSE;
			LeaveCriticalSection(&dc->csPlacares);
			CloseHandle(ovWrite.hEvent);

			atualizaSHM(dc);
			//Sleep(500);
			SetEvent(dc->hEventoSair);

			_tprintf_s(_T("[DEBUG] A encerrar plataforma...\n"));
			break;
}

		else {
			_tprintf_s(_T("[THREAD-ADMIN] Comando '%s' não reconhecido.\n"), cmd);
		}
	}

	return 0;
}

int _tmain(int argc, TCHAR* argv[]) {
#ifdef UNICODE
	_setmode(_fileno(stdin), _O_WTEXT);
	_setmode(_fileno(stdout), _O_WTEXT);
#endif

	TD_PLACAR_CENTRAL dadosThreads[MAX_PLACARES + 1];
	TCHAR namedpipe[TAM];
	HANDLE hPipe = INVALID_HANDLE_VALUE;
	HANDLE thrPlacares[MAX_PLACARES + 1];
	HANDLE thrAdmin;
	DADOS_CENTRAL dc;

	BOOL continua = TRUE;


	dc.numPlacares = 0;
	dc.contadorID = 1;
	dc.continua = &continua;
	dc.hEventoSair = CreateEvent(NULL, TRUE, FALSE, NULL);
	InitializeCriticalSection(&dc.csPlacares);



	for (int i = 0; i < MAX_PLACARES; i++) {
		dc.placares[i].id = 0;
		dc.placares[i].hPipe = INVALID_HANDLE_VALUE;

		thrPlacares[i] = INVALID_HANDLE_VALUE;
	}

	if (argc != 2) {
		_tprintf_s(_T("[CENTRAL] Erro: Número de argumentos errados.\nUso correto: central.exe tubo.\n"));
		return 1;
	}

	_stprintf_s(namedpipe, TAM, _T("\\\\.\\pipe\\%s"), argv[1]);
	_tprintf(_T("Named Pipe = '%s'\n"), argv[1]);

	dc.hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SHM_ALERTA), NOME_SHM);

	if (dc.hMapFile == NULL) {
		_tprintf(_T("Erro ao criar a memoria partilhada.\n"));
		return 1;
	}

	dc.pShm = (SHM_ALERTA*)MapViewOfFile(dc.hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SHM_ALERTA));
	dc.hMutex = CreateMutex(NULL, FALSE, NOME_MUTEX);
	// VERIFICAÇÃO SE JÁ EXISTE OUTRA CENTRAL:
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		_tprintf(_T("Erro: Ja existe uma instancia da Central em execucao!\n"));

		// Como a outra central é que manda, fechamos o handle que tentámos abrir e saímos
		CloseHandle(dc.hMutex);
		return 0;
	}

	dc.hEventUpdate = CreateEvent(NULL, TRUE, FALSE, NOME_EVENTO_UPDATE);

	ZeroMemory(dc.pShm, sizeof(SHM_ALERTA));

	thrAdmin = CreateThread(NULL, 0, threadAdmin, (LPVOID)&dc, 0, NULL);

	while (*dc.continua) { // Use o ponteiro que você definiu
		_tprintf_s(_T("[DEBUG] Criar uma cópia do pipe '%s'...\n"), namedpipe);
		hPipe = CreateNamedPipe(
			namedpipe,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_MESSAGE |
			PIPE_READMODE_MESSAGE |
			PIPE_WAIT,
			MAX_PLACARES + 1,
			sizeof(MSG_ALERTA),
			sizeof(MSG_ALERTA),
			1000,
			NULL
		);

		OVERLAPPED ovConnect;
		ZeroMemory(&ovConnect, sizeof(ovConnect));
		ovConnect.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		BOOL ligado = ConnectNamedPipe(hPipe, &ovConnect);

		if (!ligado && GetLastError() == ERROR_IO_PENDING) {
			HANDLE esperaNoMain[2] = { ovConnect.hEvent, dc.hEventoSair };
			DWORD res = WaitForMultipleObjects(2, esperaNoMain, FALSE, INFINITE);

			if (res == WAIT_OBJECT_0 + 1) { // Evento de sair sinalizado
				_tprintf_s(_T("[CENTRAL] A encerrar loop principal.\n"));
				CloseHandle(ovConnect.hEvent);
				CloseHandle(hPipe);
				break;
			}
			ligado = TRUE; // Se foi o evento do pipe, está ligado
		}
		if (ligado) {
			EnterCriticalSection(&dc.csPlacares);

			if (dc.numPlacares < MAX_PLACARES) {
				dadosThreads[dc.numPlacares].hPipe = hPipe;
				dadosThreads[dc.numPlacares].dc = &dc;

				thrPlacares[dc.numPlacares] = CreateThread(NULL, 0, atendePlacar, &dadosThreads[dc.numPlacares], 0, NULL);
				dc.numPlacares++;

			}
			else {
				// REJEITAR: Limite atingido
				_tprintf_s(_T("[AVISO] Limite de %d placares atingido. Ligação rejeitada.\n"), MAX_PLACARES);
				CloseHandle(hPipe);
			}

			LeaveCriticalSection(&dc.csPlacares);
		}

	}

	WaitForSingleObject(thrAdmin, INFINITE);

	if (dc.pShm) UnmapViewOfFile(dc.pShm);
	if (dc.hMapFile) CloseHandle(dc.hMapFile);
	if (dc.hMutex) CloseHandle(dc.hMutex);
	if (dc.hEventUpdate) CloseHandle(dc.hEventUpdate);
	if (dc.hEventoSair) CloseHandle(dc.hEventoSair);

	DeleteCriticalSection(&dc.csPlacares);
	CloseHandle(hPipe);

	return 0;
}