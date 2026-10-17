#include "../comum.h"


HKEY abrirChaveProjeto(DWORD acesso) {
	HKEY hKey = NULL;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, _T("Software\\TrabSO2"), 0, NULL, REG_OPTION_NON_VOLATILE, acesso, NULL,
		&hKey, NULL) != ERROR_SUCCESS) {
		return NULL;
	}
	return hKey;
}

BOOL lerNomeNamedPipeDoRegistry(TCHAR* namedpipe, DWORD tamStr, int argc, TCHAR* argv[]) {
	HKEY hKey = abrirChaveProjeto(KEY_ALL_ACCESS);
	if (hKey == NULL) return FALSE;

	if (argc > 1) {
		_tcscpy_s(namedpipe, tamStr, argv[1]);
		RegSetValueEx(hKey, _T("NPIPE"), 0, REG_SZ, (BYTE*)namedpipe,
			(DWORD)(_tcslen(namedpipe) + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
		return TRUE;
	}

	DWORD tamBytes = tamStr * sizeof(TCHAR);
	LSTATUS res = RegQueryValueEx(hKey, _T("NPIPE"), NULL, NULL, (BYTE*)namedpipe, &tamBytes);
	RegCloseKey(hKey);

	return (res == ERROR_SUCCESS);
}

void imprimeAlerta(const TCHAR* msg) {
	SYSTEMTIME st;
	GetLocalTime(&st);
	_tprintf(_T("\n[%02d:%02d:%02d] %s\n"), st.wHour, st.wMinute, st.wSecond, msg);
}

// --- THREAD DA CONSOLA ---
DWORD WINAPI threadConsola(LPVOID param) {
	TD_CONSOLA* dados = (TD_CONSOLA*)param;
	TCHAR linha[TAM], cmd[TAM];
	MSG_CMD msgLigar;
	MSG_ID msgID;
	OVERLAPPED ov = { 0 };
	HANDLE hEvHandshake = CreateEvent(NULL, TRUE, FALSE, NULL);
	ov.hEvent = hEvHandshake;

	while (*dados->continua) {
		_tprintf(_T("\n> "));
		if (_fgetts(linha, TAM, stdin) == NULL) continue;
		if (_stscanf_s(linha, _T("%s"), cmd, (unsigned)_countof(cmd)) <= 0) continue;

		if (_tcscmp(cmd, _T("ligar")) == 0) {
			if (*dados->hPipe != INVALID_HANDLE_VALUE) {
				_tprintf(_T("[!] O placar já está ligado!\n"));
				continue;
			}

			// Tenta abrir o Pipe
			*dados->hPipe = CreateFile(dados->pipePath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
				OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

			if (*dados->hPipe == INVALID_HANDLE_VALUE) {
				_tprintf(_T("[ERRO] Não foi possível ligar à Central.\n"));
				continue;
			}

			_tprintf_s(_T("Liguei-me ao central...\n"));

			// Handshake (Pedir ID)
			msgLigar.tipo = 1;
			if (!WriteFile(*dados->hPipe, &msgLigar, sizeof(MSG_CMD), NULL, &ov)) {
				if (GetLastError() == ERROR_IO_PENDING)
					WaitForSingleObject(hEvHandshake, INFINITE);
				else {
					_tprintf(_T("[ERRO] Falha ao comunicar com o Central.\n"));
					CloseHandle(*dados->hPipe);
					*dados->hPipe = INVALID_HANDLE_VALUE;
					continue;
				}
			}
			ResetEvent(hEvHandshake);

			_tprintf_s(_T("Enviei %u byte ao central... MSG_CMD (tipo = %u)\n"), sizeof(msgLigar), msgLigar.tipo);

			if (!ReadFile(*dados->hPipe, &msgID, sizeof(MSG_ID), NULL, &ov)) {
				if (GetLastError() == ERROR_IO_PENDING) {
					if (WaitForSingleObject(hEvHandshake, 5000) == WAIT_TIMEOUT) { // Timeout de 5s
						_tprintf(_T("[ERRO] Central não respondeu a tempo.\n"));
						CancelIo(*dados->hPipe);
						CloseHandle(*dados->hPipe);
						*dados->hPipe = INVALID_HANDLE_VALUE;
						continue;
					}
				}
				else {
					_tprintf(_T("[REJEITADO] Central atingiu o limite ou fechou a ligação.\n"));
					CloseHandle(*dados->hPipe);
					*dados->hPipe = INVALID_HANDLE_VALUE;
					continue;
				}
			}

			_tprintf_s(_T("Recebi %u bytes ao central... MSG_CMD (tipo = %u)\n"), sizeof(msgID), msgID.tipo);

			if (msgID.tipo == 7) {
				_tprintf(_T("Identificador = %u\n"), msgID.identificador);
				SetEvent(dados->hEventPrincipal);
			}
			else {
				_tprintf(_T("[ERRO] Recebida resposta inválida do Central.\n"));
				CloseHandle(*dados->hPipe);
				*dados->hPipe = INVALID_HANDLE_VALUE;
			}

			// AVISA O MAIN PARA COMEÇAR A LER ALERTAS
			SetEvent(dados->hEventPrincipal);
		}
		else if (_tcscmp(cmd, _T("desligar")) == 0) {

			// Só tenta avisar a central se o handle for válido
			if (*dados->hPipe != INVALID_HANDLE_VALUE) {
				MSG_CMD msgDesligar;
				msgDesligar.tipo = 2; // Comando para desligar
				DWORD nBytes;

				WriteFile(*dados->hPipe, &msgDesligar, sizeof(msgDesligar), &nBytes, NULL);

				_tprintf_s(_T("[DEBUG] Enviei %u byte ao central... MSG_CMD (tipo = %d)\n"), sizeof(msgDesligar), msgDesligar.tipo);

			}

			break;
		}
		else {
			_tprintf_s(_T("Comando não reconhecido.\n"));
		}
	}
	CloseHandle(hEvHandshake);
	return 0;
}

int _tmain(int argc, TCHAR* argv[]) {
#ifdef UNICODE
	_setmode(_fileno(stdin), _O_WTEXT);
	_setmode(_fileno(stdout), _O_WTEXT);
#endif

	HANDLE hPipe = INVALID_HANDLE_VALUE;
	HANDLE hEvent, hTimer, hThread;
	TCHAR pipePath[TAM];
	MSG_ALERTA msgAlerta;
	OVERLAPPED ov = { 0 };
	BOOL continua = TRUE;

	TCHAR nomeSimples[TAM]; // Apenas "canal"
	TCHAR pipeCompleto[TAM]; // "\\.\pipe\canal"

	if (!lerNomeNamedPipeDoRegistry(nomeSimples, TAM, argc, argv)) {
		_tprintf(_T("[ERRO] Nome do pipe não definido!\n"));
		return 1;
	}

	_stprintf_s(pipeCompleto, TAM, _T("\\\\.\\pipe\\%s"), nomeSimples);
	_tprintf_s(_T("Named pipe = '%s'\n"), nomeSimples);

	hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	hTimer = CreateWaitableTimer(NULL, FALSE, NULL);


	// Configura estrutura da thread
	TD_CONSOLA dc;
	_tcscpy_s(dc.pipePath, TAM, pipeCompleto);
	dc.hPipe = &hPipe;
	dc.continua = &continua;
	dc.hEventPrincipal = hEvent;

	hThread = CreateThread(NULL, 0, threadConsola, &dc, 0, NULL);

	HANDLE hWaitHandles[2] = { hEvent, hTimer };

	while (continua) {
		// Se o pipe não estiver aberto, esperamos apenas pelo evento de "ligar" da consola
		if (hPipe == INVALID_HANDLE_VALUE) {
			WaitForSingleObject(hEvent, INFINITE);
			ResetEvent(hEvent);
			if (!continua) break; // Se o desligar foi premido, sai aqui
		}

		// Se chegámos aqui, o pipe está aberto. Vamos ler o que a Central manda.
		ZeroMemory(&ov, sizeof(ov));
		ov.hEvent = hEvent;

		// Leitura Overlapped
		BOOL lido = ReadFile(hPipe, &msgAlerta, sizeof(MSG_ALERTA), NULL, &ov);
		if (!lido && GetLastError() != ERROR_IO_PENDING) {
			_tprintf(_T("Erro no ReadFile ou Central fechou.\n"));
			CloseHandle(hPipe);
			hPipe = INVALID_HANDLE_VALUE;
			break;
		}

		DWORD res = WaitForMultipleObjects(2, hWaitHandles, FALSE, INFINITE);

		if (!continua) {
			CancelIoEx(hPipe, &ov); // Cancela a leitura pendente para o handle fechar bem
			break;
		}

		if (res == WAIT_OBJECT_0) {
			DWORD nBytes;
			if (!GetOverlappedResult(hPipe, &ov, &nBytes, FALSE)) {
				_tprintf(_T("\n[AVISO] Ligação fechada ou erro no pipe.\n"));
				CloseHandle(hPipe);
				hPipe = INVALID_HANDLE_VALUE; // Volta ao estado de espera
				continue;
			}

			if (nBytes > 0) {
				if (msgAlerta.tipo == 2) {
					MSG_CMD* msgDesligar = (MSG_CMD*)&msgAlerta;
					_tprintf_s(_T("[DEBUG] Recebi %u byte do central... MSG_CMD (tipo = %d)\n"), sizeof(*msgDesligar), msgDesligar->tipo);
					continua = FALSE;
					_tprintf(_T("A desligar...\n"));
				}
				else if (msgAlerta.tipo == 4) { // Alerta
					_tprintf_s(_T("[DEBUG] Recebi %u bytes do central... MSG_ALERTA (tipo = %u)\n"), sizeof(msgAlerta), msgAlerta.tipo);

					OVERLAPPED ov = { 0 };
					HANDLE hEvHandshake = CreateEvent(NULL, TRUE, FALSE, NULL);
					ov.hEvent = hEvHandshake;

					if (!WriteFile(hPipe, &msgAlerta, sizeof(msgAlerta), NULL, &ov)) {
						if (GetLastError() == ERROR_IO_PENDING) {
							WaitForSingleObject(hEvHandshake, INFINITE);
						}
						else {
							_tprintf_s(_T("Erro ao enviar mensagem de alerta de volta ao central\n"));
							CloseHandle(hEvHandshake);
							continue;
						}
					}
					_tprintf_s(_T("[DEBUG] Enviei %u bytes ao central... MSG_ALERTA (tipo = %u)\n"), sizeof(msgAlerta), msgAlerta.tipo);

					imprimeAlerta(msgAlerta.msg);
					LARGE_INTEGER liDueTime;
					liDueTime.QuadPart = -(LONGLONG)msgAlerta.duracao * 10000000LL;
					SetWaitableTimer(hTimer, &liDueTime, 0, NULL, NULL, FALSE);

					CloseHandle(hEvHandshake);
				}
				else if (msgAlerta.tipo == 5) {
					MSG_CMD* msgCancelar = (MSG_CMD*)&msgAlerta;
					_tprintf_s(_T("[DEBUG] Recebi %u bytes do central... MSG_CMD (tipo = %d)\n"), sizeof(*msgCancelar), msgCancelar->tipo);

					CancelWaitableTimer(hTimer);
					OVERLAPPED ov = { 0 };
					HANDLE hEvHandshake = CreateEvent(NULL, TRUE, FALSE, NULL);
					ov.hEvent = hEvHandshake;

					if (!WriteFile(hPipe, msgCancelar, sizeof(*msgCancelar), NULL, &ov)) {
						if (GetLastError() == ERROR_IO_PENDING) {
							WaitForSingleObject(hEvHandshake, INFINITE);
						}
						else {
							_tprintf_s(_T("Erro ao enviar mensagem de cancelar de volta ao central\n"));
							CloseHandle(hEvHandshake);
							continue;
						}
					}

					_tprintf_s(_T("[DEBUG] Enviei %u bytes ao central... MSG_CMD (tipo = %d)\n"), sizeof(*msgCancelar), msgCancelar->tipo);

					imprimeAlerta(_T("---"));

					CloseHandle(hEvHandshake);
				}
				else if (msgAlerta.tipo == 6) { // Encerrar
					MSG_CMD* msgEncerrar = (MSG_CMD*)&msgAlerta;
					_tprintf(_T("\n[DEBUG] Recebi %u byte do central... MSG_CMD (tipo = %d)\n"), sizeof(*msgEncerrar), msgEncerrar->tipo);
					continua = FALSE;

					SetEvent(hEvent);
					_tprintf_s(_T("A encerrar...\n"));
					break;
				}
			}
		}
		else if (res == WAIT_OBJECT_0 + 1) { // TIMER
			OVERLAPPED ov = { 0 };
			ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			MSG_CMD msg;
			msg.tipo = 3;

			if (!WriteFile(hPipe, &msg, sizeof(msg), NULL, &ov)) {
				if (GetLastError() == ERROR_IO_PENDING) {
					WaitForSingleObject(ov.hEvent, INFINITE);
				}
				else {
					_tprintf_s(_T("Erro ao enviar mensagem de alerta de volta ao central\n"));
					CloseHandle(ov.hEvent);
					continue;
				}
			}

			_tprintf_s(_T("[DEBUG] Enviei %u byte ao central... MSG_CMD (tipo = %u)"), sizeof(msg), msg.tipo);
			imprimeAlerta(_T("---"));
			CloseHandle(ov.hEvent);
		}



	}
	if (hPipe != INVALID_HANDLE_VALUE) {
		CancelIoEx(hPipe, NULL);
		CloseHandle(hPipe);
		hPipe = INVALID_HANDLE_VALUE;
	}

	CancelSynchronousIo(hThread);

	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
	CloseHandle(hEvent);
	CloseHandle(hTimer);
	return 0;
}